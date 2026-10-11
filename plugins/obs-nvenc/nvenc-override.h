#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Preset enableLookahead applies only when the settings did not set
 * "lookahead". An explicit value, including the Enhanced Broadcasting
 * override, wins. */
bool nvenc_lookahead_enabled(bool preset_enables, bool explicit_setting, bool setting_on);

/* Explicit lookahead off must clear the preset's enableLookahead flag and
 * lookaheadDepth. Those fields are copied from the preset before the
 * override is applied. */
bool nvenc_lookahead_force_off(bool explicit_setting, bool setting_on);

/* UHQ encodes at least 4 B-frames. Other tunes keep `bframes`. */
int nvenc_bframes_for_uhq(bool uhq, int bframes);

#ifdef __cplusplus
}
#endif
