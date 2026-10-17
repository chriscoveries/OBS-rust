/* Test-only oracle: the original libobs/graphics/shader-param-names.c,
 * unmodified, with its exports renamed to oracle_* so it can link next to the
 * Rust implementation. astrcmpi/astrcmp_n are the C dstr.c, already renamed
 * in oracle/dstr.c, so the oracle stays all C. */
#define get_shader_param_type oracle_get_shader_param_type
#define get_sample_filter oracle_get_sample_filter
#define get_address_mode oracle_get_address_mode
#define astrcmpi oracle_astrcmpi
#define astrcmp_n oracle_astrcmp_n

#include "graphics/shader-param-names.c.h"
