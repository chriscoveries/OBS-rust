/* Test-only oracle: the original libobs/obs-av1.c, unmodified, with its
 * global symbols renamed to oracle_* so it can link next to the Rust
 * implementation. */
#define obs_av1_keyframe oracle_obs_av1_keyframe
#define obs_extract_av1_headers oracle_obs_extract_av1_headers
#define metadata_obu_itu_t35 oracle_metadata_obu_itu_t35
#define metadata_obu oracle_metadata_obu

#include "obs-av1.c.h"
