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

/* Cross-process shared textures for GPU encoding (Windows: keyed-mutex D3D11 textures). */

#include "defs.h"
#include "../graphics/graphics.h"

struct obs_core_video_mix;
struct obs_tex_frame;

/* Creates the GPU encoder's NV12/P010 staging surface `i` of `video`;
 * false on failure. */
OBS_HOST_EXTERN bool obs_host_init_encode_copy_surface(struct obs_core_video_mix *video, uint32_t width,
						       uint32_t height, size_t i);
/* The surfaces and channel count GPU-encode frames are staged through. */
OBS_HOST_EXTERN void obs_host_gpu_encode_surfaces(struct obs_core_video_mix *video,
						  gs_stagesurf_t *const **copy_surfaces, size_t *channel_count);
/* True when the Y and UV planes are views of one texture, so one
 * gs_copy_texture copies both. */
OBS_HOST_EXTERN bool obs_host_nv12_planes_share_texture(void);
/* Takes and hands back an encoder frame's texture across processes. */
OBS_HOST_EXTERN void obs_host_tex_frame_acquire(struct obs_tex_frame *tf);
OBS_HOST_EXTERN void obs_host_tex_frame_release(struct obs_tex_frame *tf);
/* The texture's shared handle, or (uint32_t)-1 without one. */
OBS_HOST_EXTERN uint32_t obs_host_texture_shared_handle(gs_texture_t *tex);
