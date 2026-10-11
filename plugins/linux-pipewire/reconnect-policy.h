/* reconnect-policy.h
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pipewire/stream.h>

/* Decisions for recovering from a lost PipeWire connection, kept free of
 * PipeWire and portal calls so they can be tested on their own. */

/* Whether a core error means the connection to the daemon is lost. Returns
 * true at most once per connection: *disconnected latches. */
bool obs_pipewire_core_error_is_lost(bool *disconnected, uint32_t id, int res);

/* Whether a stream state change means the connection is lost. *streamed
 * records that the stream ever streamed; only a stream that fails after that
 * counts, since earlier errors would only repeat. */
bool obs_pipewire_stream_state_is_lost(bool *disconnected, bool *streamed, enum pw_stream_state state);

#define OBS_PIPEWIRE_RECONNECT_MAX_ATTEMPTS 10
#define OBS_PIPEWIRE_RECONNECT_MAX_DELAY_MS 10000

/* Count a failed screencast reconnection attempt. Returns false once the
 * attempts are used up; otherwise sets *delay_ms to the wait before the next
 * one. */
bool obs_pipewire_reconnect_next_delay(uint32_t *attempts, uint32_t *delay_ms);
