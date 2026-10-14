/* MinGW ships this header as windows.h. audio-io.c includes <Windows.h>.
 * On a case-sensitive host those names are different files, so the
 * Ubuntu windows-gnu link fails. This directory is searched first, and
 * include_next skips it. system_header keeps -Wpedantic off that extension. */
#pragma GCC system_header
#include_next <windows.h>
