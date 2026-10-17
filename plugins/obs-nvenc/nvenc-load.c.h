#include "build/obs-nvenc.pch.h"

void *nvenc_lib = NULL;

bool load_nvenc_lib(void)
{
#if defined(_M_ARM64)
	nvenc_lib = os_dlopen("nvEncodeAPI.dll");
#elif defined(_WIN32)
	nvenc_lib = os_dlopen("nvEncodeAPI64.dll");
#else
	nvenc_lib = os_dlopen("libnvidia-encode.so.1");
#endif
	return nvenc_lib != NULL;
}
