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
#include <linux/videodev2.h>

/* The frames the virtual camera writes, decided from the format the device
 * accepted, kept free of ioctls so it can be tested on its own.
 *
 * VIDIOC_S_FMT does not have to take the size it is asked for: it returns
 * the format the device will actually use. A v4l2loopback device whose
 * format was fixed with `v4l2loopback-ctl set-caps`, or that a reader
 * already holds open, keeps its own size whatever OBS asks for
 * (obsproject/obs-studio#12081). */

struct vcam_format {
	uint32_t width;
	uint32_t height;
	uint32_t frame_size;
};

/* requested_width/height: the output size OBS asked the device for.
 * negotiated: the format VIDIOC_S_FMT handed back.
 * Returns false when the device cannot take the YUYV frames OBS writes. */
bool vcam_format_negotiate(uint32_t requested_width, uint32_t requested_height,
			   const struct v4l2_pix_format *negotiated, struct vcam_format *out);
