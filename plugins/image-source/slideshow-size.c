#include "slideshow-size.h"

#include <inttypes.h>
#include <stdio.h>

void slideshow_parse_size(const char *str, uint32_t base_cx, uint32_t base_cy, uint32_t *cx, uint32_t *cy)
{
	uint32_t w = 0;
	uint32_t h = 0;

	*cx = base_cx;
	*cy = base_cy;

	if (!str)
		return;

	if (sscanf(str, "%" SCNu32 "x%" SCNu32, &w, &h) == 2 && w && h) {
		*cx = w;
		*cy = h;
		return;
	}

	if (sscanf(str, "%" SCNu32 ":%" SCNu32, &w, &h) != 2 || !w || !h || !base_cx || !base_cy)
		return;

	/* As the original slideshow did: keep the canvas side that fits and
	 * widen or heighten the other to the ratio. */
	uint64_t ratio_w = (uint64_t)w * base_cy;
	uint64_t ratio_h = (uint64_t)h * base_cx;

	if (ratio_w > ratio_h)
		*cx = (uint32_t)((uint64_t)base_cy * w / h);
	else if (ratio_w < ratio_h)
		*cy = (uint32_t)((uint64_t)base_cx * h / w);
}
