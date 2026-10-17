/* Test-only oracle: the original libobs/util/threading-posix.c, unmodified,
 * with every global symbol renamed to oracle_* so it can link next to the
 * Rust implementation. bzalloc/bstrdup_n/bfree come from test_bmem.c. Unix
 * only: the Windows counterpart is threading-windows.c.
 *
 * -std=c11 hides the X/Open pthread_mutexattr_settype and
 * PTHREAD_MUTEX_RECURSIVE that util/threading.h uses; ask for them. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define os_event_init oracle_os_event_init
#define os_event_destroy oracle_os_event_destroy
#define os_event_wait oracle_os_event_wait
#define os_event_timedwait oracle_os_event_timedwait
#define os_event_try oracle_os_event_try
#define os_event_signal oracle_os_event_signal
#define os_event_reset oracle_os_event_reset
#define os_sem_init oracle_os_sem_init
#define os_sem_destroy oracle_os_sem_destroy
#define os_sem_post oracle_os_sem_post
#define os_sem_wait oracle_os_sem_wait
#define os_set_thread_name oracle_os_set_thread_name

#include "util/threading-posix.c.h"

/* Test-only exports called from Rust over FFI. */
int oracle_os_sem_trywait(os_sem_t *sem);

/* Parity-test probe only, not part of util/threading.h: a non-blocking
 * take that exposes a wrong credit count through return codes alone
 * (e.g. a post that adds two credits). */
int oracle_os_sem_trywait(os_sem_t *sem)
{
#ifdef __APPLE__
	mach_timespec_t ts = {0, 0};
	return semaphore_timedwait(sem->sem, ts) == KERN_SUCCESS ? 0 : -1;
#else
	return sem_trywait(&sem->sem);
#endif
}
