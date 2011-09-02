/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

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
#ifndef DEVICE_HELPER_H
#define DEVICE_HELPER_H 

#include <sys/types.h>
#include "eegdev-common.h"

struct egdich {
	const char* label;
	unsigned int stype, dtype;
};


/* \param fd	file descriptor
 * \param buff	pointer to the buffer that should receive the data
 * \param count number of bytes to be transfer
 *
 * Exactly like the read system call excepting that it can only entirely
 * transfer count bytes or fails, i.e. it attempts to complete the read if
 * less than count bytes are transferred.
 *
 * Returns 0 in case of success or -1 if an error occurred (errno is then 
 * set accordingly) */
LOCAL_FN int egdi_fullread(int fd, void* buff, size_t count);

/* \param fd	file descriptor
 * \param buff	pointer to the buffer containing the data to be written
 * \param count number of bytes to be transfer
 *
 * Exactly like the write system call excepting that it can only entirely
 * transfer count bytes or fails, i.e. it attempts to complete the write if
 * less than count bytes are transferred.
 *
 * Returns 0 in case of success or -1 if an error occurred (errno is then 
 * set accordingly) */
LOCAL_FN int egdi_fullwrite(int fd, const void* buff, size_t count);


/* \param ch	array of channel description (channel map)
 * \param stype  the sensor type that is requested
 * \param tind	the index of the requested channel of the correct type
 * 
 * Returns the index in the ch array of the tind-th channel of type stype
 */
LOCAL_FN int egdi_next_chindex(const struct egdich* ch, 
                               unsigned int stype, int ind);

/* \param ch	array of channel description (channel map)
 * \param ind	the index of the requested channel of the correct type
 * 
 * Returns the offset of the ind-th channel in the backend mapping (the ch
 * array is assumed to describe the mapping of the data provided by the
 * backend)
 */
LOCAL_FN int egdi_in_offset(const struct egdich* ch, int ind);

/* \param dev		pointer to a eegdev struct
 * \param channels	array if the channels description (channel map)
 * \param ngrp		number of channel groups
 * \param grp		array of channel groups
 *
 * Take the channels description and calculate and allocated the required
 * number of selected_channels structures, and fills them accordingly.
 * This function is to be used by backend in their set_channel_groups
 * method.
 *
 * Returns the number of selected_channels structures in case of success, or
 * -1 in case of failure.
 *
 * NOTE: If successful, egd_alloc_input_groups will be called by this
 * function
 */
LOCAL_FN int egdi_split_alloc_chgroups(struct eegdev* dev,
                              const struct egdich* channels,
                              unsigned int ngrp, const struct grpconf* grp);


#endif //DEVICE_HELPER_H

