/******************************************************************************
    Copyright (C) 2026 by the obs-rust contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

/* No-op version: Where module binaries are (macOS: bundles, and the obsolete obs-browser to skip). */

#include "../module-paths.h"
#include "../../util/dstr.h"

extern const char *get_module_extension(void);

bool obs_host_skip_module(const char *path)
{
	UNUSED_PARAMETER(path);
	return false;
}

void obs_host_add_module_extension(struct dstr *bin_path)
{
	dstr_cat(bin_path, get_module_extension());
}
