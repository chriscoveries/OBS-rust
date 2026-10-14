/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <util/pipe.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

pid_t __real_waitpid(pid_t pid, int *status, int options);
pid_t __wrap_waitpid(pid_t pid, int *status, int options);

static bool interrupt_wait;
static unsigned interrupted;
static unsigned no_child;

pid_t __wrap_waitpid(pid_t pid, int *status, int options)
{
	/* A failed wait provides no status. Seed the slot so an invalid read
	 * deterministically looks like success, rather than arbitrary stack data. */
	*status = 0;
	if (interrupt_wait) {
		interrupt_wait = false;
		interrupted++;
		errno = EINTR;
		return -1;
	}
	pid_t ret = __real_waitpid(pid, status, options);
	if (ret == -1 && errno == ECHILD)
		no_child++;
	return ret;
}

static bool check_exit(const char *name, int expected)
{
	os_process_pipe_t *pp = os_process_pipe_create("exit 23", "r");
	if (!pp)
		return false;
	uint8_t byte;
	os_process_pipe_read(pp, &byte, 1);
	int ret = os_process_pipe_destroy(pp);
	fprintf(stderr, "%s: expected %d, got %d\n", name, expected, ret);
	return ret == expected;
}

int main(void)
{
	struct sigaction action = {.sa_handler = SIG_DFL}, old;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGCHLD, &action, &old) != 0)
		return 2;

	bool normal = check_exit("normal exit", 23);
	interrupt_wait = true;
	bool retry = check_exit("EINTR retry", 23);

	/* The kernel, not the wrapper, produces ECHILD for this child. */
	action.sa_handler = SIG_IGN;
	bool configured = sigaction(SIGCHLD, &action, NULL) == 0;
	bool missing = configured && check_exit("auto-reaped child", -1);
	bool restored = sigaction(SIGCHLD, &old, NULL) == 0;
	return normal && retry && missing && restored && interrupted == 1 && no_child == 1 ? 0 : 1;
}
