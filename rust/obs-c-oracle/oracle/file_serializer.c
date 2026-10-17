/* Test-only oracle: the original libobs/util/file-serializer.c, unmodified,
 * with every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. os_fopen and the bmem helpers it calls come from
 * file_serializer_host.c and test_bmem.c. */
#define _FILE_OFFSET_BITS 64
#define file_input_serializer_init oracle_file_input_serializer_init
#define file_input_serializer_free oracle_file_input_serializer_free
#define file_output_serializer_init oracle_file_output_serializer_init
#define file_output_serializer_init_safe oracle_file_output_serializer_init_safe
#define file_output_serializer_free oracle_file_output_serializer_free
/* os_safe_replace is the Rust platform shim on Unix and Windows. This
 * oracle keeps the C copy under a private name. */
#define os_safe_replace oracle_host_os_safe_replace

#include "util/file-serializer.c.h"
