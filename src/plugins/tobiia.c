/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <eegdev-pluginapi.h>
#include <expat.h>
#include <mmerrno.h>
#include <mmsysio.h>
#include <mmthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/*****************************************************************
 *                        TOBIIA metadata                        *
 *****************************************************************/
struct tia_signal_information {
	struct egdi_signal_info si;
	const char* type;
	uint32_t mask;
	int aperiodic;
};
#define get_tia_si(si) ((struct tia_signal_information*)si)

#define SIGELEC .si = {.dtype=EGD_FLOAT, .unit="uV"}
#define SIGUNK .si = {.dtype=EGD_FLOAT, .unit="Unknown"}
#define SIGBOOL .si = {.dtype=EGD_FLOAT, .unit="Boolean",\
                        .isint = 1, .prefiltering="No filtering"}
#define SIGNOFIL .si = {.dtype=EGD_FLOAT,  .unit="Unknown",\
                             .prefiltering="No filtering"}

static const struct tia_signal_information sig_info[] = {
	{.type = "eeg", .mask = 0x00000001, SIGELEC},
	{.type = "emg", .mask = 0x00000002, SIGELEC},
	{.type = "eog", .mask = 0x00000004, SIGELEC},
	{.type = "ecg", .mask = 0x00000008, SIGELEC},
	{.type = "hr", .mask = 0x00000010, SIGUNK},
	{.type = "bp", .mask = 0x00000020, SIGUNK},
	{.type = "button", .mask = 0x00000040, SIGBOOL, .aperiodic = 1},
	{.type = "joystick", .mask = 0x00000080, SIGNOFIL, .aperiodic = 1},
	{.type = "sensors", .mask = 0x00000100, SIGUNK},
	{.type = "nirs", .mask = 0x00000200, SIGUNK},
	{.type = "fmri", .mask = 0x00000400, SIGUNK},
	{.type = "mouse", .mask = 0x00000800, SIGNOFIL, .aperiodic = 1},
	{.type = "mouse-button", .mask = 0x00001000, SIGBOOL, .aperiodic=1},
	{.type = "user1", .mask = 0x00010000, SIGUNK},
	{.type = "user2", .mask = 0x00020000, SIGUNK},
	{.type = "user3", .mask = 0x00040000, SIGUNK},
	{.type = "user4", .mask = 0x00080000, SIGUNK},
	{.type = "undefined", .mask = 0x00100000, SIGUNK},
	{.type = "event", .mask = 0x00200000,
	 .si = {.dtype=EGD_FLOAT, .transducer="Triggers and Status",
	        .unit = "Boolean", .isint = 1}}
};
#define TIA_NUM_SIG (sizeof(sig_info)/sizeof(sig_info[0]))

static const char tia_device_type[] = "TOBI interface A";

typedef enum {RUNNING, STOP} tia_state_t;
enum {OPT_HOST, OPT_PORT, NUMOPT};
static const struct egdi_optname tia_options[] = {
	[OPT_HOST] = {.name = "host", .defvalue = NULL},
	[OPT_PORT] = {.name = "port", .defvalue = "38500"},
	[NUMOPT] = {.name = NULL}
};


#define XML_BSIZE	4096

struct tia_eegdev {
	struct devmodule dev;
	FILE* ctrl;
	int datafd, ctrlfd;
	mmthread_t thid;
	XML_Parser parser;

	int fs, blocksize;
	unsigned int nch, nsig;
	int offset[TIA_NUM_SIG];

	struct egdi_chinfo* chmap;

	tia_state_t reader_state;
	mmthr_mtx_t reader_state_lock;
};
#define get_tia(dev_p) 	((struct tia_eegdev*)(dev_p))

struct parsingdata {
	struct tia_eegdev* tdev;
	int sig, nch, invalid;
	char ltype[16];
	struct plugincap cap;
};


/*****************************************************************
 *                        tobiia misc                            *
 *****************************************************************/
static
int get_eegdev_sigtype(const char* sig)
{
	int sigid;

	if (!strcmp(sig, "eeg"))
		sigid = EGD_EEG;	
	else if (!strcmp(sig, "event"))
		sigid = EGD_TRIGGER;
	else
		sigid = EGD_SENSOR;
	
	return sigid;
}

static
int get_tobiia_siginfo_mask(uint32_t type)
{
	int i;

	for (i=0; i<(int)TIA_NUM_SIG; i++)
		if (type == sig_info[i].mask)
			return i;
	
	return -1;
}

static
int get_tobiia_siginfo_type(const char* ltype)
{
	int i;

	for (i=0; i<(int)TIA_NUM_SIG; i++)
		if (!strcmp(ltype,sig_info[i].type))
			return i;
	
	return -1;
}


static
int parse_url(const char* url, char* host, unsigned short *port)
{
	if (!sscanf(url, "%[^][:]:%hu", host, port)
	 && !sscanf(url, "%[:0-9a-f]", host)
	 && !sscanf(url, "[%[:0-9a-f]]:%hu", host, port)) {
		fprintf(stderr, "Cannot parse address\n");
		return -1;
	}

	return 0;
}


static
int connect_server(const char *host, unsigned int short port)
{
	char uri[128];

	if (!host || host[0] == '\0')
		host = "localhost";
	snprintf(uri, sizeof(uri), "tcp://%s:%hu", host, port);

	return mm_create_sockclient(uri);
}


static
int fullread(int fd, void* buff, size_t count)
{
	do {
		ssize_t rsiz = mm_read(fd, buff, count);
		if (rsiz <= 0) {
			if (rsiz == 0)
				errno = EPIPE;
			return -1;
		}
		count -= rsiz;
		buff = ((char*)buff) + rsiz;
	} while(count);
	return 0;
}


static
int fullwrite(int fd, const void* buff, size_t count)
{
	do {
		ssize_t rsiz = write(fd, buff, count);
		if (rsiz < 0)
			return -1;
		count -= rsiz;
		buff = ((char*)buff) + rsiz;
	} while(count);
	return 0;
}


/*****************************************************************
 *                        tobiia XML parsing                     *
 *****************************************************************/
static
int ch_cmp(const void* e1, const void* e2)
{
	const struct egdi_chinfo *ch1 = e1, *ch2 = e2;
	const struct tia_signal_information *tsinfo1, *tsinfo2;

	tsinfo1 = get_tia_si(ch1->si);
	tsinfo2 = get_tia_si(ch2->si);

	if (tsinfo1->mask == tsinfo2->mask)
		return 0;
	else if (tsinfo1->mask < tsinfo2->mask)
		return -1;
	else
		return 1;
}


static
void parse_start_tiametainfo(struct parsingdata* data)
{
	struct tia_eegdev* tdev = data->tdev;
	unsigned int i;

	for (i=0; i<TIA_NUM_SIG; i++)
		tdev->offset[i] = -1;
}


static
void parse_end_tiametainfo(struct parsingdata* data)
{
	struct tia_eegdev* tdev = data->tdev;
	unsigned int i;
	int signch, nch = 0;

	qsort(tdev->chmap, tdev->nch, sizeof(*tdev->chmap), ch_cmp);

	for (i=0; i<TIA_NUM_SIG; i++) {
		if (tdev->offset[i] < 0)
			continue;
		signch = tdev->offset[i]+1;
		tdev->offset[i] = nch;
		nch += signch;
	}
}


static
int parse_start_mastersignal(struct parsingdata* data, const char **attr)
{
	struct tia_eegdev* tdev = data->tdev;
	unsigned int i;
	
	// Read signal metadata
 	for (i=0; attr[i]; i+=2) {
		if (!strcmp(attr[i], "samplingRate"))
			data->cap.sampling_freq = atoi(attr[i+1]);
		else if (!strcmp(attr[i], "blockSize"))
			tdev->blocksize = atoi(attr[i+1]);
	}

	return 0;
}


static
int parse_start_signal(struct parsingdata* data, const char **attr)
{
	unsigned int i, fs = 0;
	int sig, tiatype, bs = 0;
	struct egdi_chinfo *newchmap = data->tdev->chmap;
	const char* ltype = NULL;
	struct tia_eegdev* tdev = data->tdev;
	
	// Read signal metadata
 	for (i=0; attr[i]; i+=2) {
		if (!strcmp(attr[i], "type"))
			ltype = attr[i+1];
		else if (!strcmp(attr[i], "numChannels"))
			data->nch = atoi(attr[i+1]);
		else if (!strcmp(attr[i], "samplingRate"))
			fs = atoi(attr[i+1]);
		else if (!strcmp(attr[i], "blockSize"))
			bs = atoi(attr[i+1]);
	}

	// For the moment we support only signal with the same samplerate
	// as the mastersignal
	if ((data->cap.sampling_freq != fs) || (tdev->blocksize != bs)) 
		return -1;

	tdev->nsig++;
	sig = get_eegdev_sigtype(ltype);

	// resize metadata structures to hold new channels
	tdev->nch += data->nch;
	newchmap = realloc(newchmap, tdev->nch*sizeof(*newchmap));
	if (!newchmap)
		return -1;
	tdev->chmap = newchmap;

	tiatype = get_tobiia_siginfo_type(ltype);
	if (tiatype < 0)
		return -1;
	tdev->offset[tiatype] += data->nch;
	
	for (i=tdev->nch - data->nch; i<tdev->nch; i++) {
		tdev->chmap[i].stype = sig;
		tdev->chmap[i].label = NULL;
		tdev->chmap[i].si = &sig_info[tiatype].si;
	}
	data->sig = sig;
	strncpy(data->ltype, ltype, sizeof(data->ltype)-1);

	return 0;
}


static
int parse_end_signal(struct parsingdata* data)
{
	struct tia_eegdev* tdev = data->tdev;
	int i;
	size_t len = strlen(data->ltype)+8;
	struct egdi_chinfo* newmap = tdev->chmap + (tdev->nch - data->nch);
	char* label;
	
	// Assign default labels for unlabelled channels
	for (i=0; i<data->nch; i++) {
		if (!newmap[i].label) {
			if (!(label = malloc(len)))
				return -1;
			sprintf(label, "%s:%u", data->ltype, i+1);
			newmap[i].label = label;
		}
	}

	return 0;
}


static
int parse_start_channel(struct parsingdata* data, const char **attr)
{
	int index = -1, i;
	const char* label = NULL;
	char* newlabel;
	struct tia_eegdev* tdev = data->tdev;

 	for (i=0; attr[i]; i+=2) {
		if (!strcmp(attr[i], "nr"))
			index = atoi(attr[i+1])-1;
		else if (!strcmp(attr[i], "label"))
			label = attr[i+1];
	}

	// locate the channel to modify
	if (index >= data->nch || index < 0)
		return -1;
	i = tdev->nch - data->nch + index;
	
	// Change the label
	if (!(newlabel = realloc((char*)tdev->chmap[i].label, strlen(label)+1)))
		return -1;
	strcpy(newlabel, label);
	tdev->chmap[i].label = newlabel;
	
	return 0;
}


static XMLCALL
void start_xmlelt(void *data, const char *name, const char **attr)
{
	struct parsingdata* pdata = data;
	int ret = 0;

	if (!pdata)
		return;

	if (!strcmp(name, "tiaMetaInfo")) 
		parse_start_tiametainfo(pdata);
	else if (!strcmp(name, "masterSignal"))
	      ret = parse_start_mastersignal(pdata, attr);
	else if (!strcmp(name, "signal"))
	      ret = parse_start_signal(pdata, attr);
	else if (!strcmp(name, "channel"))
	      ret = parse_start_channel(pdata, attr);

	if (ret) {
		pdata->invalid = 1;
		XML_StopParser(pdata->tdev->parser, XML_FALSE);
	}	
}


static XMLCALL
void end_xmlelt(void *data, const char *name)
{
	struct parsingdata* pdata = data;

	if (!pdata)
		return;

	if (!strcmp(name, "signal")) {
		if (parse_end_signal(pdata)) {
			pdata->invalid = 1;
			XML_StopParser(pdata->tdev->parser, XML_FALSE);
		}
	} else if (!strcmp(name, "tiaMetaInfo"))
		parse_end_tiametainfo(pdata);
}


static
int init_xml_parser(struct tia_eegdev* tdev)
{
	if (!(tdev->parser = XML_ParserCreate("UTF-8")))
		return -1;

	XML_SetElementHandler(tdev->parser, start_xmlelt, end_xmlelt);

	return 0;
}


static
int parse_xml_message(struct tia_eegdev* tdev, unsigned int len,
                                               struct parsingdata* data)
{
	void *xmlbuf;
	unsigned int clen;

	if (data == NULL)
		return -1;

	// Read and parse additional XML content by chunks
	XML_SetUserData(tdev->parser, data);
	while (len) {
		clen = (len < XML_BSIZE) ? len : XML_BSIZE;

		// Alloc chunk XML buffer, fill it, parse it
		if ( !(xmlbuf = XML_GetBuffer(tdev->parser, XML_BSIZE))
		  || !fread(xmlbuf, clen, 1, tdev->ctrl)
		  || !XML_ParseBuffer(tdev->parser, clen, !(len-=clen))
		  || data->invalid )
			return -1;
	}

	return 0;
}


/*****************************************************************
 *                  tobiia control communication                 *
 *****************************************************************/
enum protcall {
	TIA_VERSION = 0,
	TIA_METAINFO,
	TIA_DATACONNECTION,
	TIA_STARTDATA,
	TIA_STOPDATA,
	TIA_STATECONNECTION
};

static const char* const prot_request[] = {
	[TIA_VERSION] = "CheckProtocolVersion",
	[TIA_METAINFO] = "GetMetaInfo",
	[TIA_DATACONNECTION] = "GetDataConnection: TCP",
	[TIA_STARTDATA] = "StartDataTransmission",
	[TIA_STOPDATA] = "StopDataTransmission",
	[TIA_STATECONNECTION] = "GetServerStateConnection",
};
static const char* const prot_answer[] = {
	[TIA_VERSION] = "OK",
	[TIA_METAINFO] = "MetaInfo",
	[TIA_DATACONNECTION] = "DataConnectionPort: %i",
	[TIA_STARTDATA] = "OK",
	[TIA_STOPDATA] = "OK",
	[TIA_STATECONNECTION] = "ServerStateConnectionPort: %i",
};


static
int tia_request(struct tia_eegdev* tdev, enum protcall req,
                                         struct parsingdata* data)
{
	char buffer[64], msg[32];
	unsigned int vers[2], len = 0;
	int ret = 0;

	// Send request and read answer message header
	sprintf(buffer, "TiA 1.0\n%s\n\n", prot_request[req]);
	if (fullwrite(tdev->ctrlfd, buffer, strlen(buffer))
	   || !fgets(buffer, sizeof(buffer), tdev->ctrl)
           || sscanf(buffer, " TiA %u.%u", vers, vers+1) < 2
	   || !fgets(buffer, sizeof(buffer), tdev->ctrl)
           || sscanf(buffer, " %31[^\n]", msg) < 1
	   || !fgets(buffer, sizeof(buffer), tdev->ctrl))
		return -1;

	// Read if additional content is present and skip one line if so
	sscanf(buffer, " Content-Length: %u\n", &len);
	if (len) {
		if ( !fgets(buffer, sizeof(buffer), tdev->ctrl)
		   || parse_xml_message(tdev, len, data))
		return -1;
	}

	// Parse returned message
	switch (req) {
	case TIA_METAINFO:
	case TIA_VERSION:
	case TIA_STARTDATA:
	case TIA_STOPDATA:
		if (strcmp(msg, prot_answer[req]))
			ret = -1;
		break;

	case TIA_DATACONNECTION:
	case TIA_STATECONNECTION:
		if (!sscanf(msg, prot_answer[req], &ret))
			ret = -1;
		break;
	}

	return ret;
}


static
int init_ctrl_com(struct tia_eegdev* tdev, const char* host, int port)
{
	if ((tdev->ctrlfd = connect_server(host, port)) < 0)
		return -1;
	
	if (!(tdev->ctrl = fdopen(tdev->ctrlfd, "r"))) {
		close(tdev->ctrlfd);
		tdev->ctrlfd = -1;
		return -1;
	}
	
	return 0;
}


/*****************************************************************
 *                     tobiia data communication                 *
 *****************************************************************/
struct data_hdr {
	uint8_t padding[7]; 
	uint8_t version;
	uint32_t size;
	uint32_t type_flags;
	uint64_t id;
	uint64_t number;
	uint64_t ts;
};

#define DATHDR_LEN	(sizeof(struct data_hdr)-7)
#define DATHDR_OFF	offsetof(struct data_hdr, version)

static
unsigned int parse_type_flags(uint32_t flags, const struct tia_eegdev* tdev,
                              int offset[32])
{
	unsigned int i, nsig = 0;
	int tiatype;
	uint32_t mask;
	
	// Estimate number of signal
	for (i=0; i<32; i++) {
		mask = ((uint32_t)1) << i;
		if (flags & mask) {
			nsig++;

			// Retrieve the type of flagged signal
			if ((tiatype = get_tobiia_siginfo_mask(mask)) < 0)
				continue;

			// setup the sample offset according to the type
			// of the signal
			offset[nsig-1] = tdev->offset[tiatype];
		}
	}

	return nsig;
}


static
size_t unpack_datapacket(const struct tia_eegdev* tdev,
                         uint32_t type_flags, const void* pbuf, void* sbuf)
{
	unsigned int i, ich, sig, nsig;
	const uint16_t *numch, *blocksize;
	unsigned int stride = tdev->nch;
	float* data = sbuf;
	const float* sigb;
	int off[32];

	// Parse type flags and packet pointer accordingly
	nsig = parse_type_flags(type_flags, tdev, off);
	numch = (const uint16_t*)pbuf;
	blocksize = ((const uint16_t*)pbuf) + nsig;
	sigb = (const float*)(((const uint16_t*)pbuf) + 2*nsig);

	// convert array grouped by signal type into an array
	// grouped by samples
	for (sig=0; sig<nsig; sig++) {
		// negative offset means that signal should not be sent
		if (off[sig] < 0) {
			sigb += numch[sig]*blocksize[sig];
			continue;
		}

		for (i=0; i<blocksize[sig]; i++) {
			for (ich=0; ich<numch[sig]; ich++)
				data[i*stride + off[sig] + ich] = sigb[ich];
			sigb += numch[sig];
		}
	}

	return blocksize[0]*stride*sizeof(float);
}

static
void* data_fn(void *data)
{
	struct tia_eegdev* tdev = data;
	const struct core_interface* restrict ci = &tdev->dev.ci;
	struct data_hdr hdr;
	size_t blen, pbsize;
	int fd = tdev->datafd;
	tia_state_t reader_state;
	void *sbuf = NULL, *pbuf = NULL;

	mmthr_mtx_lock(&tdev->reader_state_lock);
	reader_state = tdev->reader_state;
	mmthr_mtx_unlock(&tdev->reader_state_lock);

	// Allocate utility packet and sample buffers
	pbsize = tdev->nsig*2*sizeof(uint16_t)
	         + tdev->blocksize*tdev->nch*sizeof(float);
	pbuf = malloc(pbsize);
	sbuf = malloc(tdev->nch*sizeof(float)*tdev->blocksize);

	while (pbuf && sbuf && reader_state == RUNNING) {
		// Read packet header
		if (fullread(fd, &(hdr.version), DATHDR_LEN))
			break;

		// Resize packet buffer if too small
		if (pbsize < hdr.size-DATHDR_LEN) {
			pbsize = hdr.size-DATHDR_LEN;
			free(pbuf);
			if (!(pbuf = malloc(pbsize)))
				break;
		}

		// Read packet data
		if (fullread(fd, pbuf, hdr.size-DATHDR_LEN))
			break;

		// Parse packet and update ringbuffer
		blen = unpack_datapacket(tdev, hdr.type_flags, pbuf, sbuf);
		if (ci->update_ringbuffer(&tdev->dev, sbuf, blen))
			break;

		mmthr_mtx_lock(&tdev->reader_state_lock);
		reader_state = tdev->reader_state;
		mmthr_mtx_unlock(&tdev->reader_state_lock);
	}

	if(reader_state == RUNNING) // if true there has been an error
		ci->report_error(&tdev->dev, errno);

	free(pbuf);
	free(sbuf);

	return NULL;
}


static
int init_data_com(struct tia_eegdev* tdev, const char* host)
{
	int port;
	struct devmodule* dev = &tdev->dev;

	dev->ci.set_input_samlen(dev, tdev->nch*sizeof(float));
	tdev->reader_state = RUNNING;

	if ( (port = tia_request(tdev, TIA_DATACONNECTION, NULL)) < 0
          || (tdev->datafd = connect_server(host, port)) < 0
	  || mmthr_create(&tdev->thid, data_fn, tdev) ) {
	  	if (tdev->datafd >= 0) {
			mm_close(tdev->datafd);
			tdev->datafd = -1;
		}
		tdev->reader_state = STOP;
		return -1;
	}

	return 0;
}

/******************************************************************
 *                             Metadata                           *
 ******************************************************************/
static
int setup_device_config(struct tia_eegdev* tdev, const char* url)
{
	struct parsingdata data = {.tdev = tdev};
	struct devmodule* dev = &tdev->dev;
	struct blockmapping mappings = {.num_skipped = 0};

	// Request system information from server
	if (tia_request(tdev, TIA_METAINFO, &data))
		return -1;

	// setup device capabilities with the digested metainfo
	mappings.nch = tdev->nch;
	mappings.chmap = tdev->chmap;
	data.cap.mappings = &mappings;
	data.cap.num_mappings = 1;
	data.cap.device_type = tia_device_type;
	data.cap.device_id = url ? url : "local server";
	data.cap.flags = EGDCAP_NOCP_CHMAP | EGDCAP_NOCP_CHLABEL
	                                              | EGDCAP_NOCP_DEVTYPE;
	dev->ci.set_cap(dev, &data.cap);

	return 0;
}

/******************************************************************
 *                  Init/Destroy TOBIIA device                    *
 ******************************************************************/
static
int tia_close_device(struct devmodule* dev)
{
	struct tia_eegdev* tdev = get_tia(dev);
	unsigned int i;

	// Free channels metadata
	for (i=0; i<tdev->nch; i++)
		free((char*)tdev->chmap[i].label);
	free(tdev->chmap);

	// Destroy control connection
	if (tdev->ctrl) {
		mm_shutdown(fileno(tdev->ctrl), SHUT_RDWR);
		fclose(tdev->ctrl);
	}

	// Destroy data connection
	if (tdev->datafd >= 0) {
		mmthr_mtx_lock(&tdev->reader_state_lock);
		tdev->reader_state = STOP;
		mmthr_mtx_unlock(&tdev->reader_state_lock);
		mmthr_join(tdev->thid, NULL);
		mm_close(tdev->datafd);
	}

	// Destroy XML parser
	if (tdev->parser)
  		XML_ParserFree(tdev->parser);

	mmthr_mtx_deinit(&tdev->reader_state_lock);
	return 0;
}


static
int tia_open_device(struct devmodule* dev, const char* optv[])
{
	struct tia_eegdev* tdev = get_tia(dev);
	unsigned short port = atoi(optv[OPT_PORT]);
	const char *url = optv[OPT_HOST];
	size_t hostlen = url ? strlen(url) : 0;
	char hoststring[hostlen + 1];
	char* host = url ? hoststring : NULL;

	tdev->datafd = tdev->ctrlfd = -1;
	mmthr_mtx_init(&tdev->reader_state_lock, 0);

	if ( (url && parse_url(url, host, &port))
	  || init_xml_parser(tdev)
	  || init_ctrl_com(tdev, host, port)
	  || setup_device_config(tdev, url)
	  || init_data_com(tdev, host) ) {
		tia_close_device(dev);
		return -1;
	}
	return 0;
}


/******************************************************************
 *                  tobiia methods implementation                 *
 ******************************************************************/
static 
void tia_fill_chinfo(const struct devmodule* dev, int stype,
	             unsigned int ich, struct egdi_chinfo* info,
		     struct egdi_signal_info* si)
{
	unsigned int index, sch = 0;
	struct tia_eegdev* tdev = get_tia(dev);
	
	// Find channel mapping
	for (index=0; index<tdev->nch; index++) {
		if (tdev->chmap[index].stype == stype)
			if (ich == sch++)
				break;
	}
	
	// Fill channel metadata
	info->label = tdev->chmap[index].label;
	memcpy(si, tdev->chmap[index].si, sizeof(*si));

	// Guess the scaling information from the integer type
	if (!si->isint) {
		si->mmtype = EGD_DOUBLE;
		si->min.valdouble = -262144.0;
		si->max.valdouble = 262143.96875;
	} else {
		si->mmtype = EGD_INT32;
		si->min.valint32_t = -8388608;
		si->max.valint32_t = 8388607;
	}
}


static
int tia_start_acq(struct devmodule* dev)
{
	return tia_request(get_tia(dev), TIA_STARTDATA, NULL);
}


static
int tia_stop_acq(struct devmodule* dev)
{
	return tia_request(get_tia(dev), TIA_STOPDATA, NULL);
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct tia_eegdev),
	.open_device = 		tia_open_device,
	.close_device = 	tia_close_device,
	.fill_chinfo = 		tia_fill_chinfo,
	.start_acq = 		tia_start_acq,
	.stop_acq = 		tia_stop_acq,
	.supported_opts = 	tia_options
};

