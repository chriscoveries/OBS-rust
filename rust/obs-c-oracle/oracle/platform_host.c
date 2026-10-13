/* Test-only oracle: the libobs symbols the oracle needs that it cannot compile
 * from the real source. Each one is tagged with its libobs source file in the
 * tag form the drift check on #145 looks for, so a copy cannot silently fall
 * behind the original.
 *
 * The names are oracle_* where the Rust port owns the plain names in test
 * binaries (see libobs/cmake/rust-exports.d/obs-util-platform_nix.txt): an
 * oracle that calls the plain os_breakpoint / os_oom would run the port under
 * test instead of the original C, which is the #145 failure mode. On Windows
 * the port does not define them at all, so the plain names are provided below.
 *
 * Deliberately includes no libobs header: platform.h marks these EXPORT. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* The two entry points libobs/util/bmem.c calls on the 0-size and
 * failed-allocation paths (bmem.c:104, :111, :125, :132).
 *
 * Intentionally different from the original: libobs raises SIGTRAP there
 * (libobs/util/platform-nix.c:793 for os_breakpoint and :798 for os_oom,
 * libobs/util/platform-windows.c:1192 and :1197 for the Windows halves). A
 * SIGTRAP inside cargo test aborts the harness and confuses debuggers, and an
 * empty body - what the deleted oracle/test_stubs.c had - lets a parity test pass
 * against an oracle libobs would have trapped in. So they abort loudly instead:
 * any test that reaches either path dies visibly rather than silently agreeing
 * with a difference. rust/obs-util/tests/platform_oracle.rs checks that with a
 * fork.
 *
 * Note that a check matching definition names against libobs cannot see these:
 * the oracle_ prefix keeps them out of the port's symbol namespace.
 */
/* platform-nix.c, platform-windows.c */
void oracle_os_breakpoint(void);
void oracle_os_oom(void);

/* stderr is block-buffered when it is a file, and abort() does not flush stdio,
 * so the marker is flushed explicitly before the abort. */
void oracle_os_breakpoint(void)
{
	fprintf(stderr, "oracle: os_breakpoint: libobs would raise SIGTRAP "
			"(libobs/util/platform-nix.c:793); aborting\n");
	fflush(stderr);
	abort();
}

/* platform-nix.c, platform-windows.c */
void oracle_os_oom(void)
{
	fprintf(stderr, "oracle: os_oom: libobs would raise SIGTRAP "
			"(libobs/util/platform-nix.c:798); aborting\n");
	fflush(stderr);
	abort();
}

/* On Windows the Rust port does not define os_breakpoint or os_oom (its
 * platform_nix module is #[cfg(unix)], rust/obs-util/src/ffi/mod.rs:19), and
 * obs-util's bmem shim imports them on every target
 * (rust/obs-util/src/ffi/bmem.rs:29-30), so the test binaries would not link.
 * These are the plain-named providers for that target only, and they abort the
 * same way the oracle_ copies above do; the ports under test keep raising
 * SIGTRAP on Unix, where this file is not built for the plain names.
 *
 * The original Windows bodies are in libobs/util/platform-windows.c:1192 and
 * :1197, which call __debugbreak; upstream #291 made these definitions strong
 * on every _WIN32 because MinGW discards weak-only definitions, and that
 * intent is carried over here.
 */
#if defined(_WIN32)
/* platform-windows.c */
void os_breakpoint(void);

/* platform-windows.c */
void os_oom(void);

void os_breakpoint(void)
{
	abort();
}

void os_oom(void)
{
	abort();
}
#endif

/* Windows needs the platform functions under their plain names, and the
 * oracle only has them oracle_-renamed: build.rs compiles
 * oracle/platform.c (the real platform.c, every export renamed) on every
 * target, but oracle/platform_nix_posix.c - the file that provides the same
 * names unrenamed - is Unix-only (build.rs), and libobs' own
 * platform-windows.c is not compiled by the oracle at all. So the Rust crates
 * that declare these on Windows (rust/obs-util's bmem shim declares
 * os_breakpoint and os_oom on every target, and its Windows sync module
 * declares os_utf8_to_wcs_ptr) would not link. These forward to the real
 * bodies platform.c compiles, so Windows keeps the same surface the deleted
 * stand-ins provided.
 *
 * Note that `os_sleepto_ns_fast` and `os_set_thread_name` are not here: those
 * come from libobs/util/threading-windows.c through
 * oracle/threading_windows.c, which renames them (as the base did).
 */
#if defined(_WIN32)
/* The definitions platform.c compiles under renamed names. Declared here
 * because this translation unit cannot include platform.c. */
size_t oracle_os_mbs_to_wcs(const char *str, size_t str_len, wchar_t *dst, size_t dst_size);
size_t oracle_os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size);
size_t oracle_os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst, size_t dst_size);
size_t oracle_os_wcs_to_mbs(const wchar_t *str, size_t str_len, char *dst, size_t dst_size);
size_t oracle_os_mbs_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr);
size_t oracle_os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr);
size_t oracle_os_wcs_to_utf8_ptr(const wchar_t *str, size_t len, char **pstr);
size_t oracle_os_mbs_to_utf8_ptr(const char *str, size_t len, char **pstr);
FILE *oracle_os_fopen(const char *path, const char *mode);
int oracle_os_fseeki64(FILE *file, int64_t offset, int origin);
int64_t oracle_os_ftelli64(FILE *file);

/* platform-windows.c (compiled by oracle/platform.c) */
size_t os_mbs_to_wcs(const char *str, size_t str_len, wchar_t *dst, size_t dst_size);
size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size);
size_t os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst, size_t dst_size);
size_t os_wcs_to_mbs(const wchar_t *str, size_t str_len, char *dst, size_t dst_size);
size_t os_mbs_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr);
size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr);
size_t os_wcs_to_utf8_ptr(const wchar_t *str, size_t len, char **pstr);
size_t os_mbs_to_utf8_ptr(const char *str, size_t len, char **pstr);
FILE *os_fopen(const char *path, const char *mode);
int os_fseeki64(FILE *file, int64_t offset, int origin);
int64_t os_ftelli64(FILE *file);

size_t os_mbs_to_wcs(const char *str, size_t str_len, wchar_t *dst, size_t dst_size)
{
	return oracle_os_mbs_to_wcs(str, str_len, dst, dst_size);
}

size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size)
{
	return oracle_os_utf8_to_wcs(str, len, dst, dst_size);
}

size_t os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst, size_t dst_size)
{
	return oracle_os_wcs_to_utf8(str, len, dst, dst_size);
}

size_t os_wcs_to_mbs(const wchar_t *str, size_t str_len, char *dst, size_t dst_size)
{
	return oracle_os_wcs_to_mbs(str, str_len, dst, dst_size);
}

size_t os_mbs_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr)
{
	return oracle_os_mbs_to_wcs_ptr(str, len, pstr);
}

size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr)
{
	return oracle_os_utf8_to_wcs_ptr(str, len, pstr);
}

size_t os_wcs_to_utf8_ptr(const wchar_t *str, size_t len, char **pstr)
{
	return oracle_os_wcs_to_utf8_ptr(str, len, pstr);
}

size_t os_mbs_to_utf8_ptr(const char *str, size_t len, char **pstr)
{
	return oracle_os_mbs_to_utf8_ptr(str, len, pstr);
}

FILE *os_fopen(const char *path, const char *mode)
{
	return oracle_os_fopen(path, mode);
}

int os_fseeki64(FILE *file, int64_t offset, int origin)
{
	return oracle_os_fseeki64(file, offset, origin);
}

int64_t os_ftelli64(FILE *file)
{
	return oracle_os_ftelli64(file);
}
#endif

/* On Windows the oracle needs three symbols that platform-nix.c cannot
 * provide and platform-windows.c cannot either: libobs' Windows bodies use
 * MSVC-only constructs (`__try` in has_obs_export and is_obs_plugin, and
 * IsWow64Process2), so the file does not compile under the mingw-w64 target
 * that the fork's link-check lane uses. oracle/platform.c renames os_mkdir,
 * os_safe_replace and os_gettime_ns on every target, so without these three
 * the Windows link would be short three symbols the Unix build gets from the
 * real platform-nix.c. Nothing on that lane executes them - it links only.
 */
#if defined(_WIN32)
/* platform-windows.c */
int oracle_os_mkdir(const char *path);

/* platform-windows.c */
int oracle_os_safe_replace(const char *target, const char *from, const char *backup);

/* platform-windows.c */
uint64_t oracle_os_gettime_ns(void);

int oracle_os_mkdir(const char *path)
{
	(void)path;
	return -1;
}

int oracle_os_safe_replace(const char *target, const char *from, const char *backup)
{
	(void)target;
	(void)from;
	(void)backup;
	return -1;
}

uint64_t oracle_os_gettime_ns(void)
{
	return 0;
}
#endif

/* The clock, which the Rust port does not define everywhere.
 *
 * Intentionally different from the original, and only defined where the Rust
 * shim is absent: the shim defines os_gettime_ns on non-Apple Unix, but macOS
 * cargo tests do not link libobs/util/platform-cocoa.m and Windows does not
 * build that shim, so the profiler and audio-io code in the Rust crates would
 * not link on those two. The value is deliberately 0 - no test reads it (the
 * oracle's own clock is the scripted oracle_profiler_gettime_ns, and
 * oracle/platform_nix_posix.c keeps the real renamed original for anything that
 * does need one).
 */
/* platform-cocoa.m, platform-windows.c */
#if defined(__APPLE__) || defined(_WIN32)
uint64_t os_gettime_ns(void);

uint64_t os_gettime_ns(void)
{
	return 0;
}
#endif

/* The video info the platform.c formatter calls.
 *
 * Intentionally different from the original: the real one is in libobs/obs.c
 * (obs_get_video_info, libobs/obs.c:1680), which pulls in the whole video
 * pipeline that the oracle does not build, and the only platform.c function
 * that calls it is os_generate_formatted_filename (libobs/util/platform.c:699),
 * to fill in file-name specifiers. No oracle test calls that one, so this stub
 * only keeps the platform.c translation unit linkable; it reports "no video
 * info" rather than guessing at a struct the caller would have to size.
 *
 * The return type matches the declaration in libobs/obs.h:468, which the
 * platform.c translation unit sees through the rename macro; a void definition
 * for an external bool would be a cross-translation-unit type mismatch.
 */
/* obs.c */
struct obs_video_info;

bool oracle_obs_get_video_info(struct obs_video_info *ovi);

bool oracle_obs_get_video_info(struct obs_video_info *ovi)
{
	(void)ovi;
	return false;
}
