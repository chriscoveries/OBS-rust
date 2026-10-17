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

#include "build/obs-ffmpeg-vaapi-device-select.pch.h"

static inline bool is_set(const char *path)
{
	return path && *path;
}

const char *vaapi_select_device(const char *configured, const char *const *candidates, size_t num_candidates,
				vaapi_device_usable_cb usable, void *param)
{
	if (is_set(configured) && usable(configured, param))
		return configured;

	for (size_t i = 0; i < num_candidates; i++) {
		const char *candidate = candidates[i];

		if (!is_set(candidate))
			continue;
		if (is_set(configured) && strcmp(candidate, configured) == 0)
			continue;
		if (usable(candidate, param))
			return candidate;
	}

	return configured;
}
