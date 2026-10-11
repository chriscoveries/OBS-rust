#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The filename format a file output (recording, replay buffer, screenshot,
 * stream dump) passes to os_generate_formatted_filename.
 *
 * prefix and suffix may be NULL or empty. A prefix goes in front of the last
 * path component of format, followed by a space; a suffix goes at the end,
 * after a space unless it starts with one.
 *
 * Characters that Windows filesystems reject (* ? " | : > <) become '_',
 * so a format such as "%CCYY-%MM-%DD %hh:%mm:%ss" gives a name that can be
 * written to NTFS or exFAT. '\\' becomes '/', so subdirectories in format
 * stay subdirectories. The recording directory is not part of format and is
 * never changed.
 *
 * Returns a malloc'd string the caller frees with free(). A NULL format is
 * treated as an empty one. Returns NULL only if memory runs out. */
char *output_filename_format(const char *format, const char *prefix, const char *suffix);

#ifdef __cplusplus
}
#endif
