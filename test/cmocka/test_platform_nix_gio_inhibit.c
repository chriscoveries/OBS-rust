#include <stdbool.h>
#include <string.h>

#include <cmocka.h>
#include <gio/gio.h>
#include <util/platform.h>

#define TEST_GIO_EXPORT __attribute__((visibility("default")))

static GDBusConnection *fake_connection;
static unsigned int bus_get_calls;
static unsigned int unique_name_calls;
static unsigned int portal_version_calls;
static unsigned int legacy_service_queries;
static unsigned int inhibit_calls;
static unsigned int close_calls;
static unsigned int subscribe_calls;
static unsigned int unsubscribe_calls;
static bool reason_matches;

TEST_GIO_EXPORT GDBusConnection *g_bus_get_sync(GBusType bus_type, GCancellable *cancellable, GError **error)
{
	(void)cancellable;
	(void)error;
	bus_get_calls++;
	assert_int_equal(bus_type, G_BUS_TYPE_SESSION);
	fake_connection = (GDBusConnection *)g_cancellable_new();
	return fake_connection;
}

TEST_GIO_EXPORT const gchar *g_dbus_connection_get_unique_name(GDBusConnection *connection)
{
	assert_ptr_equal(connection, fake_connection);
	unique_name_calls++;
	return ":1.42";
}

TEST_GIO_EXPORT GVariant *g_dbus_connection_call_sync(GDBusConnection *connection, const gchar *bus_name,
							       const gchar *object_path,
							       const gchar *interface_name,
							       const gchar *method_name,
							       GVariant *parameters,
							       const GVariantType *reply_type,
							       GDBusCallFlags flags, gint timeout_msec,
							       GCancellable *cancellable, GError **error)
{
	(void)parameters;
	(void)reply_type;
	(void)flags;
	(void)timeout_msec;
	(void)cancellable;
	(void)error;
	assert_ptr_equal(connection, fake_connection);
	if (strcmp(interface_name, "org.freedesktop.DBus.Properties") == 0 && strcmp(method_name, "Get") == 0) {
		portal_version_calls++;
		assert_string_equal(bus_name, "org.freedesktop.portal.Desktop");
		assert_string_equal(object_path, "/org/freedesktop/portal/desktop");
		return g_variant_ref_sink(g_variant_new("(v)", g_variant_new_uint32(3)));
	}
	if (strcmp(interface_name, "org.freedesktop.DBus") == 0 && strcmp(method_name, "GetNameOwner") == 0) {
		legacy_service_queries++;
		return NULL;
	}
	fail_msg("unexpected synchronous D-Bus call: %s.%s", interface_name, method_name);
	return NULL;
}

TEST_GIO_EXPORT guint g_dbus_connection_signal_subscribe(GDBusConnection *connection, const gchar *sender,
							 const gchar *interface_name,
							 const gchar *member, const gchar *object_path,
							 const gchar *arg0,
							 GDBusSignalFlags flags,
							 GDBusSignalCallback callback,
							 gpointer user_data, GDestroyNotify destroy)
{
	(void)arg0;
	(void)flags;
	(void)callback;
	(void)user_data;
	(void)destroy;
	assert_ptr_equal(connection, fake_connection);
	assert_string_equal(sender, "org.freedesktop.portal.Desktop");
	assert_string_equal(interface_name, "org.freedesktop.portal.Request");
	assert_string_equal(member, "Response");
	assert_non_null(object_path);
	subscribe_calls++;
	return 1;
}

TEST_GIO_EXPORT void g_dbus_connection_signal_unsubscribe(GDBusConnection *connection, guint subscription_id)
{
	assert_ptr_equal(connection, fake_connection);
	assert_int_equal(subscription_id, 1);
	unsubscribe_calls++;
}

TEST_GIO_EXPORT void g_dbus_connection_call(GDBusConnection *connection, const gchar *bus_name,
						    const gchar *object_path, const gchar *interface_name,
						    const gchar *method_name, GVariant *parameters,
						    const GVariantType *reply_type, GDBusCallFlags flags,
						    gint timeout_msec, GCancellable *cancellable,
						    GAsyncReadyCallback callback, gpointer user_data)
{
	(void)reply_type;
	(void)flags;
	(void)timeout_msec;
	(void)cancellable;
	assert_ptr_equal(connection, fake_connection);
	assert_string_equal(bus_name, "org.freedesktop.portal.Desktop");
	if (strcmp(method_name, "Inhibit") == 0) {
		const gchar *app_id;
		const gchar *reason;
		guint32 inhibit_flags;
		GVariant *options;

		assert_string_equal(object_path, "/org/freedesktop/portal/desktop");
		assert_string_equal(interface_name, "org.freedesktop.portal.Inhibit");
		g_variant_get(parameters, "(&su@a{sv})", &app_id, &inhibit_flags, &options);
		assert_string_equal(app_id, "");
		assert_int_equal(inhibit_flags, 0xC);
		reason_matches = g_variant_lookup(options, "reason", "&s", &reason) &&
				 strcmp(reason, "cmocka portal parity") == 0;
		g_variant_unref(options);
		inhibit_calls++;
	} else {
		assert_string_equal(method_name, "Close");
		assert_string_equal(interface_name, "org.freedesktop.portal.Request");
		assert_non_null(object_path);
		close_calls++;
	}

	assert_non_null(callback);
	callback((GObject *)connection, (GAsyncResult *)connection, user_data);
}

TEST_GIO_EXPORT GVariant *g_dbus_connection_call_finish(GDBusConnection *connection, GAsyncResult *result,
							 GError **error)
{
	(void)result;
	(void)error;
	assert_ptr_equal(connection, fake_connection);
	return g_variant_ref_sink(g_variant_new("()"));
}

static void reset_fake_portal(void)
{
	bus_get_calls = 0;
	unique_name_calls = 0;
	portal_version_calls = 0;
	legacy_service_queries = 0;
	inhibit_calls = 0;
	close_calls = 0;
	subscribe_calls = 0;
	unsubscribe_calls = 0;
	reason_matches = false;
}

static void test_portal_is_selected_and_receives_both_transitions(void **state)
{
	(void)state;
	reset_fake_portal();
	fake_connection = NULL;

	os_inhibit_t *info = os_inhibit_sleep_create("cmocka portal parity");
	assert_non_null(info);
	assert_true(os_inhibit_sleep_set_active(info, true));
	assert_true(os_inhibit_sleep_set_active(info, false));
	assert_false(os_inhibit_sleep_set_active(info, false));
	os_inhibit_sleep_destroy(info);

	assert_int_equal(bus_get_calls, 1);
	assert_int_equal(unique_name_calls, 1);
	assert_int_equal(portal_version_calls, 1);
	assert_int_equal(legacy_service_queries, 0);
	assert_int_equal(inhibit_calls, 1);
	assert_int_equal(close_calls, 1);
	assert_int_equal(subscribe_calls, 1);
	assert_int_equal(unsubscribe_calls, 1);
	assert_true(reason_matches);
	fake_connection = NULL;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_portal_is_selected_and_receives_both_transitions),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
