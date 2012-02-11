/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <pthread.h>
#include <expat.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <portable-time.h>

#include "src/plugins/cross-socket.h"

#include "time-utils.h"
#include "tia-server.h"

#define CTRL_PORT	38500
#define DATA_PORT	38501

#define METATMPFILE	"metainfoxml.tmp"

enum protcall {
	TIA_VERSION = 0,
	TIA_METAINFO,
	TIA_DATACONNECTION,
	TIA_STARTDATA,
	TIA_STOPDATA,
	TIA_STATECONNECTION,
	NUM_PROTCALL
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

static pthread_t ctrl_thid, data_thid;
static int listenfd = -1, datafd = -1;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int acq_run = -1;
static struct timespec acq_ts;

static const uint32_t type_flags = 0x00000001 | 0x00000002 | 0x00200000;
static const unsigned int num_sig_ch[] = {16, 4, 1};
static const char* chtype[] = {"eeg", "emg", "event"};
static unsigned int samplingrate = 128;
static unsigned int blocksize = 10;
#define NSIG	(sizeof(num_sig_ch)/sizeof(num_sig_ch[0]))


static
int create_listening_socket(unsigned short port)
{
	int fd = -1, reuse = 1, v6only = 0;
	struct sockaddr_in6 saddr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(port),
		.sin6_addr = IN6ADDR_ANY_INIT
	};
	
	if ((fd = sock_socket(AF_INET6, SOCK_STREAM, 0)) == -1
	 || sock_setsockopt(fd, SOL_SOCKET, 
	                              SO_REUSEADDR, &reuse, sizeof(reuse))
	 || sock_setsockopt(fd, IPPROTO_IPV6,
	                              IPV6_V6ONLY, &v6only, sizeof(v6only))
	 || sock_bind(fd, (const struct sockaddr*)&saddr, sizeof(saddr))
	 || sock_listen(fd, 32)) {
		close(fd);
		fd = -1;
	}

	return fd;
}


/**********************************************************************
 *                                                                    *
 *                                                                    *
 *                                                                    *
 **********************************************************************/
struct data_hdr {
	uint8_t padding[7]; 
	uint8_t version;
	uint32_t size;
	uint32_t type_flags;
	uint64_t id;
	uint64_t number;
	uint64_t ts;
};

#define DATHDR_OFF	offsetof(struct data_hdr, version)
#define DATHDR_LEN	(sizeof(struct data_hdr)-DATHDR_OFF)


static
int write_data_packet(char* buffer)
{
	static size_t sam  = 0; 
	static uint64_t packet_num = 0;

	unsigned int i, j, k;
	unsigned int nchtot = 0;
	struct data_hdr* hdr = (struct data_hdr*)buffer;
	uint16_t* varhdr = (uint16_t*)(buffer+sizeof(*hdr));
	float* data;

	for (i=0; i<NSIG; i++) 
		nchtot += num_sig_ch[i];
	
	data = (float*)(buffer+sizeof(*hdr) + (2*sizeof(uint16_t))*NSIG);
	hdr->version = 3;
	hdr->id = 0;
	hdr->ts = 0;
	hdr->number = packet_num++;
	hdr->type_flags = type_flags;
	hdr->size = sizeof(*hdr) + 2*sizeof(uint16_t)*NSIG
	                         + sizeof(float)*nchtot*blocksize;

	for (i=0; i<NSIG; i++) {
		varhdr[i] = num_sig_ch[i];
		varhdr[i+NSIG] = blocksize;
		for (j=0; j<blocksize; j++) {
			for (k=0; k<num_sig_ch[i]; k++)
				data[k] = (i < NSIG-1) ? get_analog_val(sam+j, k) : get_trigger_val(sam+j, k);
			data += num_sig_ch[i];
		}
	}
	sam += blocksize;

	return hdr->size;
}


static
void* data_socket_fn(void* data)
{
	(void)data;
	int ret = 0;
	int fd, len;
	struct sockaddr_in cliaddr;
	socklen_t clilen = sizeof(cliaddr);
	char buffer[8192];

	// Accept the first connection
	fd = sock_accept(datafd, (struct sockaddr *) &cliaddr, &clilen);
	
	while (!ret) {
		pthread_mutex_lock(&lock);
		while (!acq_run) 
			pthread_cond_wait(&cond, &lock);
		ret = (acq_run < 0);
		pthread_mutex_unlock(&lock);

		len = write_data_packet(buffer);
		if (write(fd, buffer+DATHDR_OFF, len) < len)
			break;

		// For the next data chunk to be available
		addtime(&acq_ts, 0, (1000000000/samplingrate)*blocksize);
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &acq_ts, NULL);
	}

	// Control connection and listening socket
	close(fd);

	return NULL;
}


/**********************************************************************
 *                                                                    *
 *                                                                    *
 *                                                                    *
 **********************************************************************/
static
int write_metainfo(FILE* fp)
{
	unsigned int i, j;
	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		    "<tiaMetaInfo version=\"1.0\">\n"
		    "<subject id=\"FAKEID\" firstName=\"John\" lastName=\"Smith\" handedness=\"r\" />\n"
		    "<masterSignal samplingRate=\"%u\" blockSize=\"%u\" />\n",
		    samplingrate, blocksize);
	
	for (i=0; i<NSIG; i++) {
		fprintf(fp, "<signal type=\"%s\" blockSize=\"%u\""
		            " samplingRate=\"%u\" numChannels=\"%u\">\n",
			    chtype[i], blocksize,
			    samplingrate, num_sig_ch[i]);
		for (j=0; j<num_sig_ch[i]; j++)
			fprintf(fp, "  <channel label=\"tobi%s:%u\" "
			                                "nr=\"%u\" />\n",
			            chtype[i], j+1, j+1);
		fprintf(fp, "</signal>\n");
	}

	fprintf(fp, "</tiaMetaInfo>\n");
	
	return 0;
}


static
int reply_msg(int fd, const char* answer, int contentlen, const char* content)
{
	char buffer[256];
	size_t count;

	// Format the reply header
	sprintf(buffer, "TiA 1.0\n%s\n", answer);
	count = strlen(buffer);
	if (contentlen && content) {
		sprintf(buffer + count, "Content-Length: %i\n", contentlen);
		count += strlen(buffer + count);
	}
	buffer[count++] = '\n';

	// Write the reply header and optionally the content to the socket
	if (write(fd, buffer, count) < (ssize_t)count)
		goto error;

	if (contentlen && content) 
		if (write(fd, content, contentlen)<contentlen)
			goto error;

	return 0;
error:
	fprintf(stderr, "Error in sending reply: %s (%i)\n", strerror(errno), errno);
	return -1;
}

/* Unused for the moment
static
int reply_error(int fd, const char* errmsg)
{
	char buffer[256];

	if (errmsg) {
		sprintf(buffer, "<tiaError version=\"1.0\" description=\"%s\"/>", errmsg);
		reply_msg(fd, "Error", strlen(buffer), buffer);
	} else
		reply_msg(fd, "Error", 0, NULL);
		
	return 0;
}
*/

static
int reply_metainfo(int fd)
{	
	void* buffer;
	size_t clen;
	int ret = 0;
	FILE* metafp;

	
	// This is not elegant but we do not care:
	// this server is not a real one, it is here only for
	// testing purpose
	metafp = fopen(METATMPFILE, "w+b");
	if (!metafp)
		return -1;

	// Fill meta info
	write_metainfo(metafp);

	// Get the length of the metainfo
	fseek(metafp, 0, SEEK_END);
	clen = (size_t) ftell(metafp);

	// Copy the metainfo into a memory buffer
	fseek(metafp, 0, SEEK_SET);
	buffer = malloc(clen);
	if (fread(buffer, clen, 1, metafp) < 1)
		ret = -1;
	fclose(metafp);

	// Send the reply to the socket
	if (!ret)
		ret = reply_msg(fd, "MetaInfo", clen, buffer);
	free(buffer);

	return ret;
}


static
void destroy_dataloop(void)
{
	if (acq_run < -1)
		return;

	pthread_mutex_lock(&lock);
	acq_run = -1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);

	pthread_join(data_thid, NULL);
}

static
int reply_dataconnection(int fd)
{
	char buffer[32];

	// Create a socket
	datafd = create_listening_socket(DATA_PORT);

	pthread_mutex_lock(&lock);
	acq_run = 0;
	pthread_mutex_unlock(&lock);
	pthread_create(&data_thid, NULL, data_socket_fn, NULL);

	sprintf(buffer, "DataConnectionPort: %i", DATA_PORT);
	return reply_msg(fd, buffer, 0, NULL);
}


static
int reply_startdata(int fd)
{
	pthread_mutex_lock(&lock);
	clock_gettime(CLOCK_REALTIME, &acq_ts);
	acq_run = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
	return reply_msg(fd, "OK", 0, NULL);
}


static
int reply_stopdata(int fd)
{
	pthread_mutex_lock(&lock);
	acq_run = 0;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
	return reply_msg(fd, "OK", 0, NULL);
}

/**********************************************************************
 *                                                                    *
 *                                                                    *
 *                                                                    *
 **********************************************************************/
static
int read_ctrl_msg(FILE* fp)
{
	char buffer[64], msg[32];
	unsigned int vers[2], clen = 0;
	size_t rlen;
	int i;

	// Read request message header
	if ( !fgets(buffer, sizeof(buffer), fp)
           || sscanf(buffer, " TiA %u.%u", vers, vers+1) < 2
	   || !fgets(buffer, sizeof(buffer), fp)
           || sscanf(buffer, " %31[^\n]", msg) < 1
	   || !fgets(buffer, sizeof(buffer), fp))
		return -1;

	// Read if additional content is present and skip one line if so
	sscanf(buffer, " Content-Length: %u\n", &clen);
	if (clen && !fgets(buffer, sizeof(buffer), fp))
		return -1;

	// Read additional content (we don't need to interpret it)
	while (clen) {
		rlen = sizeof(buffer) > clen ? sizeof(buffer) : clen;
		if (fread(buffer, rlen, 1, fp) < 1)
			return -1;
		clen -= rlen;
	}

	// Interpret request
	for (i=0; i<NUM_PROTCALL; i++)
		if (!strcmp(msg, prot_request[i]))
			return i;
	return -1;
}


static
void* ctrl_socket_fn(void* data)
{
	(void)data;
	int ret = 0;
	int fd, req;
	FILE* ctrl_in;
	struct sockaddr_in caddr;
	socklen_t clilen = sizeof(caddr);


	// Accept the first connection
	fd = sock_accept(listenfd, (struct sockaddr *) &caddr, &clilen);
	if (fd == -1)
		return NULL;

	ctrl_in = fdopen(fd, "rb");
	
	while (!ret) {
		// Process incoming message
		req = read_ctrl_msg(ctrl_in);

		if (req == TIA_VERSION)
			ret = reply_msg(fd, "OK", 0, NULL);
		else if (req == TIA_METAINFO)
			ret = reply_metainfo(fd);
		else if (req == TIA_DATACONNECTION)
			ret = reply_dataconnection(fd);
		else if (req == TIA_STARTDATA)
			ret = reply_startdata(fd);
		else if (req == TIA_STOPDATA)
			ret = reply_stopdata(fd);
		else if (req == -1)
			break;
		
	}

	// Control connection and listening socket
	fclose(ctrl_in); //closing the stream closes the socket as well
	destroy_dataloop();

	return NULL;
}


LOCAL_FN
int create_tia_server(unsigned short port)
{
	sock_init_network_system();
	
	// Create a socket
	listenfd = create_listening_socket(port);
	if (listenfd == -1)
		return -1;

	pthread_create(&ctrl_thid, NULL, ctrl_socket_fn, NULL);
	return 0;
}


LOCAL_FN
void destroy_tia_server(void)
{
	if (listenfd != -1) {
		sock_shutdown(listenfd, SHUT_RD); 
		pthread_join(ctrl_thid, NULL);
		close(listenfd);
	}
	sock_cleanup_network_system();
	unlink(METATMPFILE);
}
