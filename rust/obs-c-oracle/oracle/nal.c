/* Test-only oracle: the original libobs/obs-nal.c, unmodified, with its
 * global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. */
#define obs_nal_find_startcode oracle_obs_nal_find_startcode

#include "obs-nal.c.h"
