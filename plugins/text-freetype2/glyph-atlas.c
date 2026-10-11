/******************************************************************************
Copyright (C) 2026 by Ryan Doupe

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "glyph-atlas.h"

struct glyph_atlas_result glyph_atlas_cache(struct glyph_atlas *atlas, const wchar_t *text, size_t len,
					    const struct glyph_atlas_ops *ops, void *ctx)
{
	struct glyph_atlas_result result = {0};
	/* Glyphs of earlier texts are only evicted when the atlas is full,
	 * and only once per text: an atlas that started empty cannot do
	 * better by starting again. */
	bool can_evict = atlas->x != 0 || atlas->y != 0;
	uint32_t dx = atlas->x;
	uint32_t dy = atlas->y;

	for (size_t i = 0; i < len; i++) {
		const wchar_t ch = text[i];
		uint32_t g_w = 0, g_h = 0;

		if (ops->is_cached(ctx, ch))
			continue;

		ops->render(ctx, ch, &g_w, &g_h);

		if (atlas->max_h < g_h)
			atlas->max_h = g_h;

		if (dx + g_w >= atlas->width) {
			dx = 0;
			dy += atlas->max_h + 1;
		}

		if (dy + g_h >= atlas->height) {
			if (can_evict) {
				/* Start over with only this text's glyphs. */
				can_evict = false;
				ops->clear(ctx);
				dx = 0;
				dy = 0;
				i = (size_t)-1;
				continue;
			}
			result.out_of_space = true;
			break;
		}

		ops->store(ctx, ch, dx, dy);

		dx += (g_w + 1);
		if (dx >= atlas->width) {
			dx = 0;
			dy += atlas->max_h;
		}

		result.stored++;
	}

	atlas->x = dx;
	atlas->y = dy;
	return result;
}
