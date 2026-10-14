#include "replay-buffer-naming.h"
#include "output-filename-format.h"

#include <stdlib.h>

void replay_buffer_naming_set(obs_data_t *settings, const struct replay_buffer_naming *naming)
{
	char *format = output_filename_format(naming->filename_format, naming->prefix, naming->suffix);

	obs_data_set_string(settings, "directory", naming->directory ? naming->directory : "");
	obs_data_set_string(settings, "format", format ? format : "");
	obs_data_set_string(settings, "extension", naming->extension ? naming->extension : "");
	obs_data_set_bool(settings, "allow_spaces", naming->allow_spaces);

	free(format);
}

void replay_buffer_naming_refresh(obs_output_t *replay_buffer, const struct replay_buffer_naming *naming)
{
	if (!replay_buffer)
		return;

	/* obs_output_update() merges these into the output's settings. */
	obs_data_t *settings = obs_data_create();
	replay_buffer_naming_set(settings, naming);
	obs_output_update(replay_buffer, settings);
	obs_data_release(settings);
}
