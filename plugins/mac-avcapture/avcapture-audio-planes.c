/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "avcapture-audio-planes.h"

#include <media-io/media-io-defs.h>

/* obs_source_audio::data[] and OBSAVCaptureAudioFrame::data both carry a
 * fixed table of MAX_AV_PLANES (media-io-defs.h) plane pointers. */
size_t avcapture_audio_plane_count(size_t buffer_list_count)
{
	return buffer_list_count < MAX_AV_PLANES ? buffer_list_count : MAX_AV_PLANES;
}
