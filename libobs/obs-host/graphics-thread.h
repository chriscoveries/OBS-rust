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

/* How the graphics thread runs (macOS: user-interactive QoS, an autorelease pool per frame). */

#include "defs.h"
#include "../util/threading.h"

struct obs_graphics_context;

/* Starts obs_graphics_thread(param) in `thread`; returns pthread_create's result. */
OBS_HOST_EXTERN int obs_host_create_graphics_thread(pthread_t *thread, void *param);
/* One iteration of obs_graphics_thread_loop. */
OBS_HOST_EXTERN bool obs_host_graphics_thread_loop(struct obs_graphics_context *context);
