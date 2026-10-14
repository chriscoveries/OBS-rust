/* Test helper for test-appimage.sh. It is built next to the AppImage and run
 * by the AppImage's own loader and libraries, so its probes exercise exactly
 * what OBS gets at runtime:
 *
 *   probe-pipewire  create a PipeWire context and connect to the daemon
 *   probe-pulse     connect to the PulseAudio server (pipewire-pulse)
 *   probe-gconv     convert text through a gconv module
 *   probe-dbus      talk to the session bus with GDBus
 *   write-tone      write <file.wav> holding <seconds> of a 440 Hz stereo tone
 *                   (the test microphone plays it)
 *   fake-portal     serve org.freedesktop.portal.{ScreenCast,Camera} on the
 *                   session bus, handing out the PipeWire node named in
 *                   OBS_TEST_SCREEN_NODE (the test's stand-in for a desktop)
 *
 * Each probe exits 0 and prints one line when it works; otherwise it prints
 * what failed and exits 1. Build: see .github/workflows/appimage.yaml. */

#include <errno.h>
#include <iconv.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <pulse/pulseaudio.h>

/* ------------------------------------------------------------------ */
/* probe-pipewire                                                       */

struct pw_probe {
	struct pw_main_loop *loop;
	struct pw_core *core;
	int sync;
	bool done;
	char version[64];
	int error;
};

static void on_core_info(void *data, const struct pw_core_info *info)
{
	struct pw_probe *p = data;

	snprintf(p->version, sizeof(p->version), "%s", info->version ? info->version : "?");
}

static void on_core_done(void *data, uint32_t id, int seq)
{
	struct pw_probe *p = data;

	if (id == PW_ID_CORE && seq == p->sync) {
		p->done = true;
		pw_main_loop_quit(p->loop);
	}
}

static void on_core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	struct pw_probe *p = data;

	(void)seq;
	if (id == PW_ID_CORE) {
		fprintf(stderr, "probe-pipewire: core error %d: %s\n", res, message);
		p->error = res;
		pw_main_loop_quit(p->loop);
	}
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.info = on_core_info,
	.done = on_core_done,
	.error = on_core_error,
};

static void on_pw_timer(void *data, uint64_t expirations)
{
	struct pw_probe *p = data;

	(void)expirations;
	fprintf(stderr, "probe-pipewire: no reply from the daemon within 5 s\n");
	pw_main_loop_quit(p->loop);
}

static int probe_pipewire(void)
{
	struct pw_probe p = {0};
	struct pw_context *context;
	struct spa_hook core_listener;
	struct spa_source *timer;
	struct timespec timeout = {5, 0};

	pw_init(NULL, NULL);
	p.loop = pw_main_loop_new(NULL);
	if (!p.loop) {
		fprintf(stderr, "probe-pipewire: pw_main_loop_new failed: %s\n", strerror(errno));
		return 1;
	}
	/* This is the call that returned NULL in obs-rust/obs-studio#426: the
	 * context needs client.conf and the protocol module. */
	context = pw_context_new(pw_main_loop_get_loop(p.loop), NULL, 0);
	if (!context) {
		fprintf(stderr,
			"probe-pipewire: pw_context_new failed: %s "
			"(PIPEWIRE_CONFIG_DIR=%s PIPEWIRE_MODULE_DIR=%s)\n",
			strerror(errno), getenv("PIPEWIRE_CONFIG_DIR") ?: "", getenv("PIPEWIRE_MODULE_DIR") ?: "");
		return 1;
	}
	p.core = pw_context_connect(context, NULL, 0);
	if (!p.core) {
		fprintf(stderr, "probe-pipewire: pw_context_connect failed: %s\n", strerror(errno));
		return 1;
	}
	pw_core_add_listener(p.core, &core_listener, &core_events, &p);
	p.sync = pw_core_sync(p.core, PW_ID_CORE, 0);
	timer = pw_loop_add_timer(pw_main_loop_get_loop(p.loop), on_pw_timer, &p);
	pw_loop_update_timer(pw_main_loop_get_loop(p.loop), timer, &timeout, NULL, false);
	pw_main_loop_run(p.loop);
	if (!p.done || p.error)
		return 1;
	printf("probe-pipewire: connected to PipeWire %s (library %s)\n", p.version, pw_get_library_version());
	return 0;
}

/* ------------------------------------------------------------------ */
/* probe-pulse                                                          */

static void on_server_info(pa_context *c, const pa_server_info *info, void *userdata)
{
	(void)c;
	snprintf(userdata, 128, "%s %s", info->server_name, info->server_version);
}

static int probe_pulse(void)
{
	pa_mainloop *ml = pa_mainloop_new();
	pa_context *ctx = pa_context_new(pa_mainloop_get_api(ml), "obs-appimage-testkit");
	char server[128] = "";
	pa_operation *op = NULL;
	int i;

	if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		fprintf(stderr, "probe-pulse: connect failed: %s\n", pa_strerror(pa_context_errno(ctx)));
		return 1;
	}
	for (i = 0; i < 500; i++) {
		pa_context_state_t state = pa_context_get_state(ctx);

		if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
			fprintf(stderr, "probe-pulse: connection failed: %s\n", pa_strerror(pa_context_errno(ctx)));
			return 1;
		}
		if (state == PA_CONTEXT_READY && !op)
			op = pa_context_get_server_info(ctx, on_server_info, server);
		if (op && pa_operation_get_state(op) == PA_OPERATION_DONE)
			break;
		pa_mainloop_iterate(ml, 0, NULL);
		usleep(10000);
	}
	if (!server[0]) {
		fprintf(stderr, "probe-pulse: no server info within 5 s\n");
		return 1;
	}
	printf("probe-pulse: connected to %s (libpulse %s)\n", server, pa_get_library_version());
	return 0;
}

/* ------------------------------------------------------------------ */
/* probe-gconv                                                          */

static int probe_gconv(void)
{
	/* UTF-8 <-> UTF-8 is built in; a real charset needs a gconv module. */
	iconv_t cd = iconv_open("ISO-8859-15", "UTF-8");
	char in[] = "\xe2\x82\xac", out[8] = {0};
	char *inp = in, *outp = out;
	size_t inleft = 3, outleft = sizeof(out);

	if (cd == (iconv_t)-1) {
		fprintf(stderr, "probe-gconv: iconv_open failed: %s (GCONV_PATH=%s)\n", strerror(errno),
			getenv("GCONV_PATH") ?: "");
		return 1;
	}
	if (iconv(cd, &inp, &inleft, &outp, &outleft) == (size_t)-1 || (unsigned char)out[0] != 0xa4) {
		fprintf(stderr, "probe-gconv: conversion failed\n");
		return 1;
	}
	iconv_close(cd);
	printf("probe-gconv: UTF-8 -> ISO-8859-15 works\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* probe-dbus                                                           */

static int probe_dbus(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	g_autoptr(GVariant) reply = NULL;
	const char *id;

	if (!bus) {
		fprintf(stderr, "probe-dbus: %s\n", error->message);
		return 1;
	}
	reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
					    "org.freedesktop.DBus", "GetId", NULL, G_VARIANT_TYPE("(s)"),
					    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
	if (!reply) {
		fprintf(stderr, "probe-dbus: %s\n", error->message);
		return 1;
	}
	g_variant_get(reply, "(&s)", &id);
	printf("probe-dbus: session bus %s\n", id);
	return 0;
}

/* ------------------------------------------------------------------ */
/* write-tone                                                           */

static void put_le(FILE *f, uint32_t v, int bytes)
{
	for (int i = 0; i < bytes; i++)
		fputc((v >> (8 * i)) & 0xff, f);
}

static int write_tone(const char *path, int seconds)
{
	const uint32_t rate = 48000, frames = rate * (uint32_t)seconds, data = frames * 4;
	FILE *f = fopen(path, "wb");

	if (!f || seconds <= 0) {
		fprintf(stderr, "write-tone: cannot write %s\n", path);
		return 1;
	}
	fputs("RIFF", f);
	put_le(f, 36 + data, 4);
	fputs("WAVEfmt ", f);
	put_le(f, 16, 4);
	put_le(f, 1, 2); /* PCM */
	put_le(f, 2, 2); /* channels */
	put_le(f, rate, 4);
	put_le(f, rate * 4, 4); /* bytes per second */
	put_le(f, 4, 2);        /* block align */
	put_le(f, 16, 2);       /* bits per sample */
	fputs("data", f);
	put_le(f, data, 4);
	for (uint32_t i = 0; i < frames; i++) {
		int16_t v = (int16_t)(12000 * sin(2 * M_PI * 440 * i / rate));

		put_le(f, (uint16_t)v, 2);
		put_le(f, (uint16_t)v, 2);
	}
	if (fclose(f) != 0)
		return 1;
	printf("write-tone: %s, %d s of 440 Hz\n", path, seconds);
	return 0;
}

/* ------------------------------------------------------------------ */
/* fake-portal                                                          */

static const char portal_xml[] = "<node>"
				 " <interface name='org.freedesktop.portal.ScreenCast'>"
				 "  <method name='CreateSession'>"
				 "   <arg type='a{sv}' direction='in'/><arg type='o' direction='out'/>"
				 "  </method>"
				 "  <method name='SelectSources'>"
				 "   <arg type='o' direction='in'/><arg type='a{sv}' direction='in'/>"
				 "   <arg type='o' direction='out'/>"
				 "  </method>"
				 "  <method name='Start'>"
				 "   <arg type='o' direction='in'/><arg type='s' direction='in'/>"
				 "   <arg type='a{sv}' direction='in'/><arg type='o' direction='out'/>"
				 "  </method>"
				 "  <method name='OpenPipeWireRemote'>"
				 "   <arg type='o' direction='in'/><arg type='a{sv}' direction='in'/>"
				 "   <arg type='h' direction='out'/>"
				 "  </method>"
				 "  <property name='AvailableSourceTypes' type='u' access='read'/>"
				 "  <property name='AvailableCursorModes' type='u' access='read'/>"
				 "  <property name='version' type='u' access='read'/>"
				 " </interface>"
				 " <interface name='org.freedesktop.portal.Camera'>"
				 "  <method name='AccessCamera'>"
				 "   <arg type='a{sv}' direction='in'/><arg type='o' direction='out'/>"
				 "  </method>"
				 "  <method name='OpenPipeWireRemote'>"
				 "   <arg type='a{sv}' direction='in'/><arg type='h' direction='out'/>"
				 "  </method>"
				 "  <property name='IsCameraPresent' type='b' access='read'/>"
				 "  <property name='version' type='u' access='read'/>"
				 " </interface>"
				 "</node>";

static uint32_t screen_node;
static int screen_width = 320, screen_height = 240;

static int connect_pipewire_socket(void)
{
	const char *dir = getenv("PIPEWIRE_RUNTIME_DIR") ?: getenv("XDG_RUNTIME_DIR");
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	int fd;

	if (!dir)
		return -1;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/pipewire-0", dir);
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void portal_method_call(GDBusConnection *connection, const char *sender, const char *object_path,
			       const char *interface_name, const char *method_name, GVariant *parameters,
			       GDBusMethodInvocation *invocation, gpointer user_data)
{
	g_autoptr(GVariant) options = NULL;
	g_autofree char *sender_part = g_strdup(sender + 1);
	g_autofree char *request_path = NULL;
	const char *handle_token = "t";
	GVariantBuilder results;

	(void)object_path;
	(void)user_data;
	g_strdelimit(sender_part, ".", '_');
	g_printerr("fake-portal: %s.%s\n", interface_name, method_name);

	if (strcmp(method_name, "OpenPipeWireRemote") == 0) {
		g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new();
		int fd = connect_pipewire_socket();
		int index = fd >= 0 ? g_unix_fd_list_append(fd_list, fd, NULL) : -1;

		if (fd >= 0)
			close(fd);
		if (index < 0) {
			g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.portal.Error.Failed",
								   "no PipeWire socket");
			return;
		}
		g_dbus_method_invocation_return_value_with_unix_fd_list(invocation, g_variant_new("(h)", index),
									fd_list);
		return;
	}

	options = g_variant_get_child_value(parameters, g_variant_n_children(parameters) - 1);
	g_variant_lookup(options, "handle_token", "&s", &handle_token);
	request_path = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender_part, handle_token);
	g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", request_path));

	g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
	if (strcmp(method_name, "CreateSession") == 0) {
		const char *session_token = "s";
		g_autofree char *session_path = NULL;

		g_variant_lookup(options, "session_handle_token", "&s", &session_token);
		session_path =
			g_strdup_printf("/org/freedesktop/portal/desktop/session/%s/%s", sender_part, session_token);
		g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string(session_path));
	} else if (strcmp(method_name, "Start") == 0) {
		GVariantBuilder streams, props;

		g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&props, "{sv}", "size", g_variant_new("(ii)", screen_width, screen_height));
		g_variant_builder_add(&props, "{sv}", "source_type", g_variant_new_uint32(1));
		g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
		g_variant_builder_add(&streams, "(u@a{sv})", screen_node, g_variant_builder_end(&props));
		g_variant_builder_add(&results, "{sv}", "streams", g_variant_builder_end(&streams));
		g_variant_builder_add(&results, "{sv}", "restore_token", g_variant_new_string("appimage-test"));
	}
	g_dbus_connection_emit_signal(connection, sender, request_path, "org.freedesktop.portal.Request", "Response",
				      g_variant_new("(u@a{sv})", 0, g_variant_builder_end(&results)), NULL);
}

static GVariant *portal_get_property(GDBusConnection *connection, const char *sender, const char *object_path,
				     const char *interface_name, const char *property_name, GError **error,
				     gpointer user_data)
{
	(void)connection;
	(void)sender;
	(void)object_path;
	(void)interface_name;
	(void)error;
	(void)user_data;

	if (strcmp(property_name, "AvailableSourceTypes") == 0)
		return g_variant_new_uint32(1); /* monitor */
	if (strcmp(property_name, "AvailableCursorModes") == 0)
		return g_variant_new_uint32(1 | 2);
	if (strcmp(property_name, "IsCameraPresent") == 0)
		return g_variant_new_boolean(TRUE);
	return g_variant_new_uint32(strcmp(interface_name, "org.freedesktop.portal.Camera") == 0 ? 1 : 4);
}

static const GDBusInterfaceVTable portal_vtable = {
	.method_call = portal_method_call,
	.get_property = portal_get_property,
};

static gboolean on_term(gpointer loop)
{
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

static int fake_portal(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	g_autoptr(GDBusNodeInfo) node = g_dbus_node_info_new_for_xml(portal_xml, NULL);
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	g_autoptr(GVariant) reply = NULL;
	const char *s;

	if (!bus) {
		fprintf(stderr, "fake-portal: %s\n", error->message);
		return 1;
	}
	if ((s = getenv("OBS_TEST_SCREEN_NODE")))
		screen_node = (uint32_t)strtoul(s, NULL, 10);
	if ((s = getenv("OBS_TEST_SCREEN_SIZE")))
		sscanf(s, "%dx%d", &screen_width, &screen_height);
	for (int i = 0; node->interfaces[i]; i++)
		g_dbus_connection_register_object(bus, "/org/freedesktop/portal/desktop", node->interfaces[i],
						  &portal_vtable, NULL, NULL, NULL);
	reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
					    "org.freedesktop.DBus", "RequestName",
					    g_variant_new("(su)", "org.freedesktop.portal.Desktop", 4), NULL,
					    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!reply) {
		fprintf(stderr, "fake-portal: %s\n", error->message);
		return 1;
	}
	g_unix_signal_add(SIGTERM, on_term, loop);
	g_unix_signal_add(SIGINT, on_term, loop);
	printf("fake-portal: serving ScreenCast (node %u, %dx%d) and Camera\n", screen_node, screen_width,
	       screen_height);
	fflush(stdout);
	g_main_loop_run(loop);
	return 0;
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";

	if (strcmp(mode, "probe-pipewire") == 0)
		return probe_pipewire();
	if (strcmp(mode, "probe-pulse") == 0)
		return probe_pulse();
	if (strcmp(mode, "probe-gconv") == 0)
		return probe_gconv();
	if (strcmp(mode, "probe-dbus") == 0)
		return probe_dbus();
	if (strcmp(mode, "write-tone") == 0 && argc > 3)
		return write_tone(argv[2], atoi(argv[3]));
	if (strcmp(mode, "fake-portal") == 0)
		return fake_portal();
	fprintf(stderr,
		"usage: %s probe-pipewire|probe-pulse|probe-gconv|probe-dbus|write-tone <file> <s>|fake-portal\n",
		argv[0]);
	return 2;
}
