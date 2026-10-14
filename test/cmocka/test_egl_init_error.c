/* EGL initialization diagnostics, obs-rust/obs-studio#633. No EGL driver required. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>
#include <glad/glad_egl.h>
#include "gl-egl-errors.h"

static char message[1024];
static int error_reads;

EGLint eglGetError(void)
{
	error_reads++;
	return EGL_NOT_INITIALIZED;
}

static void capture_log(int level, const char *format, va_list args, void *param)
{
	(void)param;
	assert_int_equal(level, LOG_ERROR);
	vsnprintf(message, sizeof(message), format, args);
}

static void reports_error_and_environment(void **state)
{
	(void)state;
	setenv("EGL_PLATFORM", "wayland", 1);
	setenv("WAYLAND_DISPLAY", "wayland-1", 1);
	gl_egl_log_init_error("eglGetPlatformDisplay");
	assert_non_null(strstr(message, "eglGetPlatformDisplay failed"));
	assert_non_null(strstr(message, "0x3001"));
	assert_non_null(strstr(message, "EGL_PLATFORM=wayland"));
	assert_non_null(strstr(message, "WAYLAND_DISPLAY=wayland-1"));
	assert_int_equal(error_reads, 1);
}

static void reports_unset_environment(void **state)
{
	(void)state;
	unsetenv("EGL_PLATFORM");
	unsetenv("WAYLAND_DISPLAY");
	gl_egl_log_init_error("eglInitialize");
	assert_non_null(strstr(message, "eglInitialize failed"));
	assert_non_null(strstr(message, "EGL_PLATFORM=(unset)"));
	assert_non_null(strstr(message, "WAYLAND_DISPLAY=(unset)"));
	assert_int_equal(error_reads, 2);
}

int main(void)
{
	base_set_log_handler(capture_log, NULL);
	const struct CMUnitTest tests[] = {cmocka_unit_test(reports_error_and_environment),
					   cmocka_unit_test(reports_unset_environment)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
