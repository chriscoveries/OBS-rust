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

/* Same plane table size as MAX_AV_PLANES in libobs/audio-monitoring: the
 * obs_source_audio data[] and OBSAVCaptureAudioFrame::data tables both
 * carry exactly this many plane pointers. */
#define AVCAPTURE_MAX_AUDIO_PLANES 8

size_t avcapture_audio_plane_count(size_t buffer_list_count)
{
	return buffer_list_count < AVCAPTURE_MAX_AUDIO_PLANES ? buffer_list_count : AVCAPTURE_MAX_AUDIO_PLANES;
}
