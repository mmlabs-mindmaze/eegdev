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
#ifndef ACQUISITION_H
#define ACQUISITION_H

#ifdef __cplusplus
extern "C" {
#endif

#define ACQ_FS		0
#define ACQ_NEEG	1
#define ACQ_NSENS	2
#define ACQ_NTRI	3

struct acq;

typedef void (*acqcb)(void* data, size_t ns,
                      float *eeg, float* sens, int32_t* trigg);

struct acq* acq_init(const char* devstring, acqcb cb, void* cbdata);
int acq_get_info(struct acq* acq, int type);
int acq_prepare_rec(struct acq* acq, const char* filename);
int acq_start(struct acq* acq);
int acq_stop(struct acq* acq);
void acq_close(struct acq* acq);

#ifdef __cplusplus
}
#endif


#endif /* ACQUISITION_H */
