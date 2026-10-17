#include "build/obs-webrtc.pch.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-webrtc", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "OBS WebRTC module";
}

bool obs_module_load()
{
	register_whip_output();
	register_whip_service();

	return true;
}
