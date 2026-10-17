#include "build/obs-nvenc-caps-cache.pch.h"

/* No cache key on Windows yet: obs-nvenc-test runs at every start, as before. */
char *nvenc_caps_cache_key(const char *sys_root, const char *test_exe)
{
	UNUSED_PARAMETER(sys_root);
	UNUSED_PARAMETER(test_exe);
	return NULL;
}
