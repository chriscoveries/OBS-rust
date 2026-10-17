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

/* No-op version: Cross-process shared textures for GPU encoding (Windows: keyed-mutex D3D11 textures). */

#include "../shared-texture.h"

bool obs_host_init_encode_copy_surface(struct obs_core_video_mix *video, uint32_t width, uint32_t height, size_t i)
{
	UNUSED_PARAMETER(video);
	UNUSED_PARAMETER(width);
	UNUSED_PARAMETER(height);
	UNUSED_PARAMETER(i);
	return true;
}

void obs_host_gpu_encode_surfaces(struct obs_core_video_mix *video, gs_stagesurf_t *const **copy_surfaces,
				  size_t *channel_count)
{
	UNUSED_PARAMETER(video);
	UNUSED_PARAMETER(copy_surfaces);
	UNUSED_PARAMETER(channel_count);
}

bool obs_host_nv12_planes_share_texture(void)
{
	return false;
}

void obs_host_tex_frame_acquire(struct obs_tex_frame *tf)
{
	UNUSED_PARAMETER(tf);
}

void obs_host_tex_frame_release(struct obs_tex_frame *tf)
{
	UNUSED_PARAMETER(tf);
}

uint32_t obs_host_texture_shared_handle(gs_texture_t *tex)
{
	UNUSED_PARAMETER(tex);
	return (uint32_t)-1;
}
