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
#ifndef BIOSEMI_H
#define BIOSEMI_H

#ifdef ACT2_SUPPORT
LOCAL_FN struct eegdev* open_biosemi(const struct opendev_options* opt);
#else  //!ACT2_SUPPORT
#define open_biosemi	NULL
#endif //ACT2_SUPPORT

#endif // BIOSEMI_H
