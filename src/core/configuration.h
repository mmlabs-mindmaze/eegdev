/*
    Copyright (C) 2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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
#ifndef CONFIGURATION_H
#define CONFIGURATION_H

struct setting {
	unsigned int c_offset;
	unsigned int v_offset;
};

struct egdi_config {
	int numsettings, nmaxsettings;
	struct setting* settings;
	char* buffer;
	size_t cursize, maxsize;
};


void egdi_free_config(struct egdi_config* cf);
void egdi_init_config(struct egdi_config* cf);
void egdi_reinit_config(struct egdi_config* cf);
int egdi_add_setting(struct egdi_config* cf,
                     const char* name, const char* val);
const char* egdi_get_setting_value(struct egdi_config* cf,
                                   const char* name);

int egdi_parse_conffile(const char* filename, struct egdi_config* cf);
int egdi_parse_confline(const char* confstr, struct egdi_config* cf);



#endif //CONFIGURATION_H
