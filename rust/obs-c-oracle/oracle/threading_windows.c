/* Test-only oracle: the original libobs/util/threading-windows.c, unmodified,
 * with every global symbol renamed to oracle_* so it can link next to the
 * Rust implementation. bfree and os_utf8_to_wcs_ptr come from the oracle's
 * bmem.c and platform.c. Windows only.
 *
 * UNICODE has to be set before the included file pulls in windows.h.
 * Otherwise LoadLibrary(L"KernelBase.dll") is LoadLibraryA on a wide
 * string, the module load fails, and SetThreadDescription is skipped.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#define os_event_init oracle_os_event_init
#define os_utf8_to_wcs_ptr oracle_os_utf8_to_wcs_ptr
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

#include "util/threading-windows.c.h"

/* Parity-test probe only, not part of util/threading.h: a non-blocking
 * take. The handle is the opaque pointer, as in threading-windows.c. */
int oracle_os_sem_trywait(os_sem_t *sem);

int oracle_os_sem_trywait(os_sem_t *sem)
{
	if (!sem)
		return -1;
	return WaitForSingleObject((HANDLE)sem, 0) == WAIT_OBJECT_0 ? 0 : -1;
}
