/*
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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <obs-properties.h>
#include <util/dstr.h>

/* Framerate decisions of the v4l2 source, kept free of device ioctls so they
 * can be tested on their own. Framerates are packed (numerator, denominator)
 * tuples of the device's time per frame, see v4l2_pack_tuple(). */

/* Add one discrete frame interval to the framerate list. */
void v4l2_framerate_list_add_discrete(obs_property_t *prop, struct dstr *buffer, uint32_t numerator,
				      uint32_t denominator);

/* Whether the requested framerate setting differs from the one the device is
 * running at, so the stream has to be restarted. -1 means "leave unchanged";
 * *unchanged remembers that it was already applied once. */
bool v4l2_framerate_changed(int64_t current, int64_t requested, bool *unchanged);
