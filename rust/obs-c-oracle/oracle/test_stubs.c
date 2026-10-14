/* Test-only stand-ins for libobs symbols (base.c, platform.c) that the
 * oracles and the Rust bmem shim reference under cargo test. Delete together
 * with the oracles once the C sources are deleted.
 *
 * Deliberately includes no libobs header: they mark these functions
 * EXPORT/dllexport. Prototypes below match libobs/util/base.h and
 * libobs/util/platform.h. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void *bmalloc(size_t size);
void bfree(void *ptr);

void os_breakpoint(void);
void os_oom(void);
size_t os_fread_utf8(FILE *file, char **pstr);

/* Unix cargo tests also link the Rust os_breakpoint / os_oom shims.
 * The weak stubs lose that link. Windows links the platform-windows
 * shims from obs-util. A strong stub here would collide with them.
 * MinGW drops a weak-only object from a static archive (#291); this
 * file also defines os_fread_utf8, so the object is still pulled in.
 * MSVC has no weak symbols: /alternatename is the fallback used only
 * when the Rust shim is absent (graphics tests that never reference it). */
#if defined(_MSC_VER)
void os_breakpoint_stub(void) {}
void os_oom_stub(void) {}
#pragma comment(linker, "/alternatename:os_breakpoint=os_breakpoint_stub")
#pragma comment(linker, "/alternatename:os_oom=os_oom_stub")
#elif defined(_WIN32)
__attribute__((weak)) void os_breakpoint(void) {}
__attribute__((weak)) void os_oom(void) {}
#else
__attribute__((weak)) void os_breakpoint(void) {}
__attribute__((weak)) void os_oom(void) {}
#endif

/* os_fopen comes from oracle/file_serializer_host.c. */

/* The Rust shim defines os_gettime_ns, including the macOS Cocoa port.
 * This stub stays weak so the shim wins. The profiler oracle's scripted
 * clock is oracle_profiler_gettime_ns and does not use this symbol. */
#if defined(__APPLE__)
uint64_t os_gettime_ns(void);

__attribute__((weak)) uint64_t os_gettime_ns(void)
{
	return 0;
}
#elif defined(_MSC_VER)
uint64_t os_gettime_ns_stub(void)
{
	return 0;
}
#pragma comment(linker, "/alternatename:os_gettime_ns=os_gettime_ns_stub")
#elif defined(_WIN32)
uint64_t os_gettime_ns(void);

__attribute__((weak)) uint64_t os_gettime_ns(void)
{
	return 0;
}
#endif

/* Mirrors platform.c os_fread_utf8, including returning 0 for the length. */
size_t os_fread_utf8(FILE *file, char **pstr)
{
	size_t size = 0;
	size_t len = 0;

	*pstr = NULL;

	fseek(file, 0, SEEK_END);
	size = (size_t)ftell(file);

	if (size > 0) {
		char bom[3] = {0, 0, 0};
		char *utf8str;
		long offset;

		fseek(file, 0, SEEK_SET);
		size_t size_read = fread(bom, 1, 3, file);
		(void)size_read;

		offset = (memcmp(bom, "\xEF\xBB\xBF", 3) == 0) ? 3 : 0;

		size -= (size_t)offset;
		if (size == 0)
			return 0;

		utf8str = bmalloc(size + 1);
		fseek(file, offset, SEEK_SET);

		size = fread(utf8str, 1, size, file);
		if (size == 0) {
			bfree(utf8str);
			return 0;
		}

		utf8str[size] = 0;

		*pstr = utf8str;
	}

	return len;
}
