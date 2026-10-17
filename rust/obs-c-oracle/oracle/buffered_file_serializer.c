/* Test-only oracle: the original libobs/util/buffered-file-serializer.c,
 * unmodified, with every global symbol renamed to oracle_*. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define _FILE_OFFSET_BITS 64

#define buffered_file_serializer_init_defaults oracle_buffered_file_serializer_init_defaults
#define buffered_file_serializer_init oracle_buffered_file_serializer_init
#define buffered_file_serializer_free oracle_buffered_file_serializer_free

#include "util/buffered-file-serializer.c.h"
