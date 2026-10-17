/* Test-only oracle: the original libobs/util/profiler.c, unmodified, with
 * its exported names prefixed by oracle_. Print and CSV live in
 * profiler-io.c (they stay C) and are not part of this translation unit.
 *
 * The scripted clock is oracle_profiler_gettime_ns. The Rust shim owns
 * os_gettime_ns, and the parity harness feeds the Rust side separately.
 * oracle_profiler_reset frees the roots
 * through the real profiler_free and re-inits the mutex that free destroys,
 * so cases do not share state. */

/* PTHREAD_MUTEX_RECURSIVE (util/threading.h) is behind _GNU_SOURCE with
 * -std=c11. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>

static const uint64_t *oracle_profiler_clock;
static size_t oracle_profiler_clock_len;
static size_t oracle_profiler_clock_pos;

#define profiler_start oracle_profiler_start
#define profiler_stop oracle_profiler_stop
#define profile_reenable_thread oracle_profile_reenable_thread
#define profile_register_root oracle_profile_register_root
#define profile_start oracle_profile_start
#define profile_end oracle_profile_end
#define profiler_free oracle_profiler_free
#define profiler_name_store_create oracle_profiler_name_store_create
#define profiler_name_store_free oracle_profiler_name_store_free
#define profile_store_name oracle_profile_store_name
#define profile_snapshot_create oracle_profile_snapshot_create
/* dstr_vprintf stays in dstr-libc.c and is renamed in the oracle. */
#define dstr_vprintf oracle_dstr_vprintf
/* Calls inside profiler.c use the scripted clock, not the Rust shim. */
#define os_gettime_ns oracle_profiler_gettime_ns

#include "util/profiler.c.h"

/* Test-only exports called from Rust over FFI. */
void oracle_profiler_set_clock(const uint64_t *times, size_t len);
void oracle_profiler_reset(void);

void oracle_profiler_set_clock(const uint64_t *times, size_t len)
{
	oracle_profiler_clock = times;
	oracle_profiler_clock_len = len;
	oracle_profiler_clock_pos = 0;
}

EXPORT uint64_t os_gettime_ns(void)
{
	if (oracle_profiler_clock_pos < oracle_profiler_clock_len)
		return oracle_profiler_clock[oracle_profiler_clock_pos++];
	return 0;
}

void oracle_profiler_reset(void)
{
	oracle_profiler_free();
	pthread_mutex_init(&root_mutex, NULL);
	enabled = false;
	thread_context = NULL;
	thread_enabled = true;
}
