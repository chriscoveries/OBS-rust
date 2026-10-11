#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <vaapi-device-select.h>

/* A machine whose usable devices are listed; every probe is counted. */
struct machine {
	const char *const *usable;
	size_t num_usable;
	int probes;
};

static bool machine_usable(const char *path, void *param)
{
	struct machine *m = param;

	m->probes++;
	for (size_t i = 0; i < m->num_usable; i++) {
		if (strcmp(m->usable[i], path) == 0)
			return true;
	}
	return false;
}

static void expect_device(const char *got, const char *want)
{
	if (got == want)
		return;
	if (got && want && strcmp(got, want) == 0)
		return;

	fprintf(stderr, "expected device '%s', got '%s'\n", want ? want : "(null)", got ? got : "(null)");
	fail();
}

/* One render node that encodes, as on a single-GPU machine. */
static const char *const one_gpu[] = {"/dev/dri/renderD128"};

static void test_working_configured_device_is_kept(void **state)
{
	(void)state;

	static const char *const two_gpus[] = {"/dev/dri/renderD128", "/dev/dri/renderD129"};
	struct machine m = {two_gpus, 2, 0};
	const char *configured = "/dev/dri/renderD129";

	expect_device(vaapi_select_device(configured, one_gpu, 1, machine_usable, &m), configured);
}

static void test_missing_render_node_falls_back(void **state)
{
	(void)state;

	/* Saved while a second GPU was installed, or before a driver change
	 * renumbered the nodes. */
	struct machine m = {one_gpu, 1, 0};

	expect_device(vaapi_select_device("/dev/dri/renderD129", one_gpu, 1, machine_usable, &m),
		      "/dev/dri/renderD128");
}

static void test_other_machines_by_path_device_falls_back(void **state)
{
	(void)state;

	/* A profile shared with a machine whose GPU sits on another PCI slot. */
	struct machine m = {one_gpu, 1, 0};

	expect_device(vaapi_select_device("/dev/dri/by-path/pci-0000:00:02.0-render", one_gpu, 1, machine_usable, &m),
		      "/dev/dri/renderD128");
}

static void test_unusable_existing_node_falls_back(void **state)
{
	(void)state;

	/* The primary node exists but cannot be opened by the user (or has no
	 * encoder); FFmpeg fails on it with EINVAL. */
	struct machine m = {one_gpu, 1, 0};

	expect_device(vaapi_select_device("/dev/dri/card0", one_gpu, 1, machine_usable, &m), "/dev/dri/renderD128");
}

static void test_unusable_candidates_are_skipped(void **state)
{
	(void)state;

	/* The first node exists but cannot encode the codec. */
	static const char *const candidates[] = {"/dev/dri/renderD128", "/dev/dri/renderD129"};
	static const char *const second_only[] = {"/dev/dri/renderD129"};
	struct machine m = {second_only, 1, 0};

	expect_device(vaapi_select_device("/dev/dri/renderD130", candidates, 2, machine_usable, &m),
		      "/dev/dri/renderD129");
}

static void test_empty_device_uses_working_node(void **state)
{
	(void)state;

	/* FFmpeg treats "" as a display or device name, not as "pick one":
	 * av_hwdevice_ctx_create() fails with EINVAL or EIO. */
	struct machine m = {one_gpu, 1, 0};

	expect_device(vaapi_select_device("", one_gpu, 1, machine_usable, &m), "/dev/dri/renderD128");
}

static void test_nothing_usable_keeps_configured(void **state)
{
	(void)state;

	/* No VAAPI device at all: fail with the configured path, as before. */
	struct machine m = {NULL, 0, 0};
	const char *configured = "/dev/dri/renderD129";

	expect_device(vaapi_select_device(configured, one_gpu, 1, machine_usable, &m), configured);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_working_configured_device_is_kept),
		cmocka_unit_test(test_missing_render_node_falls_back),
		cmocka_unit_test(test_other_machines_by_path_device_falls_back),
		cmocka_unit_test(test_unusable_existing_node_falls_back),
		cmocka_unit_test(test_unusable_candidates_are_skipped),
		cmocka_unit_test(test_empty_device_uses_working_node),
		cmocka_unit_test(test_nothing_usable_keeps_configured),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
