// The runtime half of a lazy-loading shim (obs-rust/obs-studio#959). Implib.so
// generates the trampolines (every imported symbol jumps through a table that
// the first call fills); these are its --dlopen-callback and --dlsym-callback.
//
// build.py compiles this once per optional library with:
//   HOSTLIB_SPEC_HEADER  spec-<name>.h: the hostlib_spec from host-libs.json
//   HOSTLIB_DECLS_HEADER the library's own headers (its declarations)
//   HOSTLIB_STUBS_HEADER HOSTLIB_STUB(sym) for every symbol the bundle imports
//
// When the host has the library, its symbols are returned. When it has none,
// every symbol resolves to a stub with the function's exact type (decltype of
// the real declaration), returning the library's "unimplemented" value, a null
// pointer, or nothing: callers see an ordinary failure, and OBS keeps running
// with the feature off.
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

#include "hostlib.h"
#include HOSTLIB_SPEC_HEADER
#include HOSTLIB_DECLS_HEADER

namespace {

// The value a stub returns, by return type. Integers are status codes in the
// libraries this is used for (VAStatus: VA_STATUS_ERROR_UNIMPLEMENTED = 0x14);
// HOSTLIB_STUB_STATUS overrides it per library.
#ifndef HOSTLIB_STUB_STATUS
#define HOSTLIB_STUB_STATUS 0x14
#endif
template<class R> R unavailable()
{
	if constexpr (std::is_void_v<R>)
		return;
	else if constexpr (std::is_pointer_v<R>)
		return nullptr;
	else if constexpr (std::is_integral_v<R> || std::is_enum_v<R>)
		return static_cast<R>(HOSTLIB_STUB_STATUS);
	else
		return R{};
}

template<class F> struct stub_of;
template<class R, class... A> struct stub_of<R(A...)> {
	static R fn(A...) { return unavailable<R>(); }
};
template<class R, class... A> struct stub_of<R(A..., ...)> {
	static R fn(A..., ...) { return unavailable<R>(); }
};

struct stub_entry {
	const char *name;
	void *fn;
};

// The sentinel handle that means "use the stubs".
char stub_handle_storage;
void *const STUB_HANDLE = &stub_handle_storage;

pthread_once_t once = PTHREAD_ONCE_INIT;
hostlib_result result;

void find()
{
	hostlib_find(&HOSTLIB_SPEC, &result);
	char line[2048];
	hostlib_describe(&HOSTLIB_SPEC, &result, line, sizeof(line));
	// Once per process, to stderr (the OBS log captures it via AppRun).
	fprintf(stderr, "%s\n", line);
	const char *debug = getenv("OBS_HOSTLIB_DEBUG");
	if (debug && *debug && strcmp(debug, "0") != 0)
		hostlib_trace(&HOSTLIB_SPEC, &result);
}

} // namespace

#define HOSTLIB_STUB(sym) {#sym, reinterpret_cast<void *>(&stub_of<decltype(sym)>::fn)},
static const stub_entry stubs[] = {
#include HOSTLIB_STUBS_HEADER
	{nullptr, nullptr},
};

extern "C" {

__attribute__((visibility("hidden"))) void *hostlib_dlopen_cb(const char *)
{
	pthread_once(&once, find);
	return result.handle ? result.handle : STUB_HANDLE;
}

__attribute__((visibility("hidden"))) void *hostlib_dlsym_cb(void *handle, const char *name)
{
	if (handle != STUB_HANDLE) {
		void *p = dlsym(handle, name);
		if (p)
			return p;
		// The host's library lacks a symbol the bundle imports (older than the
		// build's): that one call fails like the library were absent.
	}
	for (const stub_entry *s = stubs; s->name; s++)
		if (strcmp(s->name, name) == 0)
			return s->fn;
	fprintf(stderr, "hostlib: %s: no stub for %s\n", HOSTLIB_SPEC.name, name);
	abort(); // build.py generates a stub for every imported symbol
}

// For OBS and the status light (#846): 1 when the host's library is in use.
__attribute__((visibility("default"))) int HOSTLIB_STATUS_FN(void)
{
	pthread_once(&once, find);
	return result.handle != nullptr;
}

} // extern "C"
