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

#pragma once

#include <stdbool.h>
#include <util/threading.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Upper bound for waiting on the ScreenCaptureKit stopCapture completion
 * handler before the capture object may be torn down. */
#define SCK_STOP_TIMEOUT_MS 2000

enum sck_stop_wait_result {
	SCK_STOP_WAIT_COMPLETED,
	SCK_STOP_WAIT_TIMEOUT,
	SCK_STOP_WAIT_ERROR,
};

/* Waits up to timeout_ms for the stop completion handler to signal the
 * event. NULL or a broken event maps to SCK_STOP_WAIT_ERROR so callers
 * can fall back to the previous fire-and-forget path. */
enum sck_stop_wait_result sck_wait_for_stop(os_event_t *stop_completed, unsigned int timeout_ms);

/* Whether the capture object may be destroyed after the wait result. On
 * timeout the completion block can still fire later and touch the freed
 * memory, so the caller must leak instead. */
bool sck_stop_may_free(enum sck_stop_wait_result result);

#ifdef __cplusplus
}
#endif
