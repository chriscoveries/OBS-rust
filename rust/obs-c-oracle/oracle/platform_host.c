/* Test-only oracle: what the oracle needs that it cannot compile from the real
 * libobs source. Three kinds of symbol live here, and each one is tagged with its
 * libobs source file in the tag form the drift check on #145 looks for, so a
 * copy cannot silently fall behind the original.
 *
 * The names are oracle_* where the Rust port owns the plain names in test
 * binaries (see libobs/cmake/rust-exports.d/obs-util-platform_nix.txt): an
 * oracle that calls the plain os_breakpoint / os_oom would run the port under
 * test instead of the original C, which is the #145 failure mode.
 *
 * Deliberately includes no libobs header: platform.h marks these EXPORT. */
#include <stdio.h>
#include <stdlib.h>

/* The two entry points libobs/util/bmem.c calls on the 0-size and
 * failed-allocation paths (bmem.c:104, :111, :125, :132).
 *
 * Intentionally different from the original: libobs raises SIGTRAP
 * (libobs/util/platform-nix.c:793 and :798,
 * libobs/util/platform-windows.c:1192 and :1197 for the Windows half). A SIGTRAP
 * inside cargo test aborts the harness and confuses debuggers, and an empty body
 * - what the deleted oracle/test_stubs.c had - lets a parity test pass against
 * an oracle libobs would have trapped in. So these abort loudly instead: a test
 * that reaches either path dies visibly rather than silently agreeing with a
 * difference. rust/obs-util/tests/platform_oracle.rs checks that with a fork.
 */
/* platform.c */
void oracle_os_breakpoint(void);

/* stderr is block-buffered when it is a file, and abort() does not flush stdio,
 * so the marker is flushed explicitly before the abort. */
void oracle_os_breakpoint(void)
{
	fprintf(stderr, "oracle: os_breakpoint: libobs would raise SIGTRAP "
			"(libobs/util/platform-nix.c:793); aborting\n");
	fflush(stderr);
	abort();
}

/* platform.c */
void oracle_os_oom(void)
{
	fprintf(stderr, "oracle: os_oom: libobs would raise SIGTRAP "
			"(libobs/util/platform-nix.c:798); aborting\n");
	fflush(stderr);
	abort();
}

/* The clock, which the Rust port does not define everywhere.
 *
 * Intentionally different from the original, and only defined where the Rust
 * shim is absent: the shim defines os_gettime_ns on non-Apple Unix, but macOS
 * cargo tests do not link platform-cocoa.m and Windows does not build that shim,
 * so the profiler and audio-io code in the Rust crates would not link. The value
 * is deliberately 0 - no test reads it (the oracle's own clock is the scripted
 * oracle_profiler_gettime_ns, and oracle/platform_nix_posix.c keeps the real
 * renamed original for anything that does need one).
 */
/* platform-cocoa.m, platform-windows.c */
#if defined(__APPLE__) || defined(_WIN32)
uint64_t os_gettime_ns(void)
{
	return 0;
}
#endif

/* The video info the platform.c formatter calls.
 *
 * Intentionally different from the original: the real one lives in
 * libobs/util/obs-video.c, which pulls in the whole video pipeline that the
 * oracle does not build, and the only platform.c function that calls it is
 * os_generate_formatted_filename (libobs/util/platform.c:699), to fill in
 * file-name specifiers. No oracle test calls that one, so this stub only keeps
 * the platform.c translation unit linkable; it leaves the caller's struct
 * untouched rather than guessing at its size.
 */
/* obs-video.c */
struct obs_video_info;

void oracle_obs_get_video_info(struct obs_video_info *ovi);

void oracle_obs_get_video_info(struct obs_video_info *ovi)
{
	(void)ovi;
}
