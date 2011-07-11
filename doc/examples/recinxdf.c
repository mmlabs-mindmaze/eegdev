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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "acquisition.h"

static size_t nstot;
static unsigned int fs;

static
void data_cb(void* data, size_t ns, float* eeg, float *sens, int32_t *tri)
{
	size_t i, sec;
	(void)data;
	(void)eeg;
	(void)sens;
	(void)tri;

	for (i=nstot; i<nstot+ns; i++) {
		if (i % fs == 0) {
			sec = i/fs;
			printf(sec % 10 ? "." : "|");
			fflush(stdout);
		}
	}
	nstot += ns;
}

int main(int argc, char *argv[])
{
	char str[256] = {'\0'};
	const char* devstring = NULL;
	struct acq* acq;
	int opt;

	// Parse command line options
	while ((opt = getopt(argc, argv, "d:h")) != -1) {
		switch (opt) {
		case 'd':
			devstring = optarg;
			break;
		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-d devstr]\n", argv[0]);
			return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

	// Open the connection to the data acquisition device
	if (!(acq = acq_init(devstring, data_cb, NULL)))
		return EXIT_FAILURE;
	fs = acq_get_info(acq, ACQ_FS);

	for (;;) {
		nstot = 0;
		printf("Enter a filename for recording "
		       "(Ctrl+D to exit):\n");
		if (scanf(" %255s%*1c", str) == EOF)
			break;

		// Create and initialize file for recording
		if (acq_prepare_rec(acq, str))
			continue;
		
		printf("Press ENTER to start recording\n");
		while(getchar() != '\n');
		acq_start(acq);

		printf("Press ENTER to stop recording\n");
		while(getchar() != '\n');
		// Stop file recording and close it
		acq_stop(acq);
	}
	// Close the connection to the data acquisition device
	acq_close(acq);

	return EXIT_SUCCESS;
}

