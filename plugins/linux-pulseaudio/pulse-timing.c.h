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

#include "build/linux-pulseaudio-nix.pch.h"

#define NSEC_PER_SEC 1000000000ULL

/* A packet this late against the timeline means audio was really lost
 * (e.g. the server stalled), not just delivered late: start over. */
#define PULSE_TIMING_RESYNC_NS NSEC_PER_SEC

/* How often the timeline catches up with the least wait seen, so a device
 * clock slower than the system clock does not leave it ever further behind. */
#define PULSE_TIMING_WINDOW_NS NSEC_PER_SEC

static void start_window(struct pulse_timing *timing, uint64_t now_ns)
{
	timing->min_wait_ns = UINT64_MAX;
	timing->window_start_ns = now_ns;
}

uint64_t pulse_timing_packet_ts(struct pulse_timing *timing, uint64_t now_ns, size_t frames, uint32_t rate)
{
	uint64_t duration = util_mul_div64(frames, NSEC_PER_SEC, rate);
	/* The latest the packet can have started: it ended by now. */
	uint64_t latest_ts = now_ns - duration;
	uint64_t ts;

	/* Packets follow on from each other in samples, however the server
	 * batches them, so count them on from the previous one. Restart from
	 * the clock when that would place the packet after it arrived (the
	 * device clock is fast, or the timeline started on a late packet), or
	 * when the packet is so late that audio must have been lost. */
	if (!timing->next_ts || latest_ts < timing->next_ts || latest_ts - timing->next_ts > PULSE_TIMING_RESYNC_NS) {
		ts = latest_ts;
		start_window(timing, now_ns);
	} else {
		uint64_t wait = latest_ts - timing->next_ts;

		ts = timing->next_ts;
		if (wait < timing->min_wait_ns)
			timing->min_wait_ns = wait;

		/* Every packet of the window waited at least this long, so the
		 * timeline is behind by at least as much. */
		if (now_ns - timing->window_start_ns >= PULSE_TIMING_WINDOW_NS) {
			ts += timing->min_wait_ns;
			start_window(timing, now_ns);
		}
	}

	timing->next_ts = ts + duration;
	return ts;
}

void pulse_timing_reset(struct pulse_timing *timing)
{
	timing->next_ts = 0;
	start_window(timing, 0);
}

/* File-local macros (BUILD.md): out of the next file in the unity group. */
#undef NSEC_PER_SEC
#undef PULSE_TIMING_RESYNC_NS
#undef PULSE_TIMING_WINDOW_NS
