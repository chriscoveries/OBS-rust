/* Lifetime conformance for the public obs_enum_outputs(), obs_enum_encoders()
 * and obs_enum_services(). Enumeration walks a snapshot of referenced
 * contexts, so a callback may release the reference it owns for the current
 * entry. Each scenario runs in a forked child: on an unfixed obs_enum the
 * release frees the entry being walked, and ASan's heap-use-after-free report
 * (or a crash) must not take the rest of cmocka down with it.
 *
 * Every scenario also checks destruction through a weak reference taken at
 * creation (an encoder's destroy callback only runs once it was initialized,
 * so the callbacks cannot count it):
 *  - the object the callback releases is still alive inside that callback and
 *    is destroyed once the walk returns, and
 *  - once the walk returns, the snapshot references are gone, so dropping the
 *    owner references destroys every object (a walk that never releases its
 *    snapshot leaves them alive).
 * The callback is also exercised for releasing the first and the last visited
 * entry, and for stopping the walk early.
 *
 * A separate scenario enumerates encoders while one of them is being
 * destroyed: it is still listed but its refcount is already 0, so the walk
 * must skip it rather than hand the dying encoder (or NULL) to the callback. */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <obs.h>
#include <obs-encoder.h>
#include <obs-output.h>
#include <obs-service.h>
#include <util/base.h>
#include <util/bmem.h>

#define OBJECT_COUNT 3
#define CHILD_SKIP 77
#define CHILD_TIMEOUT 30

enum kind { KIND_OUTPUT, KIND_ENCODER, KIND_SERVICE };

static void quiet_log(int lvl, const char *msg, va_list args, void *p)
{
	(void)lvl;
	(void)msg;
	(void)args;
	(void)p;
}

static bool start_obs(void)
{
	base_set_log_handler(quiet_log, NULL);
	return obs_startup("en-US", NULL, NULL);
}

static const char *test_get_name(void *type_data)
{
	(void)type_data;
	return "enum removal test object";
}

static void *test_output_create(obs_data_t *settings, obs_output_t *output)
{
	(void)settings;
	(void)output;
	return bmalloc(1);
}

static void test_output_destroy(void *data)
{
	bfree(data);
}

static bool test_output_start(void *data)
{
	(void)data;
	return true;
}

static void test_output_stop(void *data, uint64_t ts)
{
	(void)data;
	(void)ts;
}

static void test_output_packet(void *data, struct encoder_packet *packet)
{
	(void)data;
	(void)packet;
}

static void *test_encoder_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	(void)settings;
	(void)encoder;
	return bmalloc(1);
}

static void test_encoder_destroy(void *data)
{
	bfree(data);
}

static bool test_encoder_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet,
				bool *received_packet)
{
	(void)data;
	(void)frame;
	(void)packet;
	*received_packet = false;
	return true;
}

static void *test_service_create(obs_data_t *settings, obs_service_t *service)
{
	(void)settings;
	(void)service;
	return bmalloc(1);
}

static void test_service_destroy(void *data)
{
	bfree(data);
}

/* obs_service_create() refuses a service without a protocol. */
static const char *test_service_get_protocol(void *data)
{
	(void)data;
	return "RTMP";
}

static const struct obs_output_info test_output_info = {
	.id = "enum_removal_test_output",
	.flags = OBS_OUTPUT_ENCODED,
	.get_name = test_get_name,
	.create = test_output_create,
	.destroy = test_output_destroy,
	.start = test_output_start,
	.stop = test_output_stop,
	.encoded_packet = test_output_packet,
};

static const struct obs_encoder_info test_encoder_info = {
	.id = "enum_removal_test_encoder",
	.type = OBS_ENCODER_VIDEO,
	.codec = "h264",
	.get_name = test_get_name,
	.create = test_encoder_create,
	.destroy = test_encoder_destroy,
	.encode = test_encoder_encode,
};

static const struct obs_service_info test_service_info = {
	.id = "enum_removal_test_service",
	.get_name = test_get_name,
	.create = test_service_create,
	.destroy = test_service_destroy,
	.get_protocol = test_service_get_protocol,
};

/* The three enumerations only differ in the pointer type they pass around. */
struct walk {
	enum kind kind;
	void *owned[OBJECT_COUNT];
	/* Weak references taken at creation; they outlive the objects. */
	void *weak[OBJECT_COUNT];
	size_t visits;
	/* Zero-based index of the visit that acts: it releases its object, or
	 * (stop_at_target) stops the walk. SIZE_MAX never acts. */
	size_t target;
	bool stop_at_target;
	/* Objects already destroyed right after the target's release, inside the
	 * callback. */
	size_t dead_in_callback;
};

static void *get_weak(enum kind kind, void *object)
{
	switch (kind) {
	case KIND_OUTPUT:
		return obs_output_get_weak_output(object);
	case KIND_ENCODER:
		return obs_encoder_get_weak_encoder(object);
	case KIND_SERVICE:
		return obs_service_get_weak_service(object);
	}
	return NULL;
}

static void release_weak(enum kind kind, void *weak)
{
	switch (kind) {
	case KIND_OUTPUT:
		obs_weak_output_release(weak);
		break;
	case KIND_ENCODER:
		obs_weak_encoder_release(weak);
		break;
	case KIND_SERVICE:
		obs_weak_service_release(weak);
		break;
	}
}

/* Whether the weak reference still yields a strong one, i.e. the object has
 * not been destroyed. */
static bool is_alive(enum kind kind, void *weak)
{
	switch (kind) {
	case KIND_OUTPUT: {
		obs_output_t *o = obs_weak_output_get_output(weak);
		obs_output_release(o);
		return o != NULL;
	}
	case KIND_ENCODER: {
		obs_encoder_t *e = obs_weak_encoder_get_encoder(weak);
		obs_encoder_release(e);
		return e != NULL;
	}
	case KIND_SERVICE: {
		obs_service_t *v = obs_weak_service_get_service(weak);
		obs_service_release(v);
		return v != NULL;
	}
	}
	return false;
}

static size_t dead_count(const struct walk *w)
{
	size_t n = 0;
	for (size_t i = 0; i < OBJECT_COUNT; i++)
		n += !is_alive(w->kind, w->weak[i]);
	return n;
}

static void release_object(enum kind kind, void *object)
{
	switch (kind) {
	case KIND_OUTPUT:
		obs_output_release(object);
		break;
	case KIND_ENCODER:
		obs_encoder_release(object);
		break;
	case KIND_SERVICE:
		obs_service_release(object);
		break;
	}
}

static bool visit(struct walk *w, void *object)
{
	size_t slot = OBJECT_COUNT;
	for (size_t i = 0; i < OBJECT_COUNT; i++) {
		if (w->owned[i] == object)
			slot = i;
	}
	if (slot == OBJECT_COUNT)
		return true; /* not ours */

	size_t index = w->visits++;
	if (index != w->target)
		return true;
	if (w->stop_at_target)
		return false;

	w->owned[slot] = NULL;
	release_object(w->kind, object);
	w->dead_in_callback = dead_count(w);
	return true;
}

static bool visit_output(void *param, obs_output_t *output)
{
	return visit(param, output);
}

static bool visit_encoder(void *param, obs_encoder_t *encoder)
{
	return visit(param, encoder);
}

static bool visit_service(void *param, obs_service_t *service)
{
	return visit(param, service);
}

static int create_objects(struct walk *w)
{
	obs_register_output(&test_output_info);
	obs_register_encoder(&test_encoder_info);
	obs_register_service(&test_service_info);

	for (size_t i = 0; i < OBJECT_COUNT; i++) {
		char name[32];
		snprintf(name, sizeof(name), "enum object %zu", i);
		switch (w->kind) {
		case KIND_OUTPUT:
			w->owned[i] = obs_output_create(test_output_info.id, name, NULL, NULL);
			break;
		case KIND_ENCODER:
			w->owned[i] = obs_video_encoder_create(test_encoder_info.id, name, NULL, NULL);
			break;
		case KIND_SERVICE:
			w->owned[i] = obs_service_create(test_service_info.id, name, NULL, NULL);
			break;
		}
		if (!w->owned[i]) {
			fprintf(stderr, "failed to create object %zu\n", i);
			return 1;
		}
		w->weak[i] = get_weak(w->kind, w->owned[i]);
	}
	return 0;
}

static void enumerate(struct walk *w)
{
	switch (w->kind) {
	case KIND_OUTPUT:
		obs_enum_outputs(visit_output, w);
		break;
	case KIND_ENCODER:
		obs_enum_encoders(visit_encoder, w);
		break;
	case KIND_SERVICE:
		obs_enum_services(visit_service, w);
		break;
	}
}

static size_t owned_count(const struct walk *w)
{
	size_t n = 0;
	for (size_t i = 0; i < OBJECT_COUNT; i++)
		n += w->owned[i] != NULL;
	return n;
}

#define CHECK(cond, ...)                          \
	do {                                      \
		if (!(cond)) {                    \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr);      \
			ret = 1;                  \
		}                                 \
	} while (0)

/* Walk with `target` releasing its object (or stopping the walk). */
static int scenario(enum kind kind, size_t target, bool stop_at_target)
{
	struct walk w = {.kind = kind, .target = target, .stop_at_target = stop_at_target};
	int ret = create_objects(&w);
	if (ret)
		return ret;

	enumerate(&w);

	if (stop_at_target) {
		CHECK(w.visits == target + 1, "early stop: visited %zu objects, expected %zu", w.visits, target + 1);
		CHECK(dead_count(&w) == 0, "early stop: %zu objects destroyed with every owner still holding",
		      dead_count(&w));
	} else {
		CHECK(w.visits == OBJECT_COUNT, "visited %zu objects, expected %d", w.visits, OBJECT_COUNT);
		/* The snapshot reference keeps the object alive through its own
		 * callback; the walk drops it afterwards. */
		CHECK(w.dead_in_callback == 0, "object destroyed inside the callback that released it");
		CHECK(dead_count(&w) == 1,
		      "after the walk %zu objects destroyed, expected 1 (snapshot reference leaked)", dead_count(&w));
		CHECK(owned_count(&w) == OBJECT_COUNT - 1, "bookkeeping error: %zu objects still owned",
		      owned_count(&w));
	}

	for (size_t i = 0; i < OBJECT_COUNT; i++) {
		if (w.owned[i])
			release_object(kind, w.owned[i]);
	}
	/* Owner references are all gone, so nothing may survive the walk. */
	CHECK(dead_count(&w) == OBJECT_COUNT,
	      "%zu of %d objects destroyed after the last release (snapshot reference leaked)", dead_count(&w),
	      OBJECT_COUNT);
	for (size_t i = 0; i < OBJECT_COUNT; i++)
		release_weak(kind, w.weak[i]);
	return ret;
}

/* obs_encoder_destroy() logs this (format string, not the formatted message)
 * after the encoder's refcount reached 0 and before it is unlinked from the
 * encoder list (obs-encoder.c). The scenario below relies on that literal. */
#define ENCODER_DESTROYED_FORMAT "encoder '%s' destroyed"

struct dying_walk {
	obs_encoder_t *keep;
	obs_encoder_t *dying;
	/* Set while the dying encoder is being released. */
	bool armed;
	size_t enums_in_destroy;
	size_t saw_keep;
	size_t saw_dying;
	size_t saw_null;
	log_handler_t prev_handler;
	void *prev_param;
};

static bool visit_dying(void *param, obs_encoder_t *encoder)
{
	struct dying_walk *d = param;
	if (!encoder)
		d->saw_null++;
	else if (encoder == d->dying)
		d->saw_dying++;
	else if (encoder == d->keep)
		d->saw_keep++;
	return true;
}

/* Enumerates encoders at the one point where the dying encoder is still listed
 * at refcount 0, then forwards to the handler it replaced. */
static void dying_log(int lvl, const char *msg, va_list args, void *p)
{
	struct dying_walk *d = p;
	if (d->armed && strcmp(msg, ENCODER_DESTROYED_FORMAT) == 0) {
		d->armed = false;
		d->enums_in_destroy++;
		obs_enum_encoders(visit_dying, d);
	}
	d->prev_handler(lvl, msg, args, d->prev_param);
}

static int dying_scenario(void)
{
	struct dying_walk d = {0};
	int ret = 0;

	obs_register_encoder(&test_encoder_info);
	d.keep = obs_video_encoder_create(test_encoder_info.id, "enum kept encoder", NULL, NULL);
	d.dying = obs_video_encoder_create(test_encoder_info.id, "enum dying encoder", NULL, NULL);
	if (!d.keep || !d.dying) {
		fprintf(stderr, "failed to create encoders\n");
		return 1;
	}
	obs_weak_encoder_t *weak = obs_encoder_get_weak_encoder(d.dying);

	base_get_log_handler(&d.prev_handler, &d.prev_param);
	base_set_log_handler(dying_log, &d);
	d.armed = true;
	obs_encoder_release(d.dying);
	d.armed = false;
	base_set_log_handler(d.prev_handler, d.prev_param);

	CHECK(d.enums_in_destroy == 1, "destroy log seen %zu times, expected 1 (log format changed?)",
	      d.enums_in_destroy);
	CHECK(d.saw_keep == 1, "live encoder visited %zu times during the destroy, expected 1", d.saw_keep);
	CHECK(d.saw_dying == 0, "callback was handed the encoder being destroyed (%zu times)", d.saw_dying);
	CHECK(d.saw_null == 0, "callback was handed NULL (%zu times)", d.saw_null);
	CHECK(!is_alive(KIND_ENCODER, weak), "released encoder still alive");

	obs_weak_encoder_release(weak);
	obs_encoder_release(d.keep);
	return ret;
}

struct scenario_args {
	enum kind kind;
	size_t target;
	bool stop_at_target;
};

static int run_scenario(const struct scenario_args *a)
{
	if (!a)
		return dying_scenario();
	return scenario(a->kind, a->target, a->stop_at_target);
}

/* NULL args runs the dying-encoder scenario. */
static void run_child(const char *what, const struct scenario_args *args)
{
	fflush(stderr);
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		alarm(CHILD_TIMEOUT);
		if (!start_obs())
			_exit(CHILD_SKIP);
		int ret = run_scenario(args);
		fflush(stderr);
		_exit(ret);
	}

	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_SKIP)
		skip();
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "%s: child died from signal %d (%s)\n", what, WTERMSIG(status),
			WTERMSIG(status) == SIGALRM ? "hang" : "crash");
		fail();
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "%s failed\n", what);
		fail();
	}
}

static void run_in_child(const char *what, enum kind kind, size_t target, bool stop_at_target)
{
	struct scenario_args args = {.kind = kind, .target = target, .stop_at_target = stop_at_target};
	run_child(what, &args);
}

#define LAST (OBJECT_COUNT - 1)

static void test_output_release_first(void **state)
{
	(void)state;
	run_in_child("outputs, release first visited", KIND_OUTPUT, 0, false);
}

static void test_output_release_last(void **state)
{
	(void)state;
	run_in_child("outputs, release last visited", KIND_OUTPUT, LAST, false);
}

static void test_output_early_stop(void **state)
{
	(void)state;
	run_in_child("outputs, early stop", KIND_OUTPUT, 0, true);
}

static void test_encoder_release_first(void **state)
{
	(void)state;
	run_in_child("encoders, release first visited", KIND_ENCODER, 0, false);
}

static void test_encoder_release_last(void **state)
{
	(void)state;
	run_in_child("encoders, release last visited", KIND_ENCODER, LAST, false);
}

static void test_service_release_first(void **state)
{
	(void)state;
	run_in_child("services, release first visited", KIND_SERVICE, 0, false);
}

static void test_service_release_last(void **state)
{
	(void)state;
	run_in_child("services, release last visited", KIND_SERVICE, LAST, false);
}

static void test_encoder_skips_dying(void **state)
{
	(void)state;
	run_child("encoders, enumerate during destroy", NULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_output_release_first), cmocka_unit_test(test_output_release_last),
		cmocka_unit_test(test_output_early_stop),    cmocka_unit_test(test_encoder_release_first),
		cmocka_unit_test(test_encoder_release_last), cmocka_unit_test(test_service_release_first),
		cmocka_unit_test(test_service_release_last), cmocka_unit_test(test_encoder_skips_dying),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
