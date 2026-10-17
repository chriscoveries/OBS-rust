/* obs-hostlib-check: run by AppRun before OBS (obs-rust/obs-studio#959).
 * Finds every *required* host library from host-libs.json the way the shims
 * find optional ones. When one is missing, OBS could not even start, so say
 * which, and how to install it on this distribution, then exit 127:
 * on stderr, and in a desktop notification or dialog when one can be shown
 * (a dock launch has no terminal).
 *   obs-hostlib-check            check; silent when everything is there
 *   obs-hostlib-check --trace    print every candidate of every library */
#define _GNU_SOURCE
#include "hostlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct required_lib {
	struct hostlib_spec spec;
	/* install hints per distribution family */
	const char *debian, *fedora, *arch, *suse, *nixos;
};

#include HOSTLIB_REQUIRED_HEADER /* static const struct required_lib required[] = {...}; */

/* debian, fedora, arch, suse or nixos, from /etc/os-release ID and ID_LIKE. */
static const char *distro_family(void)
{
	static char family[16];
	char line[256], ids[512] = "";
	FILE *f = fopen("/etc/os-release", "r");
	if (!f)
		return "";
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, "ID=", 3) || !strncmp(line, "ID_LIKE=", 8))
			strncat(ids, strchr(line, '=') + 1, sizeof(ids) - strlen(ids) - 1);
	fclose(f);
	static const char *const families[][2] = {
		{"nixos", "nixos"}, {"debian", "debian"}, {"ubuntu", "debian"}, {"fedora", "fedora"},
		{"rhel", "fedora"},  {"centos", "fedora"}, {"arch", "arch"},     {"suse", "suse"},
	};
	for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); i++)
		if (strstr(ids, families[i][0])) {
			snprintf(family, sizeof(family), "%s", families[i][1]);
			return family;
		}
	return "";
}

static const char *hint_for(const struct required_lib *l, const char *family)
{
	if (!strcmp(family, "debian"))
		return l->debian;
	if (!strcmp(family, "fedora"))
		return l->fedora;
	if (!strcmp(family, "arch"))
		return l->arch;
	if (!strcmp(family, "suse"))
		return l->suse;
	if (!strcmp(family, "nixos"))
		return l->nixos;
	return NULL;
}

/* A dialog or notification for a launch without a terminal; best effort. */
static void show(const char *title, const char *text)
{
	static const char *const tools[][4] = {
		{"zenity", "--error", "--title", NULL},
		{"kdialog", "--error", NULL, NULL},
		{"notify-send", "--urgency=critical", NULL, NULL},
	};
	if (isatty(STDERR_FILENO) || (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")))
		return;
	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		pid_t pid = fork();
		if (pid == 0) {
			if (!strcmp(tools[i][0], "zenity"))
				execlp("zenity", "zenity", "--error", "--title", title, "--text", text, (char *)NULL);
			else if (!strcmp(tools[i][0], "kdialog"))
				execlp("kdialog", "kdialog", "--title", title, "--error", text, (char *)NULL);
			else
				execlp("notify-send", "notify-send", "--urgency=critical", title, text, (char *)NULL);
			_exit(127);
		}
		int status = 0;
		if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) != 127)
			return;
	}
}

int main(int argc, char **argv)
{
	int trace = argc > 1 && !strcmp(argv[1], "--trace");
	const char *family = distro_family();
	char msg[8192];
	size_t used = 0, missing = 0;

	used += (size_t)snprintf(msg + used, sizeof(msg) - used,
				 "OBS-Studio-Rust needs these libraries from your system, and they were not found:\n");
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		struct hostlib_result r;
		hostlib_find(&required[i].spec, &r);
		if (trace)
			hostlib_trace(&required[i].spec, &r);
		if (r.handle)
			continue;
		missing++;
		const char *hint = hint_for(&required[i], family);
		used += (size_t)snprintf(msg + used, sizeof(msg) - used, "  %s (%s)", required[i].spec.sonames[0],
					 required[i].spec.feature);
		if (hint)
			used += (size_t)snprintf(msg + used, sizeof(msg) - used, ": %s %s",
						 !strcmp(family, "nixos") ? "" : "install", hint);
		used += (size_t)snprintf(msg + used, sizeof(msg) - used, "\n");
		if (used >= sizeof(msg))
			break;
	}
	if (!missing)
		return 0;
	if (!strcmp(family, "nixos") && used < sizeof(msg))
		snprintf(msg + used, sizeof(msg) - used,
			 "On NixOS, run the AppImage through appimage-run, or enable programs.nix-ld with these "
			 "libraries.\n");
	else if (used < sizeof(msg))
		snprintf(msg + used, sizeof(msg) - used,
			 "Run with OBS_HOSTLIB_DEBUG=1 to see every place that was searched.\n");
	fputs(msg, stderr);
	show("OBS-Studio-Rust cannot start", msg);
	return 127;
}
