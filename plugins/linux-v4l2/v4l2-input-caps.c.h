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

int v4l2_open_input_caps(const struct v4l2_input_caps_ops *ops, const char *device, int input, uint32_t *caps)
{
	int dev = ops->open(device);
	if (dev == -1)
		return -1;

	if (ops->get_input_caps(dev, input, caps) < 0) {
		ops->close(dev);
		return -1;
	}

	*caps &= V4L2_IN_CAP_STD | V4L2_IN_CAP_DV_TIMINGS;
	return dev;
}
