/* Windowless zero-copy measurement harness (fork #47, M1/M2 epic).
 *
 * Boots OBS with no NSApplication, no window and no display: Metal video,
 * one color scene, a VideoToolbox encoder into a null output. Runs until
 * the output reports N frames, then prints the profiler `download_frame`
 * overall count (one per GPU-sync map today; M2 drives it down) and exits.
 *
 * Usage: zero-copy-harness [frames=120] [--raw]
 *
 * This is a CLI measurement tool, not a cmocka unit test: it needs a full
 * build tree (graphics + encoder + output modules) and real GPU/VideoToolbox.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>

#include <util/base.h>
#include <util/platform.h>
#include <util/profiler.h>
#include <obs.h>

#define DEFAULT_FRAMES 120
#define TIMEOUT_MS 120000

struct wait_state {
	uint64_t waits;
};

static char vt_plugin_binary[1152] = {0};
static void *vt_plugin_handle = NULL;

/* Byte counter living in the loaded mac-videotoolbox plugin (see
 * vt-frame-copy.h). Resolved with dlopen(RTLD_NOLOAD) so the harness
 * reads the same counter the encoder bumps. Missing symbols are fatal so
 * the harness cannot report a false zero. */
static void (*vt_copy_bytes_reset_fn)(void) = NULL;
static uint64_t (*vt_copy_bytes_total_fn)(void) = NULL;

static bool resolve_copy_counter(void)
{
	vt_plugin_handle = dlopen(vt_plugin_binary, RTLD_LAZY | RTLD_NOLOAD);
	if (!vt_plugin_handle) {
		const char *error = dlerror();
		fprintf(stderr, "harness: cannot find loaded VideoToolbox plugin at %s: %s\n", vt_plugin_binary,
		error ? error : "dlopen failed");
		return false;
	}

	(void)dlerror();
	void *reset_symbol = dlsym(vt_plugin_handle, "vt_copy_bytes_reset");
	const char *error = dlerror();
	if (error || !reset_symbol || sizeof(reset_symbol) != sizeof(vt_copy_bytes_reset_fn)) {
		fprintf(stderr, "harness: cannot resolve vt_copy_bytes_reset: %s\n", error ? error : "missing symbol");
		return false;
	}
	memcpy(&vt_copy_bytes_reset_fn, &reset_symbol, sizeof(vt_copy_bytes_reset_fn));

	(void)dlerror();
	void *total_symbol = dlsym(vt_plugin_handle, "vt_copy_bytes_total");
	error = dlerror();
	if (error || !total_symbol || sizeof(total_symbol) != sizeof(vt_copy_bytes_total_fn)) {
		fprintf(stderr, "harness: cannot resolve vt_copy_bytes_total: %s\n", error ? error : "missing symbol");
		return false;
	}
	memcpy(&vt_copy_bytes_total_fn, &total_symbol, sizeof(vt_copy_bytes_total_fn));
	return true;
}

static uint64_t read_copy_bytes(void)
{
	return vt_copy_bytes_total_fn();
}

struct child_ctx {
	struct wait_state *state;
};

static bool collect_child(void *context, profiler_snapshot_entry_t *entry);

static void find_in_children(profiler_snapshot_entry_t *entry, struct wait_state *state)
{
	const char *name = profiler_snapshot_entry_name(entry);
	uint64_t count = profiler_snapshot_entry_overall_count(entry);
	if (name && strcmp(name, "download_frame") == 0)
		state->waits += count;
	struct child_ctx ctx = {state};
	profiler_snapshot_enumerate_children(entry, collect_child, &ctx);
}

static bool collect_child(void *context, profiler_snapshot_entry_t *entry)
{
	struct child_ctx *ctx = context;
	find_in_children(entry, ctx->state);
	return true;
}

static bool find_download_waits(void *context, profiler_snapshot_entry_t *entry)
{
	struct wait_state *state = context;
	find_in_children(entry, state);
	return true;
}

static uint64_t snapshot_download_waits(void)
{
	struct wait_state state = {0};
	profiler_snapshot_t *snap = profile_snapshot_create();
	if (!snap)
		return 0;
	profiler_snapshot_enumerate_roots(snap, find_download_waits, &state);
	profile_snapshot_free(snap);
	return state.waits;
}

static const char *find_vt_encoder_id(void)
{
	static char id[128];
	static char fallback[128];
	size_t idx = 0;
	const char *candidate = NULL;

	fallback[0] = 0;
	while (obs_enum_encoder_types(idx++, &candidate)) {
		if (!candidate || !strstr(candidate, "videotoolbox"))
			continue;
		if (strstr(candidate, "264")) {
			snprintf(id, sizeof(id), "%s", candidate);
			return id;
		}
		if (!fallback[0])
			snprintf(fallback, sizeof(fallback), "%s", candidate);
	}

	return fallback[0] ? fallback : NULL;
}

static bool load_build_tree_modules(const char *argv0)
{
	/* The harness binary lives at <build>/test/perf/<Config>/. Mirror
	 * the frontend's macOS loader: one %module% pattern covering every
	 * dev-tree plugin bundle underneath <build>/plugins. Returns false
	 * if any module fails. */
	char exe[1024];
	char bin[1152];
	char data[1152];

	if (!realpath(argv0, exe))
		return false;
	char *slash = strrchr(exe, '/');
	if (!slash)
		return false;
	*slash = 0; /* executable name */
	slash = strrchr(exe, '/');
	if (!slash)
		return false;
	char config[64];
	snprintf(config, sizeof(config), "%s", slash + 1);
	*slash = 0; /* Debug, RelWithDebInfo, etc. */
	for (int i = 0; i < 2; i++) {
		slash = strrchr(exe, '/');
		if (!slash)
			return false;
		*slash = 0; /* perf, test */
	}
	snprintf(bin, sizeof(bin), "%s/plugins/%%module%%/%s/%%module%%.plugin/Contents/MacOS", exe, config);
	snprintf(data, sizeof(data), "%s/plugins/%%module%%/%s/%%module%%.plugin/Contents/Resources", exe, config);
	snprintf(vt_plugin_binary, sizeof(vt_plugin_binary),
		 "%s/plugins/mac-videotoolbox/%s/mac-videotoolbox.plugin/Contents/MacOS/mac-videotoolbox", exe,
		 config);

	struct obs_module_failure_info error = {0};
	struct obs_runtime_module_info info = {
		.path_info = {.binary = bin, .data = data},
		/* PLUGIN, not LEGACY_PLUGIN: these names are on the core-module
		 * list, which legacy entries skip. */
		.type = OBS_MODULE_TYPE_PLUGIN,
		.name = NULL,
	};
	obs_load_plugins(&info, &error);
	bool ok = error.count == 0;
	if (!ok)
		fprintf(stderr, "harness: %u module(s) failed to load\n", (unsigned)error.count);
	obs_module_failure_info_free(&error);

	/* Runs obs_module_post_load hooks (e.g. mac-videotoolbox registers
	 * its encoders there, after its async encoder list resolves). */
	obs_post_load_modules();
	return ok;
}

int main(int argc, char **argv)
{
	bool force_raw = false;
	int want_frames = DEFAULT_FRAMES;
	if (argc > 1 && strcmp(argv[1], "--raw") == 0) {
		force_raw = true;
	} else if (argc > 1) {
		want_frames = atoi(argv[1]);
	}
	if (argc > 2 && strcmp(argv[2], "--raw") == 0)
		force_raw = true;
	else if (argc > 2) {
		fprintf(stderr, "usage: %s [frames=%d] [--raw]\n", argv[0], DEFAULT_FRAMES);
		return 1;
	}
	if (want_frames <= 0)
		want_frames = DEFAULT_FRAMES;

	if (!obs_startup("en", NULL, NULL)) {
		fprintf(stderr, "harness: obs_startup failed\n");
		return 1;
	}

	/* Before video reset: threads latch the disabled state permanently
	 * otherwise (only lock_root failures re-arm it, and the render
	 * thread never reenables itself). */
	profiler_start();

	struct obs_video_info ovi = {0};
	ovi.adapter = 0;
	ovi.fps_num = 60;
	ovi.fps_den = 1;
	/* Bare filename with extension: os_dlopen only appends ".so" when no
	 * known extension is present, so this resolves via DYLD_LIBRARY_PATH
	 * (which must include the built libobs-metal directory). */
	ovi.graphics_module = "libobs-metal.dylib";
	/* GPU conversion path: the M1 zero-copy work happens here (NV12
	 * textures staged on the GPU). Raw path would bypass it. */
	ovi.gpu_conversion = !force_raw;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.base_width = 1920;
	ovi.base_height = 1080;
	ovi.output_width = 1920;
	ovi.output_height = 1080;

	if (obs_reset_video(&ovi) != 0) {
		fprintf(stderr, "harness: obs_reset_video (Metal) failed\n");
		obs_shutdown();
		return 1;
	}

	struct obs_audio_info oai = {0};
	oai.samples_per_sec = 48000;
	oai.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&oai)) {
		fprintf(stderr, "harness: obs_reset_audio failed\n");
		obs_shutdown();
		return 1;
	}

	if (!load_build_tree_modules(argv[0])) {
		fprintf(stderr, "harness: module load reported failures\n");
		obs_shutdown();
		return 1;
	}
	if (!resolve_copy_counter()) {
		if (vt_plugin_handle)
			dlclose(vt_plugin_handle);
		obs_shutdown();
		return 1;
	}

	obs_scene_t *scene = obs_scene_create("harness scene");
	obs_source_t *color = obs_source_create("color_source", "harness color", NULL, NULL);
	if (!scene || !color) {
		fprintf(stderr, "harness: scene/source setup failed\n");
		obs_shutdown();
		return 1;
	}
	obs_scene_add(scene, color);
	obs_set_output_source(0, obs_scene_get_source(scene));

	const char *enc_id = find_vt_encoder_id();
	if (!enc_id) {
		fprintf(stderr, "harness: no VideoToolbox encoder found\n");
		obs_shutdown();
		return 1;
	}

	obs_data_t *enc_settings = obs_data_create();
	obs_data_set_int(enc_settings, "bitrate", 2500);
	obs_encoder_t *encoder = obs_video_encoder_create(enc_id, "harness vt", enc_settings, NULL);
	obs_data_release(enc_settings);
	if (!encoder) {
		fprintf(stderr, "harness: encoder create failed\n");
		obs_shutdown();
		return 1;
	}

	obs_output_t *output = obs_output_create("null_output", "harness null", NULL, NULL);
	if (!output) {
		fprintf(stderr, "harness: null output create failed\n");
		obs_shutdown();
		return 1;
	}
	obs_encoder_set_video(encoder, obs_get_video());
	obs_output_set_video_encoder(output, encoder);

	/* Null output carries audio too; Core Audio AAC is Apple-native. */
	obs_data_t *aenc_settings = obs_data_create();
	obs_data_set_int(aenc_settings, "bitrate", 160);
	obs_encoder_t *audio_encoder = obs_audio_encoder_create("CoreAudio_AAC", "harness aac", aenc_settings, 0, NULL);
	obs_data_release(aenc_settings);
	if (audio_encoder) {
		obs_encoder_set_audio(audio_encoder, obs_get_audio());
		obs_output_set_audio_encoder(output, audio_encoder, 0);
	}
	vt_copy_bytes_reset_fn();
	const int frames_before = obs_output_get_total_frames(output);
	const uint64_t waits_before = snapshot_download_waits();
	if (!obs_output_start(output)) {
		fprintf(stderr, "harness: output start failed: %s\n", obs_output_get_last_error(output));
		dlclose(vt_plugin_handle);
		obs_shutdown();
		return 1;
	}

	int waited_ms = 0;
	while (obs_output_get_total_frames(output) < want_frames && waited_ms < TIMEOUT_MS) {
		os_sleep_ms(50);
		waited_ms += 50;
	}
	obs_output_stop(output);
	int frames = obs_output_get_total_frames(output) - frames_before;
	uint64_t waits_after = snapshot_download_waits();
	uint64_t copied_bytes = read_copy_bytes();
	uint64_t waits = waits_after >= waits_before ? waits_after - waits_before : 0;
	printf("frames=%d waits=%llu copied_bytes=%llu path=%s\n", frames, (unsigned long long)waits,
	       (unsigned long long)copied_bytes, force_raw ? "raw" : "texture");
	obs_output_release(output);
	obs_encoder_release(encoder);
	if (audio_encoder)
		obs_encoder_release(audio_encoder);
	obs_set_output_source(0, NULL);
	obs_source_remove(obs_scene_get_source(scene));
	obs_scene_release(scene);
	obs_source_release(color);
	obs_shutdown();
	dlclose(vt_plugin_handle);

	if (frames < want_frames) {
		fprintf(stderr, "harness: timeout waiting for %d frames\n", want_frames);
		return 1;
	}
	if ((force_raw && copied_bytes == 0) || (!force_raw && copied_bytes != 0)) {
		fprintf(stderr, "harness: copy counter did not match the requested %s path\n",
			force_raw ? "raw" : "texture");
		return 1;
	}
	return 0;
}
