/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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
#ifndef FAKEACT2_H
#define FAKEACT2_H

#define PERIOD	113

static inline
int32_t get_analog_val(size_t sam, unsigned int ich, int stype)
{
	(void)stype;
	return 256*((ich+1)*((sam % PERIOD) - 50));
}


static inline
float get_analog_valf(size_t sam, unsigned int ich, int stype)
{
	return (1.0f/8192.0f)*get_analog_val(sam, ich, stype);
}

static inline
double get_analog_vald(size_t sam, unsigned int ich, int stype)
{
	return (1.0/8192.0)*get_analog_val(sam, ich, stype);
}

static inline
int32_t compute_trigger(size_t sam, int32_t stateval)
{
	int32_t trval;
	
	trval = 256*(sam % PERIOD);
	trval |= stateval; 

	return trval;
}


static inline
int32_t get_trigger_val(size_t sam, int32_t stateval)
{
	return (compute_trigger(sam, stateval << 8) >> 8) & 0x00FFFFFF;
}


#endif

