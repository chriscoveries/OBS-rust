/* Test-only oracle: the original libobs/util/task.c, unmodified, with every
 * global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. pthread, the os_event/os_sem/os_set_thread_name helpers
 * and bzalloc come from libobs/util/threading-{posix,windows}.c, the
 * w32-pthreads sources (Windows only) and test_bmem.c.
 *
 * -std=c11 hides the X/Open pthread_mutexattr_settype and
 * PTHREAD_MUTEX_RECURSIVE that util/threading.h uses; ask for them. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define os_task_queue_create oracle_os_task_queue_create
#define os_task_queue_queue_task oracle_os_task_queue_queue_task
#define os_task_queue_destroy oracle_os_task_queue_destroy
#define os_task_queue_wait oracle_os_task_queue_wait
#define os_task_queue_inside oracle_os_task_queue_inside

#include "util/task.c.h"
