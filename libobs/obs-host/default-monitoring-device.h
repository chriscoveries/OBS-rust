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

/* The "default" audio monitoring device (macOS: resolved to the desktop default). */

#include "defs.h"

/* Returns false when `device_id` needs no resolving. Otherwise stores a
 * bmalloc'd device id, or NULL, in `def_id` and returns true. */
OBS_HOST_EXTERN bool obs_host_default_monitoring_device(const char *device_id, char **def_id);
