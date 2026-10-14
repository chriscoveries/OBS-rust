#pragma once

#include <stdbool.h>

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The settings of the replay buffer output (obs-ffmpeg-mux.c) that name a
 * saved replay, without Qt. The replay buffer reads them each time it saves:
 * "directory", "format" (the filename formatting with the replay buffer
 * prefix and suffix, as output_filename_format() makes it), "extension" and
 * "allow_spaces". */
struct replay_buffer_naming {
	const char *directory;
	const char *filename_format;
	const char *prefix;
	const char *suffix;
	const char *extension;
	bool allow_spaces;
};

/* Puts the naming settings into settings (for the replay buffer's start). */
void replay_buffer_naming_set(obs_data_t *settings, const struct replay_buffer_naming *naming);

/* Applies the naming settings to a replay buffer that may be running, before
 * a save, so that a replay saved after the filename settings changed uses
 * them. Leaves every other setting of the output as it is. */
void replay_buffer_naming_refresh(obs_output_t *replay_buffer, const struct replay_buffer_naming *naming);

#ifdef __cplusplus
}
#endif
