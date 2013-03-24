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
#ifndef GUSBAMP_API_H
#define GUSBAMP_API_H

#define GT_NOS_AUTOSET                  -1
#define GT_BIPOLAR_DERIVATION_NONE      -2
#define GT_FILTER_AUTOSET       -1
#define GT_FILTER_NONE          -2
#define GT_USBAMP_NUM_DIGITAL_OUT       4
#define GT_USBAMP_NUM_REFERENCE         4
#define GT_USBAMP_NUM_GROUND            4
#define GT_USBAMP_NUM_ANALOG_IN         16
#define GT_USBAMP_RECOMMENDED_BUFFER_SIZE       (32768 * 100)
#define GT_TRUE         1
#define GT_FALSE        0

typedef unsigned int gt_bool;
typedef unsigned long int gt_size;

typedef struct filter_specification {
	float f_upper;
	float f_lower;
	float sample_rate;
	float order;
	float type;
	gt_size id;
} gt_filter_specification;

enum usbamp_special_commands {
	GT_GET_BANDPASS_COUNT,
	GT_GET_NOTCH_COUNT,
	GT_GET_BANDPASS_FILTER,
	GT_GET_NOTCH_FILTER,
	GT_GET_IMPEDANCE,
	GT_GET_CHANNEL_CALIBRATION,
	GT_SET_CHANNEL_CALIBRATION
};

enum usbamp_device_mode {
	GT_MODE_NORMAL,
	GT_MODE_IMPEDANCE,
	GT_MODE_CALIBRATE,
	GT_MODE_COUNTER
};

enum usbamp_analog_out_shape {
	GT_ANALOGOUT_SQUARE,
	GT_ANALOGOUT_SAWTOOTH,
	GT_ANALOGOUT_SINE,
	GT_ANALOGOUT_DRL,
	GT_ANALOGOUT_NOISE
};

typedef enum usbamp_device_mode gt_usbamp_device_mode;
typedef enum usbamp_analog_out_shape gt_usbamp_analog_out_shape;

typedef struct usbamp_channel_calibration {
	float scale[GT_USBAMP_NUM_ANALOG_IN];
	float offset[GT_USBAMP_NUM_ANALOG_IN];
} gt_usbamp_channel_calibration;

typedef struct usbamp_analog_out_config {
	gt_usbamp_analog_out_shape shape;
	short int offset;
	short int frequency;
	short int amplitude;
} gt_usbamp_analog_out_config;

typedef struct usbamp_config {
	unsigned short int sample_rate;
	int number_of_scans;
	gt_bool enable_trigger_line;
	gt_bool slave_mode;
	gt_bool enable_sc;
	gt_bool common_ground[GT_USBAMP_NUM_GROUND];
	gt_bool common_reference[GT_USBAMP_NUM_REFERENCE];
	gt_usbamp_device_mode mode;
	gt_bool scan_dio;
	float version;
	int bandpass[GT_USBAMP_NUM_ANALOG_IN];
	int notch[GT_USBAMP_NUM_ANALOG_IN];
	int bipolar[GT_USBAMP_NUM_ANALOG_IN];
	unsigned char analog_in_channel[GT_USBAMP_NUM_ANALOG_IN];
	gt_size num_analog_in;
	gt_usbamp_analog_out_config *ao_config;
} gt_usbamp_config;

typedef struct usbamp_asynchron_config {
	gt_bool digital_out[GT_USBAMP_NUM_DIGITAL_OUT];
} gt_usbamp_asynchron_config;

#endif
