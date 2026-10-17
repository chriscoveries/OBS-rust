/*
 * Variadic half of util/pipe.c, compiled only when ENABLE_RUST_LIBOBS=ON.
 *
 * Stable Rust cannot define `os_process_args_add_argf` (c_variadic is
 * unstable), so this file unpacks the arguments with `vsnprintf` and stores
 * the result through `os_process_args_add_arg`, exactly like
 * util/base-variadic.c does for `blog`.
 */

#include "build/libobs-util.pch.h"

void os_process_args_add_argf(struct os_process_args *args, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int needed = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	if (needed < 0)
		return;

	char *buf = bmalloc((size_t)needed + 1);

	va_start(ap, format);
	vsnprintf(buf, (size_t)needed + 1, format, ap);
	va_end(ap);

	os_process_args_add_arg(args, buf);
	bfree(buf);
}
