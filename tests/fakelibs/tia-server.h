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
#ifndef TIA_SERVER_H
#define TIA_SERVER_H

static inline
float get_analog_val(size_t sam, unsigned int ich)
{
	return 1.0e-1*(ich+1)*((float)(sam % 113) - 50.0f);
}

static inline
int32_t get_trigger_val(size_t sam, unsigned int ich)
{
	return (ich+1)*(sam%113);
}


void create_tia_server(unsigned short port);
void destroy_tia_server(void);

#endif // TIA_SERVER_H
