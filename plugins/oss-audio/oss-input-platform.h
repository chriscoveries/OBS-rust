/*
Copyright (C) 2020. Ka Ho Ng <khng300@gmail.com>
Copyright (C) 2020. Ed Maste <emaste@freebsd.org>

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
*/

/* Device enumeration for the OSS input, per OS: oss-input-freebsd.c (FreeBSD
 * and DragonFly, from /dev/sndstat) or oss-input-linux.c (none), picked by
 * CMake. */
#pragma once

#include <obs-module.h>

/* Adds the system's capture devices to the device list property. */
void oss_platform_add_devices(obs_property_t *p);

/* Parses a FreeBSD sndstat file at `sndstat_path` into `p`. Defined only in
 * oss-input-freebsd.c (and compiled into its test on Linux). */
void oss_sndstat_add_devices(obs_property_t *p, const char *sndstat_path);
