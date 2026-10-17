#pragma once

/* obs-nvenc-test initialises CUDA and opens an encoder session on every GPU,
 * about a third of a second at each start, for an answer that only changes
 * with the driver, the GPUs or the test program. A successful answer is kept
 * in the module's config directory: the cache key on the first line, then the
 * test's output. These helpers are kept free of CUDA and libobs state so they
 * can be tested on their own. */

/* The test output kept in file_data for key, as a bmalloc'd string, or NULL
 * when file_data holds another key or no output. */
char *nvenc_caps_cache_lookup(const char *file_data, const char *key);

/* The cache file contents for key and caps, as a bmalloc'd string. */
char *nvenc_caps_cache_format(const char *key, const char *caps);

/* What the test's answer depends on, read below sys_root ("/" on a real
 * system): the driver version, the NVIDIA GPUs and the test program at
 * test_exe. A bmalloc'd string, or NULL when it cannot be told (then the test
 * runs). Defined per OS in nvenc-caps-cache-<os>.c. */
char *nvenc_caps_cache_key(const char *sys_root, const char *test_exe);
