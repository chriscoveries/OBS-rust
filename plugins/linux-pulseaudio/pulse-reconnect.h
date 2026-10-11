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

#include <stdbool.h>
#include <stdint.h>

#include <pulse/introspect.h>

/* Decisions for recovering from a lost server connection, kept free of
 * mainloop and context calls so they can be tested on their own. */

#define PULSE_RECONNECT_MAX_DELAY_USEC (10 * PA_USEC_PER_SEC)

struct pulse_reconnect_state {
	/* Only a connection that worked once is retried; if there never was a
	 * server, fail as before. */
	bool was_ready;
	bool reconnecting;
	uint32_t attempts;
};

/**
 * The context failed. Returns true if it should be replaced after
 * *delay_usec.
 */
bool pulse_reconnect_on_failed(struct pulse_reconnect_state *state, uint64_t *delay_usec);

/**
 * The context is ready. Returns true if it replaced a lost one, so sources
 * must recreate their streams.
 */
bool pulse_reconnect_on_ready(struct pulse_reconnect_state *state);

/**
 * The default device to record, allocated with bmalloc, or NULL if the
 * server has none (e.g. right after it restarted).
 */
char *pulse_default_device(const pa_server_info *info, bool input);

/* After the server comes back its devices reappear over the next moments;
 * sources retry starting their stream until theirs is there. */
#define PULSE_RESTART_RETRY_USEC (100 * 1000)
#define PULSE_RESTART_MAX_ATTEMPTS 100

/**
 * Count a failed stream restart. Returns true if it should be retried after
 * PULSE_RESTART_RETRY_USEC.
 */
bool pulse_restart_should_retry(uint32_t *attempts);
