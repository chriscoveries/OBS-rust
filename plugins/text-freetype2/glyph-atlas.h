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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* Packing of rendered glyphs into the source's texture atlas, kept free of
 * FreeType and graphics calls so it can be tested on its own. */

struct glyph_atlas {
	uint32_t width, height;
	/* Where the next glyph goes. */
	uint32_t x, y;
	/* Tallest glyph so far; the height of a row. */
	uint32_t max_h;
};

struct glyph_atlas_ops {
	/* Whether the glyph for this character is already in the atlas. */
	bool (*is_cached)(void *ctx, wchar_t ch);
	/* Render the glyph for this character and report its bitmap size. */
	void (*render)(void *ctx, wchar_t ch, uint32_t *w, uint32_t *h);
	/* Keep the glyph just rendered at this position in the atlas. */
	void (*store)(void *ctx, wchar_t ch, uint32_t x, uint32_t y);
	/* Forget every cached glyph and clear the atlas pixels. */
	void (*clear)(void *ctx);
};

struct glyph_atlas_result {
	/* Glyphs added to the atlas; the texture must be uploaded again. */
	size_t stored;
	/* Some glyphs did not fit and are missing. */
	bool out_of_space;
};

/* Add the glyphs of `text` that are not cached yet. */
struct glyph_atlas_result glyph_atlas_cache(struct glyph_atlas *atlas, const wchar_t *text, size_t len,
					    const struct glyph_atlas_ops *ops, void *ctx);
