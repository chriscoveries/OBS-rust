/* Lifetime conformance for obs_scene_enum_items() when the callback removes
 * scene items.
 *
 * obs_scene_enum_items() walks the scene's item list with the scene locked
 * and takes a reference only on the item it is handing to the callback; it
 * reads the next item's pointer before calling the callback. The scene lock is
 * recursive, so the callback can call obs_sceneitem_remove() on any item of the
 * scene. Removing the current item is fine (it is referenced), but removing a
 * later item drops the scene's only reference to it and frees it, and the walk
 * then moves onto the freed item. Removing two adjacent later items is worse:
 * the first item's saved links still point at the second one.
 *
 * The contract checked here: an item removed during the walk is not passed to
 * the callback afterwards, every item that is still in the scene is passed to
 * it exactly once, and nothing is read through a released item.
 *
 * Moving a later item into or out of a group with
 * obs_sceneitem_group_add_item() / obs_sceneitem_group_remove_item() takes it
 * out of the scene being enumerated without removing it, so it must not be
 * passed to the callback afterwards either (the callback would get the old
 * scene with an item that belongs to another one). The reparent cases below
 * move an item that is not adjacent to the current one, which is also what a
 * walk of the live list gets right.
 *
 * Enumeration walks a snapshot that holds a reference to every item, so those
 * references must all be released (every source is destroyed in the end), a
 * callback returning false must stop it, and an item that is being removed
 * (already marked removed, still in the list while its deselect signal runs)
 * must not be passed to a nested enumeration. An item moved to a group is
 * outside the enumerated scene's lock from then on: another thread may remove
 * it from the group while the walk goes on, so the walk must not read it. That
 * is a data race only a ThreadSanitizer build of libobs reports; the case is
 * built to run it without any sleeps.
 *
 * Each case runs in a child process with its own libobs, so a crash on a
 * released item fails the case instead of the whole test. Core video is not
 * needed. obs_startup() needs a display on Linux, so the cases skip without
 * one (run under xvfb-run). */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <obs.h>
#include <util/base.h>
#include <util/threading.h>

#define ITEM_SOURCE_ID "scene_enum_items_removal_source"
#define ITEM_COUNT 4
#define CHILD_SKIP 77
#define CHILD_TIMEOUT 30

static void quiet_log(int lvl, const char *msg, va_list args, void *p)
{
	(void)lvl;
	(void)msg;
	(void)args;
	(void)p;
}

static const char *item_source_name(void *type_data)
{
	(void)type_data;
	return "item source";
}

static void *item_source_create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;
	return source;
}

/* Sources destroyed in this process; every source of a case must be destroyed
 * once its scene and the enumeration's references are gone. */
static volatile long sources_destroyed;

static void item_source_destroy(void *data)
{
	(void)data;
	os_atomic_inc_long(&sources_destroyed);
}

static uint32_t item_source_size(void *data)
{
	(void)data;
	return 1;
}

static bool start_obs(void)
{
	base_set_log_handler(quiet_log, NULL);
	if (!obs_startup("en-US", NULL, NULL))
		return false;

	struct obs_source_info info = {
		.id = ITEM_SOURCE_ID,
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO,
		.get_name = item_source_name,
		.create = item_source_create,
		.destroy = item_source_destroy,
		.get_width = item_source_size,
		.get_height = item_source_size,
	};
	obs_register_source(&info);
	return true;
}

/* A private scene holding ITEM_COUNT items, in list order items[0] first. */
struct layout {
	obs_scene_t *scene;
	obs_sceneitem_t *items[ITEM_COUNT];
};

static void build_layout(struct layout *l)
{
	l->scene = obs_scene_create_private("enum scene");
	for (size_t i = 0; i < ITEM_COUNT; i++) {
		char name[16];
		snprintf(name, sizeof(name), "item %zu", i);
		obs_source_t *source = obs_source_create_private(ITEM_SOURCE_ID, name, NULL);
		l->items[i] = obs_scene_add(l->scene, source);
		obs_source_release(source);
	}
}

/* What the callback does for the first item it sees. */
struct walk {
	struct layout *layout;
	size_t remove_from; /* remove items[remove_from .. remove_to) ... */
	size_t remove_to;   /* ... from the callback of items[0] */
	size_t visits[ITEM_COUNT];
	size_t total;
};

static bool walk_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct walk *w = param;
	(void)scene;

	w->total++;
	for (size_t i = 0; i < ITEM_COUNT; i++) {
		if (w->layout->items[i] == item)
			w->visits[i]++;
	}

	if (item == w->layout->items[0]) {
		for (size_t i = w->remove_from; i < w->remove_to; i++)
			obs_sceneitem_remove(w->layout->items[i]);
	}
	return true;
}

/* Child exit codes: 0 ok, 1 failed (message on stderr), CHILD_SKIP. */
typedef int (*scenario_fn)(void);

static void run_in_child(scenario_fn fn, const char *what)
{
	fflush(stderr);
	pid_t pid = fork();
	assert_true(pid >= 0);
	if (pid == 0) {
		alarm(CHILD_TIMEOUT);
		if (!start_obs())
			_exit(CHILD_SKIP);
		int ret = fn();
		fflush(stderr);
		_exit(ret);
	}

	int status = 0;
	assert_int_equal(waitpid(pid, &status, 0), pid);
	if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_SKIP)
		skip();
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "%s: the child died from signal %d (%s)\n", what, WTERMSIG(status),
			WTERMSIG(status) == SIGALRM ? "hang" : "crash");
		fail();
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "%s failed\n", what);
		fail();
	}
}

/* Every item still in the scene must have been visited exactly once, removed
 * ones never. */
static int check_walk(const struct walk *w, const char *what)
{
	int ret = 0;
	for (size_t i = 0; i < ITEM_COUNT; i++) {
		bool removed = i >= w->remove_from && i < w->remove_to;
		size_t want = removed ? 0 : 1;
		if (w->visits[i] != want) {
			fprintf(stderr, "%s: item %zu was passed to the callback %zu time(s), expected %zu\n", what, i,
				w->visits[i], want);
			ret = 1;
		}
	}
	size_t want_total = ITEM_COUNT - (w->remove_to - w->remove_from);
	if (w->total != want_total) {
		fprintf(stderr, "%s: the callback ran %zu time(s), expected %zu\n", what, w->total, want_total);
		ret = 1;
	}
	return ret;
}

/* The callback for the first item removes the next item, which only the scene
 * references. */
static int scenario_remove_next(void)
{
	struct layout l;
	build_layout(&l);
	struct walk w = {.layout = &l, .remove_from = 1, .remove_to = 2};
	obs_scene_enum_items(l.scene, walk_cb, &w);
	int ret = check_walk(&w, "remove next item");
	obs_scene_release(l.scene);
	return ret;
}

/* The callback for the first item removes the next two items. */
static int scenario_remove_two_adjacent(void)
{
	struct layout l;
	build_layout(&l);
	struct walk w = {.layout = &l, .remove_from = 1, .remove_to = 3};
	obs_scene_enum_items(l.scene, walk_cb, &w);
	int ret = check_walk(&w, "remove two adjacent items");
	obs_scene_release(l.scene);
	return ret;
}

/* The removed item is also referenced by the caller, so it is not freed, but
 * it is no longer in the scene and must not be walked. */
static int scenario_remove_next_held(void)
{
	struct layout l;
	build_layout(&l);
	obs_sceneitem_addref(l.items[1]);
	struct walk w = {.layout = &l, .remove_from = 1, .remove_to = 2};
	obs_scene_enum_items(l.scene, walk_cb, &w);
	int ret = check_walk(&w, "remove next item held by the caller");
	obs_sceneitem_release(l.items[1]);
	obs_scene_release(l.scene);
	return ret;
}

/* Control: removing only the current item was always safe. */
static bool remove_current_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	size_t *count = param;
	(void)scene;
	(*count)++;
	obs_sceneitem_remove(item);
	return true;
}

static int scenario_remove_current(void)
{
	struct layout l;
	build_layout(&l);
	size_t count = 0;
	obs_scene_enum_items(l.scene, remove_current_cb, &count);
	int ret = 0;
	if (count != ITEM_COUNT) {
		fprintf(stderr, "remove current item: the callback ran %zu time(s), expected %d\n", count, ITEM_COUNT);
		ret = 1;
	}
	obs_scene_release(l.scene);
	return ret;
}

/* Reparenting: a scene [items 0..2, group G], and a group G holding items
 * 0..2 of its own. The callback for the first item moves the last one of the
 * enumerated scene into or out of the group. */
struct group_layout {
	obs_scene_t *scene;
	obs_sceneitem_t *group;
	obs_scene_t *group_scene;
	obs_sceneitem_t *items[3];
	obs_sceneitem_t *moved;
	bool into_group;
	size_t visits[3];
	size_t group_visits;
	size_t total;
};

static obs_sceneitem_t *add_item(obs_scene_t *scene, const char *name)
{
	obs_source_t *source = obs_source_create_private(ITEM_SOURCE_ID, name, NULL);
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	obs_source_release(source);
	return item;
}

/* With items_in_group the three items are in the group, otherwise they are in
 * the scene, in front of the group. */
static void build_group_layout(struct group_layout *g, bool items_in_group)
{
	memset(g, 0, sizeof(*g));
	g->scene = obs_scene_create_private("enum scene");
	if (!items_in_group) {
		g->items[0] = add_item(g->scene, "item 0");
		g->items[1] = add_item(g->scene, "item 1");
		g->items[2] = add_item(g->scene, "item 2");
	}
	g->group = obs_scene_add_group(g->scene, "group");
	g->group_scene = obs_sceneitem_group_get_scene(g->group);
	if (items_in_group) {
		g->items[0] = add_item(g->group_scene, "grouped 0");
		g->items[1] = add_item(g->group_scene, "grouped 1");
		g->items[2] = add_item(g->group_scene, "grouped 2");
	}
}

static bool reparent_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct group_layout *g = param;
	(void)scene;

	g->total++;
	if (item == g->group)
		g->group_visits++;
	for (size_t i = 0; i < 3; i++) {
		if (g->items[i] == item)
			g->visits[i]++;
	}

	if (item == g->items[0]) {
		if (g->into_group)
			obs_sceneitem_group_add_item(g->group, g->items[2]);
		else
			obs_sceneitem_group_remove_item(g->group, g->items[2]);
	}
	return true;
}

/* Items 0 and 1 are walked once, item 2 was moved away and is not, and the
 * group item itself is walked once only when the scene holding it is walked. */
static int check_reparent(const struct group_layout *g, bool group_walked, const char *what)
{
	int ret = 0;
	size_t want[3] = {1, 1, 0};
	for (size_t i = 0; i < 3; i++) {
		if (g->visits[i] != want[i]) {
			fprintf(stderr, "%s: item %zu was passed to the callback %zu time(s), expected %zu\n", what, i,
				g->visits[i], want[i]);
			ret = 1;
		}
	}
	size_t want_group = group_walked ? 1 : 0;
	if (g->group_visits != want_group) {
		fprintf(stderr, "%s: the group item was passed to the callback %zu time(s), expected %zu\n", what,
			g->group_visits, want_group);
		ret = 1;
	}
	if (g->total != 2 + want_group) {
		fprintf(stderr, "%s: the callback ran %zu time(s), expected %zu\n", what, g->total, 2 + want_group);
		ret = 1;
	}
	return ret;
}

static int scenario_scene_move_into_group(void)
{
	struct group_layout g;
	build_group_layout(&g, false);
	g.into_group = true;
	obs_scene_enum_items(g.scene, reparent_cb, &g);
	int ret = check_reparent(&g, true, "move a later item into a group");
	if (obs_sceneitem_get_scene(g.items[2]) != g.group_scene) {
		fprintf(stderr, "move a later item into a group: the item is not in the group\n");
		ret = 1;
	}
	obs_scene_release(g.scene);
	return ret;
}

static int scenario_group_move_out_of_group(void)
{
	struct group_layout g;
	build_group_layout(&g, true);
	obs_sceneitem_group_enum_items(g.group, reparent_cb, &g);
	int ret = check_reparent(&g, false, "move a later item out of a group");
	if (obs_sceneitem_get_scene(g.items[2]) != g.scene) {
		fprintf(stderr, "move a later item out of a group: the item is not in the scene\n");
		ret = 1;
	}
	obs_scene_release(g.scene);
	return ret;
}

static int scenario_scene_of_group_move_out_of_group(void)
{
	struct group_layout g;
	build_group_layout(&g, true);
	obs_scene_enum_items(g.group_scene, reparent_cb, &g);
	int ret = check_reparent(&g, false, "move a later item out of a group (scene walk)");
	obs_scene_release(g.scene);
	return ret;
}

static void move_later_item_into_group(void **state)
{
	(void)state;
	run_in_child(scenario_scene_move_into_group, "move a later item into a group");
}

static void move_later_item_out_of_group_group_enum(void **state)
{
	(void)state;
	run_in_child(scenario_group_move_out_of_group, "move a later item out of a group");
}

static void move_later_item_out_of_group_scene_enum(void **state)
{
	(void)state;
	run_in_child(scenario_scene_of_group_move_out_of_group, "move a later item out of a group (scene walk)");
}

/* The snapshot's references are released: the callback removes a later item
 * (whose last reference is then the snapshot's) and every source is destroyed
 * once the scene is released. */
static void destruction_barrier(void *param)
{
	(void)param;
}

static int scenario_references_released(void)
{
	struct layout l;
	build_layout(&l);
	struct walk w = {.layout = &l, .remove_from = 1, .remove_to = 3};
	obs_scene_enum_items(l.scene, walk_cb, &w);
	int ret = check_walk(&w, "snapshot references");
	/* the main canvas holds a reference to its scenes until they are removed */
	obs_source_remove(obs_scene_get_source(l.scene));
	obs_scene_release(l.scene);
	/* Deferred destruction runs on the destroy queue; each barrier waits
	 * for the tasks queued before it. */
	for (int depth = 0; depth < 3; depth++)
		obs_queue_task(OBS_TASK_DESTROY, destruction_barrier, NULL, true);
	long destroyed = os_atomic_load_long(&sources_destroyed);
	if (destroyed != ITEM_COUNT) {
		fprintf(stderr, "snapshot references: %ld of %d item sources were destroyed\n", destroyed, ITEM_COUNT);
		ret = 1;
	}
	return ret;
}

/* A callback that removes later items and returns false ends the enumeration. */
static bool stop_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct walk *w = param;
	(void)scene;
	(void)item;
	w->total++;
	obs_sceneitem_remove(w->layout->items[2]);
	obs_sceneitem_remove(w->layout->items[3]);
	return false;
}

static int scenario_callback_false_stops(void)
{
	struct layout l;
	build_layout(&l);
	struct walk w = {.layout = &l};
	obs_scene_enum_items(l.scene, stop_cb, &w);
	int ret = 0;
	if (w.total != 1) {
		fprintf(stderr, "callback returning false: the callback ran %zu time(s), expected 1\n", w.total);
		ret = 1;
	}
	obs_scene_release(l.scene);
	return ret;
}

/* An item being removed is marked removed before its deselect signal runs and
 * is detached only afterwards. An enumeration started from that signal must
 * not pass it to its callback. */
struct signal_walk {
	struct layout *layout;
	size_t signals;
	size_t removed_visits;
	size_t total;
};

static bool signal_inner_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct signal_walk *w = param;
	(void)scene;
	w->total++;
	if (item == w->layout->items[0])
		w->removed_visits++;
	return true;
}

static void on_item_deselect(void *param, calldata_t *data)
{
	struct signal_walk *w = param;
	(void)data;
	w->signals++;
	obs_scene_enum_items(w->layout->scene, signal_inner_cb, w);
}

static bool remove_first_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct layout *l = param;
	(void)scene;
	if (item == l->items[0])
		obs_sceneitem_remove(item);
	return true;
}

static int scenario_removal_signal_reentry(void)
{
	struct layout l;
	build_layout(&l);
	obs_sceneitem_select(l.items[0], true);
	struct signal_walk w = {.layout = &l};
	signal_handler_t *handler = obs_source_get_signal_handler(obs_scene_get_source(l.scene));
	signal_handler_connect(handler, "item_deselect", on_item_deselect, &w);
	obs_scene_enum_items(l.scene, remove_first_cb, &l);
	signal_handler_disconnect(handler, "item_deselect", on_item_deselect, &w);
	int ret = 0;
	if (w.signals != 1 || w.removed_visits != 0 || w.total != ITEM_COUNT - 1) {
		fprintf(stderr, "item_deselect re-entry: signals=%zu removed_visits=%zu total=%zu, expected 1/0/%d\n",
			w.signals, w.removed_visits, w.total, ITEM_COUNT - 1);
		ret = 1;
	}
	obs_scene_release(l.scene);
	return ret;
}

/* Another thread removes an item from the group it was just moved to while
 * the enumeration of the scene it left is still walking its snapshot. The
 * hand-off is a flag and a condition variable, so the order is bounded and
 * there are no sleeps; nothing orders the worker's removal against the walk's
 * later reads, which is the point. */
struct concurrent_walk {
	struct group_layout layout;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool go;
	size_t visits;
};

static void *remove_from_group(void *param)
{
	struct concurrent_walk *w = param;
	pthread_mutex_lock(&w->lock);
	while (!w->go)
		pthread_cond_wait(&w->cond, &w->lock);
	pthread_mutex_unlock(&w->lock);
	obs_sceneitem_remove(w->layout.items[2]);
	return NULL;
}

static bool concurrent_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	struct concurrent_walk *w = param;
	(void)scene;
	w->visits++;
	if (item == w->layout.items[0]) {
		obs_sceneitem_group_add_item(w->layout.group, w->layout.items[2]);
		pthread_mutex_lock(&w->lock);
		w->go = true;
		pthread_cond_signal(&w->cond);
		pthread_mutex_unlock(&w->lock);
	}
	return true;
}

static int scenario_concurrent_removal_from_group(void)
{
	struct concurrent_walk w;
	memset(&w, 0, sizeof(w));
	pthread_mutex_init(&w.lock, NULL);
	pthread_cond_init(&w.cond, NULL);
	build_group_layout(&w.layout, false);
	/* the caller's reference keeps the item allocated for the worker */
	obs_sceneitem_addref(w.layout.items[2]);

	pthread_t worker;
	if (pthread_create(&worker, NULL, remove_from_group, &w) != 0) {
		fprintf(stderr, "could not start the worker thread\n");
		return 1;
	}
	obs_scene_enum_items(w.layout.scene, concurrent_cb, &w);
	pthread_join(worker, NULL);

	int ret = 0;
	/* items 0 and 1 and the group; item 2 left the scene */
	if (w.visits != 3) {
		fprintf(stderr, "concurrent removal from a group: the callback ran %zu time(s), expected 3\n",
			w.visits);
		ret = 1;
	}
	obs_sceneitem_release(w.layout.items[2]);
	obs_scene_release(w.layout.scene);
	pthread_cond_destroy(&w.cond);
	pthread_mutex_destroy(&w.lock);
	return ret;
}

static void snapshot_references_are_released(void **state)
{
	(void)state;
	run_in_child(scenario_references_released, "snapshot references");
}

static void callback_false_stops_enumeration(void **state)
{
	(void)state;
	run_in_child(scenario_callback_false_stops, "callback returning false");
}

static void removal_signal_reentry_skips_removed_item(void **state)
{
	(void)state;
	run_in_child(scenario_removal_signal_reentry, "item_deselect re-entry");
}

static void concurrent_removal_from_group(void **state)
{
	(void)state;
	run_in_child(scenario_concurrent_removal_from_group, "concurrent removal from a group");
}

static void remove_next_item(void **state)
{
	(void)state;
	run_in_child(scenario_remove_next, "remove next item");
}

static void remove_two_adjacent_items(void **state)
{
	(void)state;
	run_in_child(scenario_remove_two_adjacent, "remove two adjacent items");
}

static void remove_next_item_held_by_caller(void **state)
{
	(void)state;
	run_in_child(scenario_remove_next_held, "remove next item held by the caller");
}

static void remove_current_item(void **state)
{
	(void)state;
	run_in_child(scenario_remove_current, "remove current item");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(remove_next_item_held_by_caller),
		cmocka_unit_test(remove_next_item),
		cmocka_unit_test(remove_two_adjacent_items),
		cmocka_unit_test(remove_current_item),
		cmocka_unit_test(move_later_item_into_group),
		cmocka_unit_test(move_later_item_out_of_group_group_enum),
		cmocka_unit_test(move_later_item_out_of_group_scene_enum),
		cmocka_unit_test(snapshot_references_are_released),
		cmocka_unit_test(callback_false_stops_enumeration),
		cmocka_unit_test(removal_signal_reentry_skips_removed_item),
		cmocka_unit_test(concurrent_removal_from_group),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
