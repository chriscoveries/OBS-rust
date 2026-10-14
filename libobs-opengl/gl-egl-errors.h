#pragma once

#include <stdlib.h>
#include <glad/glad_egl.h>
#include <util/base.h>

static inline void gl_egl_log_init_error(const char *operation)
{
	const EGLint error = eglGetError();
	const char *platform = getenv("EGL_PLATFORM");
	const char *display = getenv("WAYLAND_DISPLAY");
	blog(LOG_ERROR, "%s failed: EGL error 0x%04x; EGL_PLATFORM=%s; WAYLAND_DISPLAY=%s", operation,
	     (unsigned int)error, platform ? platform : "(unset)", display ? display : "(unset)");
}
