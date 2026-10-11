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

#include <stddef.h>
#include <stdint.h>

/* Timestamps for the packets of a record stream, kept free of mainloop and
 * stream calls so they can be tested on their own.
 *
 * The server does not always hand packets over as they are captured: a
 * graph quantum longer than the fragment, a capture thread held up by load,
 * or a second stream on the same connection to pipewire-pulse all deliver
 * packets in bursts. libobs stops smoothing a source whose timestamps stray
 * more than 70 ms from where its audio should continue, and the audio
 * stutters (obsproject/obs-studio#12835, #10808). */

struct pulse_timing {
	/* Where the next packet starts if the stream stays contiguous; 0 until
	 * the first packet. */
	uint64_t next_ts;
	/* Least time any packet of the current window waited after its end
	 * on the timeline, and when the window began. */
	uint64_t min_wait_ns;
	uint64_t window_start_ns;
};

/**
 * Timestamp of the first frame of a packet of @frames frames at @rate Hz
 * that was read at @now_ns (os_gettime_ns).
 */
uint64_t pulse_timing_packet_ts(struct pulse_timing *timing, uint64_t now_ns, size_t frames, uint32_t rate);

/** Forget the stream's timeline, e.g. when it is stopped. */
void pulse_timing_reset(struct pulse_timing *timing);
