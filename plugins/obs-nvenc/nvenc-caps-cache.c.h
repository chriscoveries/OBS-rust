#include "build/obs-nvenc-caps-cache.pch.h"

char *nvenc_caps_cache_lookup(const char *file_data, const char *key)
{
	if (!file_data || !key || !*key)
		return NULL;

	size_t key_len = strlen(key);
	if (strncmp(file_data, key, key_len) != 0 || file_data[key_len] != '\n')
		return NULL;

	const char *caps = file_data + key_len + 1;
	return *caps ? bstrdup(caps) : NULL;
}

char *nvenc_caps_cache_format(const char *key, const char *caps)
{
	size_t key_len = strlen(key);
	size_t caps_len = strlen(caps);
	char *file = bmalloc(key_len + 1 + caps_len + 1);

	memcpy(file, key, key_len);
	file[key_len] = '\n';
	memcpy(file + key_len + 1, caps, caps_len + 1);
	return file;
}
