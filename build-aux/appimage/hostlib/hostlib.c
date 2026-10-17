/* See hostlib.h. */
#define _GNU_SOURCE
#include "hostlib.h"

#include <ctype.h>
#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const search_dirs[] = {
	"/run/opengl-driver/lib", /* NixOS: the driver closure */
	"/usr/lib/x86_64-linux-gnu",
	"/usr/lib64",
	"/usr/lib",
	"/lib64",
	"/lib",
	NULL,
};

/* The AppImage's mount point (AppRun exports APPDIR): never a host library. */
static int inside_appdir(const char *path)
{
	const char *appdir = getenv("APPDIR");
	size_t n;
	if (!appdir || !*appdir || !path)
		return 0;
	n = strlen(appdir);
	return strncmp(path, appdir, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

static struct hostlib_try *add_try(struct hostlib_result *r, const char *path)
{
	struct hostlib_try *t;
	if (r->ntries >= HOSTLIB_MAX_TRIES)
		return NULL;
	t = &r->tries[r->ntries++];
	snprintf(t->path, sizeof(t->path), "%s", path);
	t->reason[0] = '\0';
	return t;
}

/* Load one candidate and check it. Returns the handle when accepted. */
static void *try_candidate(const struct hostlib_spec *spec, struct hostlib_result *r, const char *path)
{
	struct hostlib_try *t = add_try(r, path);
	struct link_map *map = NULL;
	void *h;
	char real[512];

	/* A directory candidate that does not exist is not worth a line. */
	if (strchr(path, '/') && access(path, F_OK) != 0) {
		if (t)
			r->ntries--;
		return NULL;
	}
	h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	if (!h) {
		const char *e = dlerror();
		if (t)
			snprintf(t->reason, sizeof(t->reason), "does not load: %s", e ? e : "?");
		return NULL;
	}
	real[0] = '\0';
	if (dlinfo(h, RTLD_DI_LINKMAP, &map) == 0 && map && map->l_name)
		snprintf(real, sizeof(real), "%s", map->l_name);
	if (inside_appdir(real[0] ? real : path)) {
		if (t)
			snprintf(t->reason, sizeof(t->reason), "is inside the AppImage (%s)", real);
		dlclose(h);
		return NULL;
	}
	for (const char *const *s = spec->symbols; s && *s; s++) {
		dlerror();
		if (!dlsym(h, *s)) {
			if (t)
				snprintf(t->reason, sizeof(t->reason), "lacks %s (too old?)", *s);
			dlclose(h);
			return NULL;
		}
	}
	snprintf(r->path, sizeof(r->path), "%s", real[0] ? real : path);
	return h;
}

void *hostlib_find(const struct hostlib_spec *spec, struct hostlib_result *r)
{
	char env[128], path[512];
	const char *override;
	size_t i;

	memset(r, 0, sizeof(*r));

	snprintf(env, sizeof(env), "OBS_HOSTLIB_%s", spec->name);
	for (i = strlen("OBS_HOSTLIB_"); env[i]; i++)
		env[i] = env[i] == '-' ? '_' : (char)toupper((unsigned char)env[i]);
	override = getenv(env);
	if (override && *override) {
		r->handle = try_candidate(spec, r, override);
		/* An explicit choice that fails is reported, never silently replaced. */
		return r->handle;
	}

	for (const char *const *so = spec->sonames; *so; so++) {
		if ((r->handle = try_candidate(spec, r, *so)))
			return r->handle;
		for (const char *const *d = search_dirs; *d; d++) {
			snprintf(path, sizeof(path), "%s/%s", *d, *so);
			if ((r->handle = try_candidate(spec, r, path)))
				return r->handle;
		}
	}
	return NULL;
}

void hostlib_describe(const struct hostlib_spec *spec, const struct hostlib_result *r, char *buf, size_t size)
{
	size_t used;
	if (r->handle) {
		snprintf(buf, size, "hostlib: %s -> %s", spec->name, r->path);
		return;
	}
	used = (size_t)snprintf(buf, size, "hostlib: %s unavailable (%s): searched %zu candidates", spec->name,
				spec->feature, r->ntries);
	for (size_t i = 0; i < r->ntries && used < size; i++)
		used += (size_t)snprintf(buf + used, size - used, "%s %s (%s)", i ? ";" : ":", r->tries[i].path,
					 r->tries[i].reason);
}

void hostlib_trace(const struct hostlib_spec *spec, const struct hostlib_result *r)
{
	fprintf(stderr, "hostlib: %s (%s):\n", spec->name, spec->feature);
	for (size_t i = 0; i < r->ntries; i++)
		fprintf(stderr, "  %s: %s\n", r->tries[i].path,
			r->tries[i].reason[0] ? r->tries[i].reason : "accepted");
	if (!r->ntries)
		fprintf(stderr, "  (no candidates)\n");
}
