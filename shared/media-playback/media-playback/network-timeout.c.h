/*
 * Copyright (c) 2026 Ryan Doupe
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "build/media-playback.pch.h"

/* Socket I/O timeout of FFmpeg's RTSP demuxer, in microseconds. A read that
 * gets no data for this long fails, which stops the media and makes the
 * source reconnect. */
#define RTSP_IO_TIMEOUT_USEC "5000000"

static bool is_rtsp(const char *path)
{
	return path && (astrcmpi_n(path, "rtsp://", 7) == 0 || astrcmpi_n(path, "rtsps://", 8) == 0);
}

void mp_media_set_network_timeouts(AVDictionary **opts, const char *path)
{
	/* FFmpeg 5.0 renamed the RTSP option "stimeout" to "timeout". Other
	 * protocols give "timeout" other meanings and units, so it is only set
	 * for RTSP. */
	if (is_rtsp(path))
		av_dict_set(opts, "timeout", RTSP_IO_TIMEOUT_USEC, AV_DICT_DONT_OVERWRITE);
}

/* File-local macros (BUILD.md): out of the next file in the unity group. */
#undef RTSP_IO_TIMEOUT_USEC
