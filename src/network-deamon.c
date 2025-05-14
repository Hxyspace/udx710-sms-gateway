#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static GMainLoop *loop = NULL;
static guint subscription_id = 0;
static GDBusConnection *sys_bus = NULL;

static void on_signal_received(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    (void)connection; (void)user_data;
    gchar *prop_name = NULL;
    GVariant *prop_value = NULL;
    GError *error = NULL;

    g_variant_get(parameters, "(sv)", &prop_name, &prop_value);

    if (g_strcmp0(prop_name, "PS Domain") == 0) {
        const gchar *status = g_variant_get_string(prop_value, NULL);
        g_print("PS Domain status: %s\n", status);
        if (g_strcmp0(status, "REGISTERED") == 0) {
            g_print("Activate APN...\n");
            //system("connmanctl ActivatePdp 1");
            GVariant *val = g_variant_new_boolean(1);
            val = g_variant_new("(sv)", "Active", val);
            GVariant *result = g_dbus_connection_call_sync(
                    sys_bus,
                    "org.ofono",
                    "/ril_0/context1",
                    "org.ofono.ConnectionContext",
                    "SetProperty", val,
                    NULL,
                    G_DBUS_CALL_FLAGS_NONE,
                    5000,
                    NULL,
                    &error);

            if (error) {
                g_printerr("set /ril_0/context1 Active fail: %s\n",
                        error->message);
                g_error_free(error);
            } else {
                g_print("set /ril_0/context1 Active success!!!\n");
                g_variant_unref(result);
            }
                system("iptables -F");
                system("ip6tables -F");
                g_print("Flush all chains.");
        }
    }

    g_free(prop_name);
    g_variant_unref(prop_value);
}

static gboolean delay_task(gpointer user_data) {
    system("iptables -t nat -A PREROUTING -p tcp -d 192.168.66.1 --dport 80 -j REDIRECT --to-port 9527");
    g_print("forward 80 port.");
    return G_SOURCE_REMOVE;
}

static void sigint_handler(int sig)
{
    (void)sig;
    g_print("\nstop listen...\n");
    g_main_loop_quit(loop);
}

int main()
{
    GError *error = NULL;

    system("setenforce 0");
    g_print("disable selinux.\n");

    g_timeout_add_seconds(20, delay_task, NULL);

    signal(SIGINT, sigint_handler);

    do {
        if (error) {
            g_printerr("DBus connection failed: %s\n", error->message);
            g_error_free(error);
            error = NULL;
            sleep(1);
        }
        g_print("try bus get...\n");
        sys_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    } while(error);

    g_print("bus get success.\n");

    subscription_id = g_dbus_connection_signal_subscribe(
            sys_bus,
            "org.ofono",
            "org.ofono.NetworkRegistration",
            "PropertyChanged",
            "/ril_0",
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_signal_received,
            NULL,
            NULL);

    loop = g_main_loop_new(NULL, FALSE);
    g_print("listen...\n");
    g_main_loop_run(loop);

    if (subscription_id > 0)
        g_dbus_connection_signal_unsubscribe(sys_bus, subscription_id);
    g_object_unref(sys_bus);
    g_main_loop_unref(loop);

    return 0;
}
