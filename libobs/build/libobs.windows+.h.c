/* Unity group (BUILD.md): Windows host code. */

#include "libobs-windows.pch.h"

#include "../audio-monitoring/win32/wasapi-enum-devices.c.h"
#include "../audio-monitoring/win32/wasapi-monitoring-available.c.h"
#include "../audio-monitoring/win32/wasapi-output.c.h"
#include "../obs-core-modules-loader-win.c.h"
#include "../obs-windows.c.h"
#include "../util/windows/device-enum.c.h"
#include "../util/windows/window-helpers.c.h"
