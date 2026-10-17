#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

#include <util/dstr.h>
#include <util/darray.h>
#include <util/c99defs.h>

struct font_path_info {
	char *face_and_style;
	uint32_t full_len;
	uint32_t face_len;

	bool is_bitmap;
	uint32_t num_sizes;
	int *sizes;

	bool bold;
	bool italic;

	char *path;
	FT_Long index;
};

static inline void font_path_info_free(struct font_path_info *info)
{
	bfree(info->sizes);
	bfree(info->face_and_style);
	bfree(info->path);
}

typedef DARRAY(struct font_path_info) font_path_info_array_t;
extern font_path_info_array_t font_list;
extern void save_font_list(void);

extern void build_font_path_info(FT_Face face, FT_Long idx, const char *path);
extern char *sfnt_name_to_utf8(FT_SfntName *sfnt_name);

extern bool load_cached_os_font_list(void);
extern void load_os_font_list(void);
extern void free_os_font_list(void);
extern const char *get_font_path(const char *family, uint16_t size, const char *style, uint32_t flags, FT_Long *idx);
/* Makes the fonts bundled in `dir` (the plugin's data/fonts) findable by
 * get_font_path, whether or not they are installed. */
extern void load_bundled_fonts(const char *dir);
