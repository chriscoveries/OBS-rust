#ifndef JANSSON_PRIVATE_CONFIG_H
#define JANSSON_PRIVATE_CONFIG_H

#define HAVE_STDINT_H 1
#define HAVE_INT32_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT8_T 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SETLOCALE 1

#ifndef _WIN32
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SCHED_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#if defined(__linux__)
#define HAVE_ENDIAN_H 1
#endif
#define HAVE_CLOSE 1
#define HAVE_GETPID 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_OPEN 1
#define HAVE_READ 1
#define HAVE_SCHED_YIELD 1
#define HAVE_SSIZE_T 1
#define USE_URANDOM 1
#define HAVE_ATOMIC_BUILTINS 1
#define HAVE_SYNC_BUILTINS 1
#else
#define USE_WINDOWS_CRYPTOAPI 1
/* MSVC has no ssize_t, and __int64 is an MSVC keyword. This header is
 * the first include (HAVE_CONFIG_H), so the windows-gnu probe has not
 * seen a system header yet and rejects __int64. MinGW's own headers
 * define ssize_t. MSVC keeps the keyword. */
#if defined(_MSC_VER)
#ifndef _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif
#else
#include <sys/types.h>
#endif
#if !defined(_MSC_VER)
#define HAVE_ATOMIC_BUILTINS 1
#define HAVE_SYNC_BUILTINS 1
#endif
#endif

#define INITIAL_HASHTABLE_ORDER 3

#endif
