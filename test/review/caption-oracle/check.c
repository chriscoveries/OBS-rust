/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Expectations are independent of the Rust decoder and character classifier. */
#include <caption/caption.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;
static bool seen_cases[10], seen_pac[15 * 32 * 2];

static void screen_check(caption_frame_t *frame, unsigned id)
{
	for (int row = 0; row < SCREEN_ROWS; ++row) {
		char text[SCREEN_COLS * 4 + 1];
		size_t length = 0;
		for (int col = 0; col < SCREEN_COLS; ++col) {
			eia608_style_t style;
			int underline;
			const char *cell = caption_frame_read_char(frame, row, col, &style, &underline);
			const size_t n = strlen(cell);
			if (n > 4) {
				++failures;
				return;
			}
			if (n) {
				memcpy(text + length, cell, n);
				length += n;
			} else {
				text[length++] = ' ';
			}
		}
		while (length && text[length - 1] == ' ')
			--length;
		text[length] = 0;
		const char *expected = "";
		if (row == 14) {
			const char *last[] = {"T, T- T.", "END", "END", "LLLL", "élÑñ A"};
			expected = last[id];
		} else if ((id == 1 && row == 13) || (id == 2 && row == 12)) {
			expected = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
		} else if (id == 2 && row == 13) {
			expected = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
		}
		if (strcmp(text, expected)) {
			fprintf(stderr, "case=%u row=%d got=[%s] expected=[%s]\n", id, row, text, expected);
			++failures;
		}
	}
}

int main(void)
{
	caption_frame_t frame;
	char line[256], extra;
	unsigned id = 0, fps = 0, cases = 0, preambles = 0, pair_count = 0;
	uint64_t previous = 0;
	bool active = false;
	while (fgets(line, sizeof(line), stdin)) {
		if (!strchr(line, '\n'))
			return 2;
		if (!strncmp(line, "CASE ", 5)) {
			if (active || sscanf(line, "CASE %u %u %c", &id, &fps, &extra) != 2 || id >= 5 ||
			    (fps != 30 && fps != 60))
				return 2;
			const unsigned slot = id + (fps == 60 ? 5 : 0);
			if (seen_cases[slot])
				return 2;
			seen_cases[slot] = true;
			caption_frame_init(&frame);
			active = true;
			pair_count = 0;
		} else if (!strncmp(line, "P ", 2)) {
			uint64_t index;
			unsigned pair;
			if (!active || sscanf(line, "P %" SCNu64 " %x %c", &index, &pair, &extra) != 2 ||
			    index > UINT32_MAX || pair > UINT16_MAX || (pair_count && index <= previous) ||
			    (fps == 60 && index % 2))
				return 2;
			previous = index;
			++pair_count;
			if (caption_frame_decode(&frame, (uint16_t)pair, (double)index / fps) == LIBCAPTION_ERROR)
				++failures;
		} else if (!strcmp(line, "END\n")) {
			if (!active || pair_count < 7)
				return 2;
			screen_check(&frame, id);
			active = false;
			++cases;
		} else if (!strncmp(line, "PAC ", 4)) {
			unsigned row, col, underline, pair;
			if (active || sscanf(line, "PAC %u %u %u %x %c", &row, &col, &underline, &pair, &extra) != 4 ||
			    row >= 15 || col >= 32 || underline > 1 || pair > UINT16_MAX)
				return 2;
			const unsigned slot = (row * 32 + col) * 2 + underline;
			if (seen_pac[slot])
				return 2;
			seen_pac[slot] = true;
			int r, c, channel, u;
			eia608_style_t style;
			if (!eia608_parity_varify((uint16_t)pair) || !eia608_is_preamble((uint16_t)pair) ||
			    !eia608_parse_preamble((uint16_t)pair, &r, &c, &style, &channel, &u) ||
			    r != (int)row || c != (int)(col / 4 * 4) || u != (int)underline || channel != 0 ||
			    style != eia608_style_white)
				++failures;
			++preambles;
		} else {
			return 2;
		}
	}
	if (ferror(stdin) || active || cases != 10 || preambles != 960)
		return 2;
	printf("libcaption screens=%u preambles=%u failures=%u\n", cases, preambles, failures);
	return failures ? 1 : 0;
}
