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
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "eegdev-procdev.h"
#include "devices.h"

#ifdef PROCDEV_CHILD

int main(int argc, char* argv[])
{
	return run_eegdev_process(open_gtec, argc, argv);
}

#else //PROCDEV_CHILD

LOCAL_FN
struct eegdev* open_gtec(const struct opendev_options* opt)
{
	return open_procdev(opt, "eegdev-proc-gtec");
}

#endif //!PROCDEC_CHILD
