/* Finding the host's copy of a library the AppImage does not bundle
 * (obs-rust/obs-studio#959). Shared by the lazy-loading shims (optional
 * libraries) and obs-hostlib-check (required ones).
 *
 * A candidate is accepted when it loads (dlopen RTLD_LAZY|RTLD_LOCAL: a 32-bit
 * or foreign-arch file fails here and the search goes on), is not inside the
 * AppImage, and resolves every symbol in `symbols`. The search order is
 *   1. $OBS_HOSTLIB_<NAME> (a path; NAME upper-cased, '-' -> '_')
 *   2. the soname alone (the ldconfig cache and the default paths)
 *   3. /run/opengl-driver/lib (NixOS), /usr/lib/x86_64-linux-gnu, /usr/lib64,
 *      /usr/lib, /lib64, /lib
 * for each soname in order. Every rejected candidate is kept with its reason,
 * for the log and for OBS_HOSTLIB_DEBUG=1. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOSTLIB_MAX_TRIES 32

struct hostlib_spec {
	const char *name;           /* "va" */
	const char *feature;        /* "VA-API hardware encoding and decoding" */
	const char *const *sonames; /* NULL-terminated */
	const char *const *symbols; /* NULL-terminated */
};

struct hostlib_try {
	char path[512];
	char reason[256]; /* empty: accepted */
};

struct hostlib_result {
	void *handle;        /* NULL: not found */
	char path[512];      /* what was loaded */
	size_t ntries;
	struct hostlib_try tries[HOSTLIB_MAX_TRIES];
};

/* Search for spec; fills *out. Returns out->handle. Thread-compatible (the
 * shims call it under pthread_once). */
void *hostlib_find(const struct hostlib_spec *spec, struct hostlib_result *out);

/* One line for the log: "hostlib: va -> /usr/lib64/libva.so.2" or
 * "hostlib: va unavailable (VA-API ...): searched N candidates: path (reason); ..." */
void hostlib_describe(const struct hostlib_spec *spec, const struct hostlib_result *r, char *buf, size_t size);

/* The full trace, one candidate per line, to stderr. */
void hostlib_trace(const struct hostlib_spec *spec, const struct hostlib_result *r);

#ifdef __cplusplus
}
#endif
