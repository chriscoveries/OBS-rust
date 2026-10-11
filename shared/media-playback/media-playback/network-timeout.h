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

#pragma once

#include <libavutil/dict.h>

/* A network source that goes silent (a camera that loses power, a link that
 * drops without closing the connection) must make av_read_frame() fail, so
 * the media thread stops and the source reconnects. Without a timeout the
 * read blocks for as long as the connection looks open, which can be
 * forever. */

/**
 * Add the FFmpeg options that bound how long a read from the network input
 * at path may block to *opts. Options the user already set are kept.
 */
void mp_media_set_network_timeouts(AVDictionary **opts, const char *path);
