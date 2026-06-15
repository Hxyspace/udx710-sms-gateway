#include "sms.h"

#include "db.h"

#define OFONO_SERVICE "org.ofono"
#define MODEM_PATH "/ril_0"
#define OFONO_MESSAGE_MANAGER "org.ofono.MessageManager"
#define OFONO_TIMEOUT_MS 15000

static GDBusConnection *sms_bus = NULL;
static guint sms_subscription_id = 0;

static void on_incoming_message(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    const gchar *content = NULL;
    GVariant *props = NULL;
    GVariant *sender_var = NULL;
    const gchar *sender = "unknown";

    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)user_data;

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sa{sv})"))) {
        g_printerr("IncomingMessage parameter type mismatch.\n");
        return;
    }

    g_variant_get(parameters, "(&s@a{sv})", &content, &props);
    sender_var = g_variant_lookup_value(props, "Sender", G_VARIANT_TYPE_STRING);
    if (sender_var)
        sender = g_variant_get_string(sender_var, NULL);

    g_print("incoming SMS from %s: %s\n", sender, content ? content : "");
    db_add_message("in", sender, content ? content : "", "received");

    if (sender_var)
        g_variant_unref(sender_var);
    g_variant_unref(props);
}

gboolean sms_init(GDBusConnection *bus)
{
    if (!bus)
        return FALSE;

    sms_bus = g_object_ref(bus);
    sms_subscription_id = g_dbus_connection_signal_subscribe(
            sms_bus,
            OFONO_SERVICE,
            OFONO_MESSAGE_MANAGER,
            "IncomingMessage",
            NULL,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_incoming_message,
            NULL,
            NULL);

    g_print("SMS signal subscription ID: %u\n", sms_subscription_id);
    return sms_subscription_id > 0;
}

void sms_deinit(void)
{
    if (sms_subscription_id > 0 && sms_bus) {
        g_dbus_connection_signal_unsubscribe(sms_bus, sms_subscription_id);
        sms_subscription_id = 0;
    }

    if (sms_bus) {
        g_object_unref(sms_bus);
        sms_bus = NULL;
    }
}

gboolean sms_send_message(
    const gchar *recipient,
    const gchar *content,
    gchar *result_path,
    gsize path_size)
{
    GError *error = NULL;
    GVariant *result = NULL;
    const gchar *path = NULL;

    if (!sms_bus || !recipient || !*recipient || !content || !*content)
        return FALSE;

    result = g_dbus_connection_call_sync(
            sms_bus,
            OFONO_SERVICE,
            MODEM_PATH,
            OFONO_MESSAGE_MANAGER,
            "SendMessage",
            g_variant_new("(ss)", recipient, content),
            G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE,
            OFONO_TIMEOUT_MS,
            NULL,
            &error);

    if (error) {
        g_printerr("send SMS failed: %s\n", error->message);
        g_error_free(error);
        db_add_message("out", recipient, content, "failed");
        return FALSE;
    }

    g_variant_get(result, "(&o)", &path);
    if (result_path && path_size > 0 && path) {
        g_strlcpy(result_path, path, path_size);
    }
    g_variant_unref(result);

    db_add_message("out", recipient, content, "sent");
    return TRUE;
}
