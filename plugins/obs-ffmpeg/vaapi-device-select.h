/*
Copyright (C) 2026 by Ryan Doupe

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
#include <stddef.h>

/* Choosing the device a VAAPI encoder opens, kept free of libva and FFmpeg
 * calls so it can be tested on its own. */

/* Returns true if the device at path can be opened and encodes the codec. */
typedef bool (*vaapi_device_usable_cb)(const char *path, void *param);

/**
 * The device to open for the vaapi_device setting configured.
 *
 * A profile can name a device that is gone or unusable here: one copied
 * from another machine, or left over from a GPU or driver change. Then the
 * first usable candidate is used instead. If nothing is usable, configured
 * is returned so the encoder fails as it did before.
 */
const char *vaapi_select_device(const char *configured, const char *const *candidates, size_t num_candidates,
				vaapi_device_usable_cb usable, void *param);
