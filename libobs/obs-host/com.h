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

/* Per-process COM apartment (Windows). */

#include "defs.h"

/* Before obs_init. */
OBS_HOST_EXTERN void obs_host_startup(void);
/* After a startup that failed before the core existed, and at the end of
 * obs_shutdown: undoes obs_host_startup, at most once. */
OBS_HOST_EXTERN void obs_host_shutdown(void);
