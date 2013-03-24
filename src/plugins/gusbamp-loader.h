/*
    Copyright (C) 2013 Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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
#ifndef GUSBAMP_LOADER_H
#define GUSBAMP_LOADER_H

#include "gusbamp-types.h"

#ifdef __cplusplus
extern "C" {
#endif

int load_gusbamp_module(void);
int unload_gusbamp_module(void);

// gUSBAmp API function pointers
LOCAL_FN void (*GT_ShowDebugInformation) (gt_bool);
LOCAL_FN gt_bool(*GT_UpdateDevices) ();
LOCAL_FN gt_size(*GT_GetDeviceListSize) ();
LOCAL_FN char **(*GT_GetDeviceList) ();
LOCAL_FN gt_bool(*GT_FreeDeviceList) (char **, gt_size);
LOCAL_FN gt_bool(*GT_OpenDevice) (const char *);
LOCAL_FN gt_bool(*GT_CloseDevice) (const char *);
LOCAL_FN gt_bool(*GT_SetConfiguration) (const char *, void *);
LOCAL_FN gt_bool(*GT_GetConfiguration) (const char *, void *);
LOCAL_FN gt_bool(*GT_SetAsynchronConfiguration) (const char *, void *);
LOCAL_FN gt_bool(*GT_ApplyAsynchronConfiguration) (const char *);
LOCAL_FN gt_bool(*GT_GetAsynchronConfiguration) (const char *, void *);
LOCAL_FN gt_bool(*GT_StartAcquisition) (const char *);
LOCAL_FN gt_bool(*GT_StopAcquisition) (const char *);
LOCAL_FN int (*GT_GetSamplesAvailable) (const char *);
LOCAL_FN int (*GT_GetData) (const char *, unsigned char *, gt_size);
LOCAL_FN gt_bool(*GT_SetDataReadyCallBack) (const char *, void (*)(void *),
                                                void *);
LOCAL_FN gt_size(*GT_GetBandpassFilterListSize) (const char *, gt_size);
LOCAL_FN gt_bool(*GT_GetBandpassFilterList) (const char *, gt_size,
                                                 gt_filter_specification *,
                                                 gt_size);
LOCAL_FN gt_size(*GT_GetNotchFilterListSize) (const char *, gt_size);
LOCAL_FN gt_bool(*GT_GetNotchFilterList) (const char *, gt_size,
                                              gt_filter_specification *,
                                              gt_size);
LOCAL_FN gt_bool(*GT_GetChannelCalibration) (const char *,
                                           gt_usbamp_channel_calibration *);
LOCAL_FN gt_bool(*GT_SetChannelCalibration) (const char *,
                                            gt_usbamp_channel_calibration*);
LOCAL_FN gt_bool(*GT_Calibrate) (const char *,
                                     gt_usbamp_channel_calibration *);
LOCAL_FN gt_bool(*GT_GetImpedance) (const char *, gt_size, int *);

#ifdef __cplusplus
}
#endif

#endif
