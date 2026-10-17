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

#pragma once

/* Where module binaries are (macOS: bundles, and the obsolete obs-browser to skip). */

#include "defs.h"

struct dstr;

/* True for a module path this host refuses to load. */
OBS_HOST_EXTERN bool obs_host_skip_module(const char *path);
/* Appends the module file extension to a module binary path. */
OBS_HOST_EXTERN void obs_host_add_module_extension(struct dstr *bin_path);
