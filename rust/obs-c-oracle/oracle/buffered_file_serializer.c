/* Test-only oracle: the original libobs/util/buffered-file-serializer.c,
 * unmodified, with every global symbol renamed to oracle_*. The file-serializer
 * entry points it calls are renamed to the copies oracle/platform.c compiles
 * from the real platform.c. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define _FILE_OFFSET_BITS 64

#define buffered_file_serializer_init_defaults oracle_buffered_file_serializer_init_defaults
#define buffered_file_serializer_init oracle_buffered_file_serializer_init
#define buffered_file_serializer_free oracle_buffered_file_serializer_free
/* the file-serializer entry points it calls, from oracle/platform.c */
#define os_fopen oracle_os_fopen
#define os_fseeki64 oracle_os_fseeki64

#include "util/buffered-file-serializer.c.h"
