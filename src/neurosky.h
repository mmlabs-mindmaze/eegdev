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
#ifndef NEUROSKY_H
#define NEUROSKY_H

#ifdef NSKY_SUPPORT
LOCAL_FN struct eegdev* open_neurosky(const struct opendev_options* opt);
#else  //!NSKY_SUPPORT
#define open_neurosky	NULL
#endif //NSKY_SUPPORT



#endif // NEUROSKY_H
