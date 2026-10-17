/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Variadic half of util/base.c, compiled only when ENABLE_RUST_LIBOBS=ON.
 *
 * Stable Rust cannot define `blog` or `bcrash` (c_variadic is unstable) and
 * cannot name a C `va_list`. The handler pointers and the crash-once /
 * re-entry flags live in the Rust core. This file unpacks arguments, runs
 * the default handlers, and calls back into that core.
 */

#include "build/libobs-util.pch.h"

/* printf-style checking at the callers of the va_list forwarders below
 * (obs-rust/obs-studio#516). */
#if defined(__MINGW32__)
#define VPRINTFATTR(f) __attribute__((__format__(__gnu_printf__, f, 0)))
#elif !defined(_MSC_VER)
#define VPRINTFATTR(f) __attribute__((__format__(__printf__, f, 0)))
#else
#define VPRINTFATTR(f)
#endif

#if !defined(_MSC_VER) && !defined(_WIN32)
/* Windows (MinGW included) has no ELF visibility. */
#define BASE_HIDDEN __attribute__((visibility("hidden")))
#else
#define BASE_HIDDEN
#endif

typedef void (*crash_handler_t)(const char *, va_list, void *);

extern void base_get_log_handler(log_handler_t *handler, void **param);
extern int base_begin_crash(crash_handler_t *handler, void **param, int *use_default);

VPRINTFATTR(2)
static void def_log_handler(int log_level, const char *format, va_list args, void *param);

static void def_log_handler(int log_level, const char *format, va_list args, void *param)
{
	char out[8192];
	vsnprintf(out, sizeof(out), format, args);

	switch (log_level) {
	case LOG_DEBUG:
		fprintf(stdout, "debug: %s\n", out);
		fflush(stdout);
		break;

	case LOG_INFO:
		fprintf(stdout, "info: %s\n", out);
		fflush(stdout);
		break;

	case LOG_WARNING:
		fprintf(stdout, "warning: %s\n", out);
		fflush(stdout);
		break;

	case LOG_ERROR:
		fprintf(stderr, "error: %s\n", out);
		fflush(stderr);
	}

	UNUSED_PARAMETER(param);
}

VPRINTFATTR(1)
OBS_NORETURN static void def_crash_handler(const char *format, va_list args, void *param);

OBS_NORETURN static void def_crash_handler(const char *format, va_list args, void *param)
{
	vfprintf(stderr, format, args);
	exit(0);

	UNUSED_PARAMETER(param);
}

/* Called only from the Rust core (obs-util), so no header declares them. */
BASE_HIDDEN log_handler_t base_default_log_handler(void);
BASE_HIDDEN void base_log_literal(log_handler_t handler, void *param, int level, const char *msg);

BASE_HIDDEN log_handler_t base_default_log_handler(void)
{
	return def_log_handler;
}

static void call_handler(log_handler_t handler, void *param, int level, const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	handler(level, msg, args, param);
	va_end(args);
}

/* `handler` is the pointer from the Rust core, already resolved to the
 * default handler when the user has not installed one. `msg` has no
 * conversions; the empty va_list exists so the user's handler can vsnprintf. */
BASE_HIDDEN void base_log_literal(log_handler_t handler, void *param, int level, const char *msg)
{
	call_handler(handler, param, level, msg);
}

void blogva(int log_level, const char *format, va_list args)
{
	log_handler_t handler = NULL;
	void *param = NULL;

	base_get_log_handler(&handler, &param);
	handler(log_level, format, args, param);
}

void blog(int log_level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	blogva(log_level, format, args);
	va_end(args);
}

OBS_NORETURN void bcrash(const char *format, ...)
{
	va_list args;
	crash_handler_t handler = NULL;
	void *param = NULL;
	int use_default = 0;
	crash_handler_t fn;

	if (base_begin_crash(&handler, &param, &use_default)) {
		fputs("Crashed in the crash handler", stderr);
		exit(2);
	}

	fn = use_default ? def_crash_handler : handler;

	va_start(args, format);
	fn(format, args, param);
	va_end(args);
	exit(0);
}

/* File-local macros (BUILD.md): out of the next file in the unity group. */
#undef VPRINTFATTR
#undef BASE_HIDDEN
