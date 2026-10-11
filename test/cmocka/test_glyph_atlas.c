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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <setjmp.h>
#include <cmocka.h>

#include "glyph-atlas.h"

/* Upstream obsproject/obs-studio#11819: a FreeType 2 text source reading a
 * file that keeps changing stops drawing new letters once its glyph atlas is
 * full, because glyphs of earlier texts are never evicted. */

#define ATLAS_SIZE 64
#define GLYPH_SIZE 10
/* 5 glyphs of 10 px plus 1 px spacing per 64 px row, 5 rows. */
#define ATLAS_CAPACITY 25
#define MAX_CHARS 0x10000

struct fake_glyph {
	bool cached;
	uint32_t x, y;
};

struct fake_cache {
	struct fake_glyph glyphs[MAX_CHARS];
	size_t clears;
};

static bool fake_is_cached(void *data, wchar_t ch)
{
	struct fake_cache *cache = data;
	return cache->glyphs[(uint32_t)ch % MAX_CHARS].cached;
}

static void fake_render(void *data, wchar_t ch, uint32_t *w, uint32_t *h)
{
	(void)data;
	(void)ch;
	*w = GLYPH_SIZE;
	*h = GLYPH_SIZE;
}

static void fake_store(void *data, wchar_t ch, uint32_t x, uint32_t y)
{
	struct fake_cache *cache = data;
	struct fake_glyph *glyph = &cache->glyphs[(uint32_t)ch % MAX_CHARS];

	glyph->cached = true;
	glyph->x = x;
	glyph->y = y;
}

static void fake_clear(void *data)
{
	struct fake_cache *cache = data;
	memset(cache->glyphs, 0, sizeof(cache->glyphs));
	cache->clears++;
}

static const struct glyph_atlas_ops fake_ops = {
	.is_cached = fake_is_cached,
	.render = fake_render,
	.store = fake_store,
	.clear = fake_clear,
};

static struct fake_cache cache;

static int setup(void **state)
{
	(void)state;
	memset(&cache, 0, sizeof(cache));
	return 0;
}

static struct glyph_atlas new_atlas(void)
{
	struct glyph_atlas atlas = {.width = ATLAS_SIZE, .height = ATLAS_SIZE};
	return atlas;
}

static struct glyph_atlas_result cache_text(struct glyph_atlas *atlas, const wchar_t *text)
{
	return glyph_atlas_cache(atlas, text, wcslen(text), &fake_ops, &cache);
}

/* Every character of `text` has a glyph, and no two cached glyphs overlap. */
static void assert_text_drawable(const wchar_t *text)
{
	const size_t len = wcslen(text);

	for (size_t i = 0; i < len; i++) {
		const struct fake_glyph *glyph = &cache.glyphs[(uint32_t)text[i] % MAX_CHARS];
		if (!glyph->cached) {
			fprintf(stderr, "glyph U+%04X of the current text was never cached\n", (unsigned)text[i]);
			fail();
		}
		assert_true(glyph->x + GLYPH_SIZE <= ATLAS_SIZE);
		assert_true(glyph->y + GLYPH_SIZE <= ATLAS_SIZE);
	}

	for (uint32_t a = 0; a < MAX_CHARS; a++) {
		if (!cache.glyphs[a].cached)
			continue;
		for (uint32_t b = a + 1; b < MAX_CHARS; b++) {
			if (!cache.glyphs[b].cached)
				continue;
			const bool apart = cache.glyphs[a].x + GLYPH_SIZE <= cache.glyphs[b].x ||
					   cache.glyphs[b].x + GLYPH_SIZE <= cache.glyphs[a].x ||
					   cache.glyphs[a].y + GLYPH_SIZE <= cache.glyphs[b].y ||
					   cache.glyphs[b].y + GLYPH_SIZE <= cache.glyphs[a].y;
			if (!apart) {
				fprintf(stderr, "glyphs U+%04X and U+%04X overlap\n", (unsigned)a, (unsigned)b);
				fail();
			}
		}
	}
}

static void text_that_fits_is_cached_once(void **state)
{
	(void)state;
	struct glyph_atlas atlas = new_atlas();

	struct glyph_atlas_result result = cache_text(&atlas, L"abcab");
	assert_int_equal(result.stored, 3);
	assert_false(result.out_of_space);
	assert_text_drawable(L"abcab");

	result = cache_text(&atlas, L"cba");
	assert_int_equal(result.stored, 0);
	assert_false(result.out_of_space);
	assert_int_equal(cache.clears, 0);
}

static void atlas_holds_its_capacity(void **state)
{
	(void)state;
	struct glyph_atlas atlas = new_atlas();

	/* 25 distinct glyphs fill it exactly. */
	const struct glyph_atlas_result result = cache_text(&atlas, L"abcdefghijklmnopqrstuvwxy");
	assert_int_equal(result.stored, ATLAS_CAPACITY);
	assert_false(result.out_of_space);
	assert_int_equal(cache.clears, 0);
	assert_text_drawable(L"abcdefghijklmnopqrstuvwxy");
}

static void new_text_after_full_atlas_is_drawn(void **state)
{
	(void)state;
	struct glyph_atlas atlas = new_atlas();

	/* The file showed Latin text first and filled the atlas... */
	cache_text(&atlas, L"abcdefghijklmnopqrstuvwxy");

	/* ...then switched to a Cyrillic title. */
	const wchar_t *title = L"\x041C\x0423\x0420\x0410\x041A\x0410\x041C\x0418";
	const struct glyph_atlas_result result = cache_text(&atlas, title);

	assert_text_drawable(title);
	assert_false(result.out_of_space);
	assert_true(result.stored > 0);
}

static void texts_keep_rotating_without_losing_letters(void **state)
{
	(void)state;
	struct glyph_atlas atlas = new_atlas();
	/* Ten titles of eight new letters each: far more than the atlas
	 * holds in total, but each one fits on its own. */
	wchar_t title[9];

	for (wchar_t n = 0; n < 10; n++) {
		for (wchar_t i = 0; i < 8; i++)
			title[i] = (wchar_t)(0x0400 + n * 8 + i);
		title[8] = 0;

		cache_text(&atlas, title);
		assert_text_drawable(title);
	}
}

static void text_larger_than_atlas_is_not_retried_forever(void **state)
{
	(void)state;
	struct glyph_atlas atlas = new_atlas();
	wchar_t text[ATLAS_CAPACITY + 6];

	for (size_t i = 0; i < ATLAS_CAPACITY + 5; i++)
		text[i] = (wchar_t)(0x0400 + i);
	text[ATLAS_CAPACITY + 5] = 0;

	const struct glyph_atlas_result result = cache_text(&atlas, text);
	assert_true(result.out_of_space);
	assert_int_equal(result.stored, ATLAS_CAPACITY);
	/* An atlas that started empty gains nothing from being cleared. */
	assert_int_equal(cache.clears, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(text_that_fits_is_cached_once, setup),
		cmocka_unit_test_setup(atlas_holds_its_capacity, setup),
		cmocka_unit_test_setup(new_text_after_full_atlas_is_drawn, setup),
		cmocka_unit_test_setup(texts_keep_rotating_without_losing_letters, setup),
		cmocka_unit_test_setup(text_larger_than_atlas_is_not_retried_forever, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
