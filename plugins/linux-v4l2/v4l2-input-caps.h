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

#include <stdint.h>

/* Opening a device to read the capabilities of one of its inputs, kept free
 * of libv4l2 calls so it can be tested on its own.
 *
 * libv4l2 tracks at most 16 open devices per process. A device that is left
 * open each time the properties are refreshed soon fills that table, and
 * every later open fails with "error attempting to open more then 16 video
 * devices" (obsproject/obs-studio#5422). */

struct v4l2_input_caps_ops {
	/* Returns the open device, or -1. */
	int (*open)(const char *device);
	int (*close)(int dev);
	/* Returns < 0 if the input cannot be queried. */
	int (*get_input_caps)(int dev, int input, uint32_t *caps);
};

/**
 * Open @device and read the video standard and DV timings capabilities of
 * @input into *caps.
 *
 * Returns the open device, which the caller closes, or -1 with no device
 * left open.
 */
int v4l2_open_input_caps(const struct v4l2_input_caps_ops *ops, const char *device, int input, uint32_t *caps);
