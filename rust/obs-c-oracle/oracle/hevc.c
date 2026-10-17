/* Test-only oracle: the original libobs/obs-hevc.c, unmodified, with its
 * global symbols renamed to oracle_* so it can link next to the Rust
 * implementation, calling the oracle copies of obs-nal.c and
 * array-serializer.c. */
#define obs_hevc_keyframe oracle_obs_hevc_keyframe
#define obs_parse_hevc_packet oracle_obs_parse_hevc_packet
#define obs_parse_hevc_packet_priority oracle_obs_parse_hevc_packet_priority
#define obs_extract_hevc_headers oracle_obs_extract_hevc_headers
#define obs_nal_find_startcode oracle_obs_nal_find_startcode
#define array_output_serializer_init oracle_array_output_serializer_init

#include "obs-hevc.c.h"
