#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

#include <util/bmem.h>
#include <util/c99defs.h>

#include "nvenc-caps-cache.h"

/* obs-nvenc ran obs-nvenc-test (CUDA initialisation and an encoder session on
 * every GPU, ~0.3 s) at every start. Its answer is kept while the driver, the
 * GPUs and the test program stay the same. */

static const char *caps = "[general]\nnvenc_supported=true\ndriver_ver=595.99.02\n\n[h264]\ncodec_supported=1\n";

static void saved_caps_are_read_back_for_their_key(void **state)
{
	UNUSED_PARAMETER(state);
	char *file = nvenc_caps_cache_format("driver=595.99.02", caps);
	assert_non_null(file);
	char *read = nvenc_caps_cache_lookup(file, "driver=595.99.02");
	assert_non_null(read);
	assert_string_equal(read, caps);
	bfree(read);
	bfree(file);
}

static void another_key_misses(void **state)
{
	UNUSED_PARAMETER(state);
	char *file = nvenc_caps_cache_format("driver=595.99.02", caps);
	assert_null(nvenc_caps_cache_lookup(file, "driver=600.10"));
	/* A key that only starts the saved one is another key. */
	assert_null(nvenc_caps_cache_lookup(file, "driver=595.99"));
	assert_null(nvenc_caps_cache_lookup(file, ""));
	bfree(file);
}

static void empty_or_broken_files_miss(void **state)
{
	UNUSED_PARAMETER(state);
	assert_null(nvenc_caps_cache_lookup(NULL, "k"));
	assert_null(nvenc_caps_cache_lookup("", "k"));
	assert_null(nvenc_caps_cache_lookup("k", "k"));
	assert_null(nvenc_caps_cache_lookup("k\n", "k"));
	assert_null(nvenc_caps_cache_lookup("k\n", NULL));
}

/* A fake sysfs below a temporary directory. */
static char root[256];

static void put(const char *rel, const char *text)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	for (char *p = path + strlen(root) + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0755);
			*p = '/';
		}
	}
	FILE *f = fopen(path, "w");
	assert_non_null(f);
	fputs(text, f);
	fclose(f);
}

static void pci(const char *slot, const char *vendor, const char *class_id, const char *device)
{
	char rel[256];
	snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/vendor", slot);
	put(rel, vendor);
	snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/class", slot);
	put(rel, class_id);
	snprintf(rel, sizeof(rel), "sys/bus/pci/devices/%s/device", slot);
	put(rel, device);
}

static int make_root(void **state)
{
	UNUSED_PARAMETER(state);
	snprintf(root, sizeof(root), "/tmp/nvenc-caps-cache-XXXXXX");
	assert_non_null(mkdtemp(root));
	put("sys/module/nvidia/version", "595.99.02\n");
	pci("0000:07:00.0", "0x10de\n", "0x030000\n", "0x1e04\n");
	pci("0000:06:00.0", "0x10de\n", "0x030000\n", "0x2504\n");
	pci("0000:06:00.1", "0x10de\n", "0x040300\n", "0x228e\n"); /* its HDMI audio */
	pci("0000:0a:00.0", "0x1002\n", "0x030000\n", "0x73bf\n"); /* not NVIDIA */
	put("obs-nvenc-test", "test program v1");
	return 0;
}

static int remove_root(void **state)
{
	UNUSED_PARAMETER(state);
	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	return system(cmd);
}

static char *key_now(void)
{
	char exe[300];
	snprintf(exe, sizeof(exe), "%s/obs-nvenc-test", root);
	return nvenc_caps_cache_key(root, exe);
}

static void key_names_driver_and_nvidia_gpus(void **state)
{
	UNUSED_PARAMETER(state);
	char *key = key_now();
	assert_non_null(key);
	assert_non_null(strstr(key, "595.99.02"));
	char *a = strstr(key, "0000:06:00.0=0x2504");
	char *b = strstr(key, "0000:07:00.0=0x1e04");
	assert_non_null(a);
	assert_non_null(b);
	assert_true(a < b); /* in bus order, whatever readdir says */
	assert_null(strstr(key, "0000:06:00.1"));
	assert_null(strstr(key, "0000:0a:00.0"));
	assert_null(strchr(key, '\n'));
	bfree(key);
}

static void key_changes_with_driver_gpus_and_test(void **state)
{
	UNUSED_PARAMETER(state);
	char *before = key_now();

	put("sys/module/nvidia/version", "600.10\n");
	char *driver = key_now();
	assert_string_not_equal(before, driver);
	put("sys/module/nvidia/version", "595.99.02\n");

	pci("0000:08:00.0", "0x10de\n", "0x030200\n", "0x2684\n");
	char *gpus = key_now();
	assert_string_not_equal(before, gpus);

	put("obs-nvenc-test", "test program v2, a different build");
	char *test = key_now();
	assert_string_not_equal(gpus, test);

	bfree(before);
	bfree(driver);
	bfree(gpus);
	bfree(test);
}

static void no_nvidia_driver_no_key(void **state)
{
	UNUSED_PARAMETER(state);
	char path[512];
	snprintf(path, sizeof(path), "%s/sys/module/nvidia/version", root);
	unlink(path);
	assert_null(key_now());
	put("sys/module/nvidia/version", "595.99.02\n");
	assert_null(nvenc_caps_cache_key(root, "/nonexistent/obs-nvenc-test"));
	assert_null(nvenc_caps_cache_key(root, NULL));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(saved_caps_are_read_back_for_their_key),
		cmocka_unit_test(another_key_misses),
		cmocka_unit_test(empty_or_broken_files_miss),
		cmocka_unit_test_setup_teardown(key_names_driver_and_nvidia_gpus, make_root, remove_root),
		cmocka_unit_test_setup_teardown(key_changes_with_driver_gpus_and_test, make_root, remove_root),
		cmocka_unit_test_setup_teardown(no_nvidia_driver_no_key, make_root, remove_root),
	};

	int failed = cmocka_run_group_tests(tests, NULL, NULL);
	if (bnum_allocs() != 0)
		return 1;
	return failed;
}
