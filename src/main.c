#include "db.h"
#include "http.h"
#include "sms.h"
#include "util.h"

#include <gio/gio.h>
#include <signal.h>
#include <unistd.h>

#define DB_PATH "/home/root/network/messages.db"
#define HTTP_PORT 9527
#define OFONO_SERVICE "org.ofono"
#define MODEM_PATH "/ril_0"
#define OFONO_CONNECTION_MANAGER "org.ofono.ConnectionManager"
#define OFONO_TIMEOUT_MS 5000

static GMainLoop *loop = NULL;

static void disable_selinux(void)
{
    if (run_command_checked("setenforce 0"))
        g_print("SELinux set to permissive.\n");
    else
        g_printerr("setenforce 0 failed; continuing.\n");
}

static void enable_roaming_allowed(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(
            bus,
            OFONO_SERVICE,
            MODEM_PATH,
            OFONO_CONNECTION_MANAGER,
            "SetProperty",
            g_variant_new("(sv)", "RoamingAllowed", g_variant_new_boolean(TRUE)),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            OFONO_TIMEOUT_MS,
            NULL,
            &error);

    if (error) {
        g_printerr("set RoamingAllowed=true failed: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (result)
        g_variant_unref(result);

    g_print("set RoamingAllowed=true success.\n");
}

static gboolean add_http_port_redirect(gpointer user_data)
{
    (void)user_data;

    if (run_command_checked(
                "iptables -t nat -C PREROUTING -p tcp --dport 80 "
                "-j REDIRECT --to-port 9527 || "
                "iptables -t nat -A PREROUTING -p tcp --dport 80 "
                "-j REDIRECT --to-port 9527")) {
        g_print("redirect tcp/%d to tcp/%d success.\n",
                80,
                HTTP_PORT);
    }

    return G_SOURCE_REMOVE;
}

static void handle_stop_signal(int sig)
{
    (void)sig;
    if (loop)
        g_main_loop_quit(loop);
}

static GDBusConnection *connect_system_bus(void)
{
    GDBusConnection *bus = NULL;
    GError *error = NULL;

    do {
        if (error) {
            g_printerr("DBus connection failed: %s\n", error->message);
            g_error_free(error);
            error = NULL;
            sleep(1);
        }
        g_print("try bus get...\n");
        bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    } while (error);

    g_print("bus get success.\n");
    return bus;
}

int main(void)
{
    GDBusConnection *bus;

    signal(SIGINT, handle_stop_signal);
    signal(SIGTERM, handle_stop_signal);

    disable_selinux();

    if (!db_init(DB_PATH))
        g_printerr("database init failed; SMS history may not be saved.\n");

    bus = connect_system_bus();

    enable_roaming_allowed(bus);
    add_http_port_redirect(NULL);
    g_timeout_add_seconds(20, add_http_port_redirect, NULL);

    g_print("SMS mode: skip APN activation and data firewall control.\n");
    sms_init(bus);
    http_server_start(HTTP_PORT);

    loop = g_main_loop_new(NULL, FALSE);
    g_print("network-daemon started.\n");
    g_main_loop_run(loop);

    http_server_stop();
    sms_deinit();
    g_object_unref(bus);
    g_main_loop_unref(loop);
    loop = NULL;
    return 0;
}
