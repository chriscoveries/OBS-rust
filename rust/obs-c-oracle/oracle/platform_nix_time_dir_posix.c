/* Test-only oracle: the original libobs/util/platform-nix-time-dir.c,
 * unmodified, with every global symbol renamed to oracle_* so it can link
 * next to the Rust implementation. bmalloc/bzalloc/bmemdup/bfree and
 * dstr_* resolve to the Rust obs-util shims in the test binary; blog
 * comes from util/base-variadic.c (compiled unrenamed into this oracle).
 * Unix-only: this file needs <dirent.h>/<glob.h>. */
/* -std=c11 hides POSIX declarations (clock_gettime/usleep/glob) on
 * glibc; libobs builds with the GNU dialect, so ask for them explicitly. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define os_sleepto_ns oracle_os_sleepto_ns
#define os_sleepto_ns_fast oracle_os_sleepto_ns_fast
#define os_sleep_ms oracle_os_sleep_ms
#define os_gettime_ns oracle_os_gettime_ns
#define os_opendir oracle_os_opendir
#define os_readdir oracle_os_readdir
#define os_closedir oracle_os_closedir
#define os_glob oracle_os_glob
#define os_globfree oracle_os_globfree

#include "util/platform-nix-time-dir.c"

#include <stddef.h>

/* Layout probes for the public structs and the file-private handles the
 * Rust port mirrors (see tests/platform_time_dir_layout.rs). */
size_t oracle_platform_time_dir_os_dirent_size(void) { return sizeof(struct os_dirent); }
size_t oracle_platform_time_dir_os_dirent_align(void) { return _Alignof(struct os_dirent); }
size_t oracle_platform_time_dir_os_dirent_offset_d_name(void) { return offsetof(struct os_dirent, d_name); }
size_t oracle_platform_time_dir_os_dirent_offset_directory(void) { return offsetof(struct os_dirent, directory); }

size_t oracle_platform_time_dir_os_globent_size(void) { return sizeof(struct os_globent); }
size_t oracle_platform_time_dir_os_globent_align(void) { return _Alignof(struct os_globent); }
size_t oracle_platform_time_dir_os_globent_offset_path(void) { return offsetof(struct os_globent, path); }
size_t oracle_platform_time_dir_os_globent_offset_directory(void) { return offsetof(struct os_globent, directory); }

size_t oracle_platform_time_dir_os_glob_info_size(void) { return sizeof(struct os_glob_info); }
size_t oracle_platform_time_dir_os_glob_info_align(void) { return _Alignof(struct os_glob_info); }
size_t oracle_platform_time_dir_os_glob_info_offset_gl_pathc(void) { return offsetof(struct os_glob_info, gl_pathc); }
size_t oracle_platform_time_dir_os_glob_info_offset_gl_pathv(void) { return offsetof(struct os_glob_info, gl_pathv); }

size_t oracle_platform_time_dir_posix_glob_info_size(void) { return sizeof(struct posix_glob_info); }
size_t oracle_platform_time_dir_posix_glob_info_offset_gl(void) { return offsetof(struct posix_glob_info, gl); }
