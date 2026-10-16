/* Test-only oracle: the original libobs/util/file-serializer.c, unmodified,
 * with every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. os_* helpers come from oracle/platform.c and the
 * platform-specific oracle wrappers; bmem comes from oracle/bmem.c. */
#define _FILE_OFFSET_BITS 64
#define file_input_serializer_init oracle_file_input_serializer_init
#define file_input_serializer_free oracle_file_input_serializer_free
#define file_output_serializer_init oracle_file_output_serializer_init
#define file_output_serializer_init_safe oracle_file_output_serializer_init_safe
#define file_output_serializer_free oracle_file_output_serializer_free
/* Platform helpers are compiled from the real util/platform.c. */
#define os_fopen oracle_os_fopen
#define os_fseeki64 oracle_os_fseeki64
#define os_ftelli64 oracle_os_ftelli64
#define os_safe_replace oracle_os_safe_replace

#include "util/file-serializer.c.h"
