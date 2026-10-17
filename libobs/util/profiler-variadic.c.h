/*
 * Variadic half of util/profiler.c, compiled only when ENABLE_RUST_LIBOBS=ON.
 *
 * Stable Rust cannot define profile_store_name. This file formats the name
 * the way profiler.c did and hands the buffer to the Rust name store.
 */

#include "build/libobs-util.pch.h"

extern const char *profiler_name_store_push(profiler_name_store_t *store, char *owned);

const char *profile_store_name(profiler_name_store_t *store, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	struct dstr str = {0};
	dstr_vprintf(&str, format, args);

	va_end(args);

	return profiler_name_store_push(store, str.array);
}
