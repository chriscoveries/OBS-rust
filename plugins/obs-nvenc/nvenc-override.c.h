#include "build/obs-nvenc-override.pch.h"

bool nvenc_lookahead_enabled(bool preset_enables, bool explicit_setting, bool setting_on)
{
	if (explicit_setting)
		return setting_on;
	return setting_on || preset_enables;
}

bool nvenc_lookahead_force_off(bool explicit_setting, bool setting_on)
{
	return explicit_setting && !setting_on;
}

int nvenc_bframes_for_uhq(bool uhq, int bframes)
{
	if (uhq && bframes < 4)
		return 4;
	return bframes;
}
