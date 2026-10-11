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

#include <errno.h>

#include "mac-sck-stop.h"

enum sck_stop_wait_result sck_wait_for_stop(os_event_t *stop_completed, unsigned int timeout_ms)
{
	if (!stop_completed)
		return SCK_STOP_WAIT_ERROR;

	int ret = os_event_timedwait(stop_completed, timeout_ms);
	if (ret == 0)
		return SCK_STOP_WAIT_COMPLETED;
	if (ret == ETIMEDOUT)
		return SCK_STOP_WAIT_TIMEOUT;
	return SCK_STOP_WAIT_ERROR;
}

bool sck_stop_may_free(enum sck_stop_wait_result result)
{
	/* On timeout the completion block can still fire and touch the
	 * event, so the capture must be leaked rather than freed. */
	return result != SCK_STOP_WAIT_TIMEOUT;
}
