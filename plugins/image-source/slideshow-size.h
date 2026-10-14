#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The size of an Image Slide Show from its "Bounding Size/Aspect Ratio"
 * setting (use_custom_size), for a canvas of base_cx x base_cy:
 *
 * - "WxH" is that size;
 * - "W:H" is the canvas widened or heightened to that aspect ratio, as the
 *   original Image Slide Show did;
 * - anything else (empty, "Automatic" from older versions, a typo, a size
 *   or ratio with a zero) is the canvas size.
 *
 * The result is never 0x0 for a canvas with a size. */
void slideshow_parse_size(const char *str, uint32_t base_cx, uint32_t base_cy, uint32_t *cx, uint32_t *cy);

#ifdef __cplusplus
}
#endif
