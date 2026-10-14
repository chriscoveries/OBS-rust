/* Starting a transition must reject a destination containing that transition.
 * Each public-API scenario has its own libobs in a child process: an alarm
 * bounds a recursive mutex acquisition without hanging the test runner.
 * obs_startup() needs a display on Linux, so run under xvfb-run. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <obs.h>
#include <util/base.h>
#include <util/platform.h>

#define CHILD_TIMEOUT 10
#define CHILD_SKIP 77
#define TRANSITION_ID "recursive_destination_transition"
#define LEAF_ID "recursive_destination_leaf"
#define HELD_ID "recursive_destination_held"

static unsigned start_calls;
static unsigned start_signals;
static unsigned rejection_logs;
static obs_source_t *callback_transition;
static obs_source_t *callback_dest;
static obs_source_t *callback_child;
static obs_source_t *callback_restart_leaf;
static enum obs_transition_mode callback_restart_mode = OBS_TRANSITION_MODE_MANUAL;
static uint32_t callback_restart_ms = 1000;
static bool callback_restart_started;
static bool callback_armed;

static void start_signal(void *data, calldata_t *cd)
{
	(void)data;
	(void)cd;
	start_signals++;
}

static void quiet_log(int lvl, const char *msg, va_list args, void *p)
{
	(void)p;
	char line[1024];
	vsnprintf(line, sizeof(line), msg, args);
	if (lvl == LOG_WARNING && strstr(line, "recursive destination") && strstr(line, "transition 'outer'") &&
	    (strstr(line, "destination 'inner'") || strstr(line, "destination 'outer'"))) {
		rejection_logs++;
		fprintf(stderr, "rejection log: %s\n", line);
	}
}

static const char *source_name(void *data)
{
	(void)data;
	return "recursive destination fixture";
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;
	return source;
}

static void source_destroy(void *data)
{
	(void)data;
}

static uint32_t leaf_size(void *data)
{
	(void)data;
	return 64;
}

static pthread_mutex_t race_mutex = PTHREAD_MUTEX_INITIALIZER;
static obs_source_t *race_transition;
static bool race_arm_on_start;
static bool race_hold_armed;

static void transition_start(void *data)
{
	start_calls++;
	if (data == race_transition) {
		pthread_mutex_lock(&race_mutex);
		race_hold_armed = race_arm_on_start;
		race_arm_on_start = false;
		pthread_mutex_unlock(&race_mutex);
	}
	if (data == callback_transition && callback_armed) {
		callback_armed = false;
		if (callback_restart_leaf)
			callback_restart_started = obs_transition_start(data, callback_restart_mode,
									callback_restart_ms, callback_restart_leaf);
		if (callback_dest) {
			obs_transition_set(callback_dest, callback_child);
			fprintf(stderr, "callback: destination graph change returned\n");
		}
	}
}

static bool transition_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
				    size_t channels, size_t sample_rate)
{
	(void)data;
	(void)ts_out;
	(void)audio;
	(void)mixers;
	(void)channels;
	(void)sample_rate;
	return false;
}

enum destination_kind {
	NESTED_RECURSIVE,
	INDIRECT_RECURSIVE,
	NONLEAF_RECURSIVE,
	NESTED_ACYCLIC,
	SELF,
	EMPTY_SELF,
	ACTIVE_REUSE,
	CALLBACK_RECURSIVE,
	CALLBACK_INDIRECT,
	CALLBACK_ACTIVE,
	CALLBACK_TIMING_REJECTED,
	CALLBACK_LONG_TIMING_REJECTED,
	CALLBACK_TIMING_SAME_DEST,
	CALLBACK_TIMING_NEW_DEST,
	NULL_DEST_TIMING,
	RENDER_RACE,
	RENDER_RACE_TIME_DIRECT,
	RENDER_RACE_DIRECT,
};

/* A start made by the plugin's transition_start callback must keep its own
 * AUTO clock when the outer start is rejected, or completes toward the same
 * destination. Starts run in a video tick so no frame renders mid-start;
 * progress is read on a later frame. Transition time is public progress in
 * [0, 1]: a zero-duration clock reads 1 once the frame is after its start. */
struct timing_case {
	uint32_t nested_ms;
	uint32_t outer_ms;
	bool change_graph;
	bool outer_to_nested_dest;
	bool expect_started;
	bool expect_finished;
};

static const struct timing_case timing_cases[] = {
	[CALLBACK_TIMING_REJECTED] = {0, UINT32_MAX, true, false, false, true},
	[CALLBACK_LONG_TIMING_REJECTED] = {UINT32_MAX, 0, true, false, false, false},
	[CALLBACK_TIMING_SAME_DEST] = {0, UINT32_MAX, false, true, true, true},
	[CALLBACK_TIMING_NEW_DEST] = {0, UINT32_MAX, false, false, true, false},
};

static pthread_mutex_t timing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timing_cond = PTHREAD_COND_INITIALIZER;
static obs_source_t *timing_transition;
static obs_source_t *timing_dest;
static uint32_t timing_outer_ms;
static unsigned timing_phase; /* video thread only */
static bool timing_done;
static bool timing_started;
static uint64_t timing_returned;
static float timing_progress;

static void timing_tick(void *param, float seconds)
{
	(void)param;
	(void)seconds;
	if (timing_phase == 0) {
		timing_started =
			obs_transition_start(timing_transition, OBS_TRANSITION_MODE_AUTO, timing_outer_ms, timing_dest);
		timing_returned = os_gettime_ns();
		timing_phase = 1;
	} else if (timing_phase == 1 && obs_get_video_frame_time() > timing_returned) {
		timing_progress = obs_transition_get_time(timing_transition);
		timing_phase = 2;
		pthread_mutex_lock(&timing_mutex);
		timing_done = true;
		pthread_cond_signal(&timing_cond);
		pthread_mutex_unlock(&timing_mutex);
	}
}

static bool start_in_video_tick(obs_source_t *transition, uint32_t duration_ms, obs_source_t *dest, float *progress)
{
	timing_transition = transition;
	timing_dest = dest;
	timing_outer_ms = duration_ms;
	pthread_mutex_lock(&timing_mutex);
	obs_add_tick_callback(timing_tick, NULL);
	while (!timing_done)
		pthread_cond_wait(&timing_cond, &timing_mutex);
	pthread_mutex_unlock(&timing_mutex);
	obs_remove_tick_callback(timing_tick, NULL);
	*progress = timing_progress;
	return timing_started;
}

static bool is_timing_kind(enum destination_kind kind)
{
	return kind >= CALLBACK_TIMING_REJECTED && kind <= CALLBACK_TIMING_NEW_DEST;
}

/* Render race: the graphics thread renders the transition (output channel 0)
 * while a start toward an ordinary destination is in child admission, which
 * holds the transition's mutex. The destination's enum_active_sources, called
 * by admission, waits until the render thread has entered the transition's
 * video_render and is asleep, i.e. blocked on that mutex. That frame must use
 * the new start's clock: a fresh transition's previous clock reads progress 1,
 * which would end the new 10 s AUTO transition with "transition_video_stop" on
 * its first frame. Waits are bounded; nothing sleeps.
 *
 * The transition's video_render draws the way one of the shipped transitions
 * does. Whichever clock read comes first in that frame must wait for the start:
 * with obs_transition_get_time first, the race frame waits there, so the
 * direct-only case is what covers obs_transition_video_render_direct's read. */
#define RACE_WAIT_S 3

enum race_draw_kind {
	/* obs_transition_video_render, as the built-in transitions do. */
	RACE_DRAW_RENDER,
	/* transition-stinger.c without a track matte: obs_transition_get_time
	 * chooses the target, then obs_transition_video_render_direct draws it.
	 * The time must come from the new start's clock: the previous clock reads
	 * 1, past the stinger's transition point, and draws the destination. */
	RACE_DRAW_TIME_DIRECT,
	/* transition-stinger.c while its track matte is not ready:
	 * obs_transition_video_render_direct alone. */
	RACE_DRAW_DIRECT,
};

static pthread_cond_t race_cond = PTHREAD_COND_INITIALIZER;
static pthread_t race_main_thread;
static bool race_want_go;
static bool race_go;
static bool race_in_hold;
static bool race_render_blocked;
static unsigned race_renders;
static unsigned race_target;
static pid_t race_render_tid;
static unsigned race_video_stops;
static enum race_draw_kind race_draw_mode;
static bool race_seen_transitioning;
static float race_first_time;

static bool race_go_set(void)
{
	return race_go;
}

static bool race_hold_set(void)
{
	return race_in_hold;
}

static bool race_renders_reached(void)
{
	return race_renders >= race_target;
}

/* Called with race_mutex held. */
static bool race_wait(bool (*pred)(void))
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += RACE_WAIT_S;
	while (!pred()) {
		if (pthread_cond_timedwait(&race_cond, &race_mutex, &deadline) == ETIMEDOUT)
			return pred();
	}
	return true;
}

/* Linux: the thread is asleep ('S'), not running or runnable. */
static bool thread_sleeping(pid_t tid)
{
	char path[64];
	char buf[512];
	snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)tid);
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	char *name_end = strrchr(buf, ')');
	return name_end && name_end[1] == ' ' && name_end[2] == 'S';
}

static bool wait_thread_sleeping(pid_t tid)
{
	uint64_t deadline = os_gettime_ns() + RACE_WAIT_S * 1000000000ULL;
	while (!thread_sleeping(tid)) {
		if (os_gettime_ns() >= deadline)
			return false;
		sched_yield();
	}
	return true;
}

static void held_enum_active_sources(void *data, obs_source_enum_proc_t cb, void *param)
{
	(void)data;
	(void)cb;
	(void)param;
	if (!pthread_equal(pthread_self(), race_main_thread))
		return;
	pthread_mutex_lock(&race_mutex);
	if (!race_hold_armed) {
		pthread_mutex_unlock(&race_mutex);
		return;
	}
	race_hold_armed = false;
	race_in_hold = true;
	race_target = race_renders + 1;
	pthread_cond_broadcast(&race_cond);
	bool entered = race_wait(race_renders_reached);
	pid_t tid = race_render_tid;
	pthread_mutex_unlock(&race_mutex);
	/* After entering video_render, the render thread's only blocking point is
	 * the transition mutex held by this admission. */
	race_render_blocked = entered && wait_thread_sleeping(tid);
	fprintf(stderr, "admission: render entered=%d blocked=%d\n", entered, race_render_blocked);
}

static void transition_video_render(void *data, gs_effect_t *effect)
{
	(void)effect;
	pthread_mutex_lock(&race_mutex);
	race_renders++;
	race_render_tid = (pid_t)syscall(SYS_gettid);
	enum race_draw_kind kind = race_draw_mode;
	pthread_cond_broadcast(&race_cond);
	pthread_mutex_unlock(&race_mutex);

	if (kind == RACE_DRAW_RENDER) {
		obs_transition_video_render(data, NULL);
		return;
	}

	float t = kind == RACE_DRAW_TIME_DIRECT ? obs_transition_get_time(data) : 1.0f;
	enum obs_transition_target target = t < 0.5f ? OBS_TRANSITION_SOURCE_A : OBS_TRANSITION_SOURCE_B;
	bool transitioning = obs_transition_video_render_direct(data, target);

	pthread_mutex_lock(&race_mutex);
	if (transitioning && !race_seen_transitioning) {
		race_seen_transitioning = true;
		race_first_time = t;
	}
	pthread_mutex_unlock(&race_mutex);
}

/* Main render callback: runs on the graphics thread after this frame's ticks
 * and before the view renders the transition. */
static void race_draw(void *param, uint32_t cx, uint32_t cy)
{
	(void)param;
	(void)cx;
	(void)cy;
	pthread_mutex_lock(&race_mutex);
	if (race_want_go) {
		race_want_go = false;
		race_go = true;
		pthread_cond_broadcast(&race_cond);
		race_wait(race_hold_set);
	}
	pthread_mutex_unlock(&race_mutex);
}

static void race_video_stop(void *data, calldata_t *cd)
{
	(void)data;
	(void)cd;
	pthread_mutex_lock(&race_mutex);
	race_video_stops++;
	pthread_mutex_unlock(&race_mutex);
}

static bool render_race(obs_source_t *transition, obs_source_t *dest, enum race_draw_kind kind)
{
	race_main_thread = pthread_self();
	pthread_mutex_lock(&race_mutex);
	race_transition = transition;
	race_draw_mode = kind;
	pthread_mutex_unlock(&race_mutex);
	signal_handler_connect(obs_source_get_signal_handler(transition), "transition_video_stop", race_video_stop,
			       NULL);
	obs_set_output_source(0, transition);
	obs_add_main_render_callback(race_draw, NULL);

	pthread_mutex_lock(&race_mutex);
	race_target = race_renders + 3;
	bool rendering = race_wait(race_renders_reached);
	race_want_go = rendering;
	race_arm_on_start = rendering;
	bool go = rendering && race_wait(race_go_set);
	pthread_mutex_unlock(&race_mutex);

	bool started = go && obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, 10000, dest);

	pthread_mutex_lock(&race_mutex);
	race_target = race_renders + 3;
	bool rendered_after = race_wait(race_renders_reached);
	unsigned video_stops = race_video_stops;
	bool seen_transitioning = race_seen_transitioning;
	float first_time = race_first_time;
	pthread_mutex_unlock(&race_mutex);

	bool active = obs_transition_is_active(transition);
	float progress = obs_transition_get_time(transition);
	fprintf(stderr,
		"render race (draw kind %d): rendering=%d start=%d render blocked in admission=%d frames after=%d "
		"video_stop_signals=%u active=%d progress=%.6f\n",
		(int)kind, rendering, started, race_render_blocked, rendered_after, video_stops, active, progress);

	bool ok = rendering && go && started && race_render_blocked && rendered_after && video_stops == 0 && active &&
		  progress < 1.0f;
	if (kind == RACE_DRAW_TIME_DIRECT) {
		fprintf(stderr, "first transitioning frame: seen=%d get_time=%.6f -> target %s\n", seen_transitioning,
			first_time, first_time < 0.5f ? "A" : "B");
		ok &= seen_transitioning && first_time < 1.0f;
	}

	obs_remove_main_render_callback(race_draw, NULL);
	obs_set_output_source(0, NULL);
	return ok;
}

static bool state_is(obs_source_t *transition, obs_source_t *a, obs_source_t *b, bool active, unsigned calls,
		     unsigned signals)
{
	obs_source_t *actual_a = obs_transition_get_source(transition, OBS_TRANSITION_SOURCE_A);
	obs_source_t *actual_b = obs_transition_get_source(transition, OBS_TRANSITION_SOURCE_B);
	bool ok = actual_a == a && actual_b == b && obs_transition_is_active(transition) == active &&
		  start_calls == calls && start_signals == signals;
	fprintf(stderr, "state: A=%d B=%d active=%d callbacks=%u signals=%u correct=%d\n", actual_a == a, actual_b == b,
		obs_transition_is_active(transition), start_calls, start_signals, ok);
	obs_source_release(actual_a);
	obs_source_release(actual_b);
	return ok;
}

static bool active_reuse(obs_source_t *outer, obs_source_t *inner, obs_source_t *leaf, obs_source_t *other)
{
	bool ok = true;
	for (unsigned i = 0; i < 3; i++) {
		unsigned calls = start_calls;
		unsigned signals = start_signals;
		ok &= obs_transition_start(outer, OBS_TRANSITION_MODE_MANUAL, 1000, other);
		ok &= state_is(outer, leaf, other, true, calls + 1, signals + 1);
		unsigned logs = rejection_logs;
		fprintf(stderr, "active graph built; rejecting inner -> outer, iteration %u\n", i);
		ok &= !obs_transition_start(outer, OBS_TRANSITION_MODE_AUTO, 1000, inner);
		ok &= state_is(outer, leaf, other, true, calls + 1, signals + 1);
		ok &= rejection_logs == logs + 1;
		obs_transition_set(outer, leaf);
		ok &= state_is(outer, leaf, NULL, false, calls + 1, signals + 1);
		ok &= !obs_transition_start(outer, OBS_TRANSITION_MODE_AUTO, 1000, leaf);
		ok &= state_is(outer, leaf, NULL, false, calls + 1, signals + 1);
	}
	obs_transition_clear(inner);
	obs_transition_clear(outer);
	ok &= state_is(outer, NULL, NULL, false, 3, 3);
	obs_transition_set(outer, leaf);
	ok &= obs_transition_start(outer, OBS_TRANSITION_MODE_MANUAL, 1000, NULL);
	ok &= state_is(outer, leaf, NULL, true, 4, 4);
	obs_transition_set(outer, leaf);
	obs_transition_set(inner, other);
	ok &= obs_transition_start(outer, OBS_TRANSITION_MODE_MANUAL, 1000, inner);
	ok &= state_is(outer, leaf, inner, true, 5, 5);
	return ok;
}

static int scenario(enum destination_kind kind)
{
	base_set_log_handler(quiet_log, NULL);
	if (!obs_startup("en-US", NULL, NULL))
		return CHILD_SKIP;

	struct obs_video_info ovi = {
		.graphics_module = GRAPHICS_MODULE_PATH,
		.fps_num = 30,
		.fps_den = 1,
		.base_width = 640,
		.base_height = 360,
		.output_width = 640,
		.output_height = 360,
		.output_format = VIDEO_FORMAT_NV12,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
		.scale_type = OBS_SCALE_BICUBIC,
	};
	if (obs_reset_video(&ovi) != OBS_VIDEO_SUCCESS) {
		obs_shutdown();
		return CHILD_SKIP;
	}

	struct obs_source_info info = {
		.id = LEAF_ID,
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO,
		.get_name = source_name,
		.create = source_create,
		.destroy = source_destroy,
		.get_width = leaf_size,
		.get_height = leaf_size,
	};
	obs_register_source(&info);
	info.id = HELD_ID;
	info.enum_active_sources = held_enum_active_sources;
	obs_register_source(&info);
	info.enum_active_sources = NULL;
	info.id = TRANSITION_ID;
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.get_width = NULL;
	info.get_height = NULL;
	info.transition_start = transition_start;
	info.audio_render = transition_audio_render;
	info.video_render = transition_video_render;
	obs_register_source(&info);

	obs_source_t *leaf = obs_source_create_private(LEAF_ID, "leaf", NULL);
	obs_source_t *other = obs_source_create_private(LEAF_ID, "other", NULL);
	obs_source_t *outer = obs_source_create_private(TRANSITION_ID, "outer", NULL);
	obs_source_t *inner = obs_source_create_private(TRANSITION_ID, "inner", NULL);
	obs_source_t *middle = obs_source_create_private(TRANSITION_ID, "middle", NULL);
	obs_source_t *held = obs_source_create_private(HELD_ID, "held", NULL);
	if (!leaf || !other || !outer || !inner || !middle || !held) {
		fprintf(stderr, "fixture source creation failed\n");
		return 1;
	}
	signal_handler_connect(obs_source_get_signal_handler(outer), "transition_start", start_signal, NULL);

	obs_source_t *source_a = leaf;
	if (kind == NONLEAF_RECURSIVE) {
		obs_transition_set(middle, leaf);
		source_a = middle;
	} else if (kind == EMPTY_SELF) {
		source_a = NULL;
	}
	obs_transition_set(outer, source_a);
	obs_transition_set(inner, leaf);
	if (kind == NESTED_RECURSIVE || kind == NONLEAF_RECURSIVE || kind == ACTIVE_REUSE) {
		obs_transition_set(inner, outer);
	} else if (kind == INDIRECT_RECURSIVE) {
		obs_transition_set(middle, outer);
		obs_transition_set(inner, middle);
	} else if (kind == CALLBACK_RECURSIVE || kind == CALLBACK_INDIRECT || kind == CALLBACK_ACTIVE) {
		if (kind == CALLBACK_INDIRECT)
			obs_transition_set(middle, outer);
		callback_transition = outer;
		callback_armed = true;
		callback_dest = inner;
		callback_child = kind == CALLBACK_INDIRECT ? middle : outer;
		callback_restart_leaf = kind == CALLBACK_ACTIVE ? other : NULL;
	} else if (is_timing_kind(kind)) {
		const struct timing_case *tc = &timing_cases[kind];
		callback_transition = outer;
		callback_armed = true;
		callback_restart_leaf = other;
		callback_restart_mode = OBS_TRANSITION_MODE_AUTO;
		callback_restart_ms = tc->nested_ms;
		callback_dest = tc->change_graph ? inner : NULL;
		callback_child = outer;
	}

	bool ok;
	if (kind == ACTIVE_REUSE) {
		ok = active_reuse(outer, inner, leaf, other);
	} else if (kind == RENDER_RACE) {
		ok = render_race(outer, held, RACE_DRAW_RENDER);
	} else if (kind == RENDER_RACE_TIME_DIRECT) {
		ok = render_race(outer, held, RACE_DRAW_TIME_DIRECT);
	} else if (kind == RENDER_RACE_DIRECT) {
		ok = render_race(outer, held, RACE_DRAW_DIRECT);
	} else if (kind == NULL_DEST_TIMING) {
		/* A start toward NULL keeps the empty B slot, so set_source takes its
		 * unchanged-slot path; that path must still start the new clock. */
		float progress;
		ok = obs_transition_start(outer, OBS_TRANSITION_MODE_AUTO, UINT32_MAX, other);
		obs_transition_set(outer, leaf);
		ok &= state_is(outer, leaf, NULL, false, 1, 1);
		bool started = start_in_video_tick(outer, 0, NULL, &progress);
		fprintf(stderr, "start toward NULL returned %s, later-frame progress %.9g\n",
			started ? "true" : "false", progress);
		ok &= started;
		ok &= state_is(outer, leaf, NULL, true, 2, 2);
		ok &= rejection_logs == 0;
		ok &= progress == 1.0f;
	} else if (is_timing_kind(kind)) {
		const struct timing_case *tc = &timing_cases[kind];
		obs_source_t *dest = tc->outer_to_nested_dest ? other : inner;
		float progress;
		fprintf(stderr, "graph built; nested AUTO %ums inside outer AUTO %ums, scenario %d\n", tc->nested_ms,
			tc->outer_ms, kind);
		bool started = start_in_video_tick(outer, tc->outer_ms, dest, &progress);
		fprintf(stderr, "outer start returned %s, nested start returned %s, later-frame progress %.9g\n",
			started ? "true" : "false", callback_restart_started ? "true" : "false", progress);
		ok = started == tc->expect_started && callback_restart_started;
		ok &= state_is(outer, leaf, tc->expect_started ? dest : other, true, 2, tc->expect_started ? 2U : 1U);
		ok &= rejection_logs == (tc->expect_started ? 0U : 1U);
		ok &= tc->expect_finished ? progress == 1.0f : progress < 1.0f;
	} else {
		obs_source_t *dest = kind == SELF || kind == EMPTY_SELF ? outer : inner;
		bool expect_start = kind == NESTED_ACYCLIC;
		bool reentry = kind == CALLBACK_RECURSIVE || kind == CALLBACK_INDIRECT || kind == CALLBACK_ACTIVE;
		bool callback_active = kind == CALLBACK_ACTIVE;
		unsigned calls = callback_active ? 2U : (expect_start || reentry ? 1U : 0U);
		unsigned signals = expect_start || callback_active ? 1U : 0U;
		fprintf(stderr, "graph built; calling obs_transition_start, scenario %d\n", kind);
		bool started = obs_transition_start(outer, OBS_TRANSITION_MODE_MANUAL, 1000, dest);
		fprintf(stderr, "start returned %s\n", started ? "true" : "false");
		ok = started == expect_start;
		ok &= state_is(outer, source_a, callback_active ? other : (expect_start ? dest : NULL),
			       expect_start || callback_active, calls, signals);
		if (callback_active)
			ok &= callback_restart_started;
		ok &= rejection_logs == (expect_start ? 0U : 1U);
		/* A rejected re-entrant start must release its destination reference and
		 * leave the transition reusable after the plugin's graph is cleared. */
		if (reentry && ok) {
			obs_transition_set(inner, other);
			obs_transition_clear(middle);
			obs_transition_set(outer, source_a);
			ok &= obs_transition_start(outer, OBS_TRANSITION_MODE_MANUAL, 1000, inner);
			ok &= state_is(outer, source_a, inner, true, calls + 1, signals + 1);
		}
	}

	obs_transition_clear(inner);
	obs_transition_clear(outer);
	obs_transition_clear(middle);
	obs_source_release(inner);
	obs_source_release(outer);
	obs_source_release(middle);
	obs_source_release(held);
	obs_source_release(leaf);
	obs_source_release(other);
	obs_shutdown();
	return ok ? 0 : 1;
}

static void run_in_child(enum destination_kind kind)
{
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		signal(SIGALRM, SIG_DFL);
		signal(SIGSEGV, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
		alarm(CHILD_TIMEOUT);
		int ret = scenario(kind);
		fflush(stderr);
		_exit(ret);
	}

	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited == -1 && errno == EINTR);
	assert_int_equal(waited, pid);
	if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_SKIP)
		skip();
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "child terminated by signal %d (%s)\n", WTERMSIG(status),
			WTERMSIG(status) == SIGALRM ? "transition start did not return" : "crash");
		fail();
	}
	assert_true(WIFEXITED(status));
	assert_int_equal(WEXITSTATUS(status), 0);
}

static void nested_recursive_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(NESTED_RECURSIVE);
}

static void nested_acyclic_destination_starts(void **state)
{
	(void)state;
	run_in_child(NESTED_ACYCLIC);
}

static void self_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(SELF);
}

static void empty_self_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(EMPTY_SELF);
}

static void indirect_recursive_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(INDIRECT_RECURSIVE);
}

static void nonleaf_starting_transition_is_rejected(void **state)
{
	(void)state;
	run_in_child(NONLEAF_RECURSIVE);
}

static void active_state_survives_rejection_and_reuse(void **state)
{
	(void)state;
	run_in_child(ACTIVE_REUSE);
}

static void callback_recursive_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(CALLBACK_RECURSIVE);
}

static void callback_indirect_destination_is_rejected(void **state)
{
	(void)state;
	run_in_child(CALLBACK_INDIRECT);
}

static void callback_active_destination_is_preserved(void **state)
{
	(void)state;
	run_in_child(CALLBACK_ACTIVE);
}

static void callback_zero_duration_survives_rejection(void **state)
{
	(void)state;
	run_in_child(CALLBACK_TIMING_REJECTED);
}

static void callback_long_duration_survives_rejection(void **state)
{
	(void)state;
	run_in_child(CALLBACK_LONG_TIMING_REJECTED);
}

static void callback_clock_survives_same_destination_start(void **state)
{
	(void)state;
	run_in_child(CALLBACK_TIMING_SAME_DEST);
}

static void callback_clock_restarts_for_new_destination(void **state)
{
	(void)state;
	run_in_child(CALLBACK_TIMING_NEW_DEST);
}

static void null_destination_start_restarts_clock(void **state)
{
	(void)state;
	run_in_child(NULL_DEST_TIMING);
}

static void render_during_admission_keeps_new_transition(void **state)
{
	(void)state;
	run_in_child(RENDER_RACE);
}

static void timed_direct_render_during_admission_uses_new_clock(void **state)
{
	(void)state;
	run_in_child(RENDER_RACE_TIME_DIRECT);
}

static void direct_render_during_admission_keeps_new_transition(void **state)
{
	(void)state;
	run_in_child(RENDER_RACE_DIRECT);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(nested_recursive_destination_is_rejected),
		cmocka_unit_test(nested_acyclic_destination_starts),
		cmocka_unit_test(self_destination_is_rejected),
		cmocka_unit_test(empty_self_destination_is_rejected),
		cmocka_unit_test(indirect_recursive_destination_is_rejected),
		cmocka_unit_test(nonleaf_starting_transition_is_rejected),
		cmocka_unit_test(active_state_survives_rejection_and_reuse),
		cmocka_unit_test(callback_recursive_destination_is_rejected),
		cmocka_unit_test(callback_indirect_destination_is_rejected),
		cmocka_unit_test(callback_active_destination_is_preserved),
		cmocka_unit_test(callback_zero_duration_survives_rejection),
		cmocka_unit_test(callback_long_duration_survives_rejection),
		cmocka_unit_test(callback_clock_survives_same_destination_start),
		cmocka_unit_test(callback_clock_restarts_for_new_destination),
		cmocka_unit_test(null_destination_start_restarts_clock),
		cmocka_unit_test(render_during_admission_keeps_new_transition),
		cmocka_unit_test(timed_direct_render_during_admission_uses_new_clock),
		cmocka_unit_test(direct_render_during_admission_keeps_new_transition),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
