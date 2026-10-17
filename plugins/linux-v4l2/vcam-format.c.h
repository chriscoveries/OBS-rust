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

#include "build/linux-v4l2-nix.pch.h"

bool vcam_format_negotiate(uint32_t requested_width, uint32_t requested_height,
			   const struct v4l2_pix_format *negotiated, struct vcam_format *out)
{
	(void)requested_width;
	(void)requested_height;

	if (negotiated->pixelformat != V4L2_PIX_FMT_YUYV)
		return false;

	/* OBS scales its output to the size the device kept and writes packed
	 * YUY2 frames of that size. */
	out->width = negotiated->width;
	out->height = negotiated->height;
	out->frame_size = negotiated->width * negotiated->height * 2;
	return true;
}
