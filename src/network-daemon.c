#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define OFONO_SERVICE "org.ofono"
#define MODEM_PATH "/ril_0"
#define DEFAULT_CONTEXT_PATH "/ril_0/context1"
#define OFONO_CONNECTION_MANAGER "org.ofono.ConnectionManager"
#define OFONO_CONNECTION_CONTEXT "org.ofono.ConnectionContext"
#define OFONO_NETWORK_REGISTRATION "org.ofono.NetworkRegistration"
#define OFONO_TIMEOUT_MS 5000

static GMainLoop *loop = NULL;
static guint subscription_id = 0;
static GDBusConnection *sys_bus = NULL;

static gboolean run_command_checked(const gchar *cmd)
{
    int status = system(cmd);

    if (status == -1) {
        g_printerr("command failed to start: %s\n", cmd);
        return FALSE;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        g_printerr("command failed: %s (status=%d)\n", cmd, status);
        return FALSE;
    }

    return TRUE;
}

static gboolean set_ofono_boolean_property(
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *property_name,
    gboolean enabled)
{
    GError *error = NULL;
    GVariant *result = NULL;

    if (!sys_bus) {
        g_printerr("system bus is not ready.\n");
        return FALSE;
    }

    result = g_dbus_connection_call_sync(
            sys_bus,
            OFONO_SERVICE,
            object_path,
            interface_name,
            "SetProperty",
            g_variant_new("(sv)", property_name, g_variant_new_boolean(enabled)),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            OFONO_TIMEOUT_MS,
            NULL,
            &error);

    if (error) {
        g_printerr("set %s %s=%s failed: %s\n",
                object_path,
                property_name,
                enabled ? "true" : "false",
                error->message);
        g_error_free(error);
        return FALSE;
    }

    if (result)
        g_variant_unref(result);

    g_print("set %s %s=%s success.\n",
            object_path,
            property_name,
            enabled ? "true" : "false");
    return TRUE;
}

static gboolean find_internet_context_path(gchar *path_buf, gsize buf_size)
{
    GError *error = NULL;
    GVariant *result = NULL;
    GVariant *array = NULL;
    GVariantIter iter;
    GVariant *child = NULL;
    gchar first_internet_path[128] = {0};
    gboolean found = FALSE;

    if (!path_buf || buf_size == 0 || !sys_bus)
        return FALSE;

    path_buf[0] = '\0';

    result = g_dbus_connection_call_sync(
            sys_bus,
            OFONO_SERVICE,
            MODEM_PATH,
            OFONO_CONNECTION_MANAGER,
            "GetContexts",
            NULL,
            G_VARIANT_TYPE("(a(oa{sv}))"),
            G_DBUS_CALL_FLAGS_NONE,
            OFONO_TIMEOUT_MS,
            NULL,
            &error);

    if (error) {
        g_printerr("GetContexts failed: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    if (!result)
        return FALSE;

    array = g_variant_get_child_value(result, 0);
    g_variant_iter_init(&iter, array);

    while ((child = g_variant_iter_next_value(&iter)) != NULL) {
        const gchar *path = NULL;
        GVariant *props = NULL;

        g_variant_get(child, "(&o@a{sv})", &path, &props);

        if (path && props) {
            GVariant *type_var =
                g_variant_lookup_value(props, "Type", G_VARIANT_TYPE_STRING);
            const gchar *context_type =
                type_var ? g_variant_get_string(type_var, NULL) : "";

            if (g_strcmp0(context_type, "internet") == 0) {
                GVariant *apn_var = g_variant_lookup_value(
                        props,
                        "AccessPointName",
                        G_VARIANT_TYPE_STRING);
                const gchar *apn =
                    apn_var ? g_variant_get_string(apn_var, NULL) : "";

                if (first_internet_path[0] == '\0')
                    g_strlcpy(first_internet_path, path, sizeof(first_internet_path));

                if (apn && apn[0] != '\0') {
                    g_strlcpy(path_buf, path, buf_size);
                    found = TRUE;
                }

                if (apn_var)
                    g_variant_unref(apn_var);
            }

            if (type_var)
                g_variant_unref(type_var);
            g_variant_unref(props);
        }

        g_variant_unref(child);

        if (found)
            break;
    }

    if (!found && first_internet_path[0] != '\0') {
        g_strlcpy(path_buf, first_internet_path, buf_size);
        found = TRUE;
    }

    g_variant_unref(array);
    g_variant_unref(result);
    return found;
}

static gboolean disable_connman_autoconnect(void)
{
    g_print("Disable connman data autoconnect...\n");
    return run_command_checked("connmanctl setautoconnect off");
}

static gboolean enable_roaming_allowed(void)
{
    g_print("Enable oFono roaming allowed...\n");
    return set_ofono_boolean_property(
            MODEM_PATH,
            OFONO_CONNECTION_MANAGER,
            "RoamingAllowed",
            TRUE);
}

static gboolean activate_apn_context(void)
{
    gchar context_path[128] = {0};

    if (!find_internet_context_path(context_path, sizeof(context_path))) {
        g_strlcpy(context_path, DEFAULT_CONTEXT_PATH, sizeof(context_path));
        g_print("use default context: %s\n", context_path);
    } else {
        g_print("use internet context: %s\n", context_path);
    }

    if (set_ofono_boolean_property(
                context_path,
                OFONO_CONNECTION_CONTEXT,
                "Active",
                TRUE)) {
        return TRUE;
    }

    g_print("fallback to connmanctl ActivatePdp 1...\n");
    return run_command_checked("connmanctl ActivatePdp 1");
}

static void configure_sms_ready_mode(void)
{
    gboolean autoconnect_ok = disable_connman_autoconnect();
    gboolean roaming_ok = enable_roaming_allowed();
    gboolean apn_ok = activate_apn_context();

    if (autoconnect_ok && roaming_ok && apn_ok) {
        g_print("SMS-ready network setup complete.\n");
    } else {
        g_printerr("SMS-ready network setup incomplete: autoconnect=%d roaming=%d apn=%d\n",
                autoconnect_ok,
                roaming_ok,
                apn_ok);
    }
}

static void configure_if_ps_domain_registered(void)
{
    GError *error = NULL;
    GVariant *result = NULL;
    GVariant *props = NULL;
    GVariant *ps_domain = NULL;
    const gchar *status = NULL;

    result = g_dbus_connection_call_sync(
            sys_bus,
            OFONO_SERVICE,
            MODEM_PATH,
            OFONO_NETWORK_REGISTRATION,
            "GetProperties",
            NULL,
            G_VARIANT_TYPE("(a{sv})"),
            G_DBUS_CALL_FLAGS_NONE,
            OFONO_TIMEOUT_MS,
            NULL,
            &error);

    if (error) {
        g_printerr("GetProperties %s failed: %s\n",
                OFONO_NETWORK_REGISTRATION,
                error->message);
        g_error_free(error);
        return;
    }

    if (!result)
        return;

    props = g_variant_get_child_value(result, 0);
    ps_domain = g_variant_lookup_value(
            props,
            "PS Domain",
            G_VARIANT_TYPE_STRING);

    if (ps_domain) {
        status = g_variant_get_string(ps_domain, NULL);
        g_print("Current PS Domain status: %s\n", status);
        if (g_strcmp0(status, "REGISTERED") == 0)
            configure_sms_ready_mode();
        g_variant_unref(ps_domain);
    }

    g_variant_unref(props);
    g_variant_unref(result);
}

static void on_signal_received(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    (void)connection; (void)sender_name; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;
    gchar *prop_name = NULL;
    GVariant *prop_value = NULL;

    g_variant_get(parameters, "(sv)", &prop_name, &prop_value);

    if (g_strcmp0(prop_name, "PS Domain") == 0) {
        const gchar *status = g_variant_get_string(prop_value, NULL);
        g_print("PS Domain status: %s\n", status);
        if (g_strcmp0(status, "REGISTERED") == 0)
            configure_sms_ready_mode();
    }

    g_free(prop_name);
    g_variant_unref(prop_value);
}

static gboolean delay_task(gpointer user_data) {
    (void)user_data;
    run_command_checked("iptables -t nat -A PREROUTING -p tcp -d 192.168.66.1 --dport 80 -j REDIRECT --to-port 9527");
    g_print("forward 80 port.");
    return G_SOURCE_REMOVE;
}

static void sigint_handler(int sig)
{
    (void)sig;
    g_print("\nstop listen...\n");
    g_main_loop_quit(loop);
}

int main(void)
{
    GError *error = NULL;

    run_command_checked("setenforce 0");
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
            OFONO_SERVICE,
            OFONO_NETWORK_REGISTRATION,
            "PropertyChanged",
            MODEM_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_signal_received,
            NULL,
            NULL);

    configure_if_ps_domain_registered();

    loop = g_main_loop_new(NULL, FALSE);
    g_print("listen...\n");
    g_main_loop_run(loop);

    if (subscription_id > 0)
        g_dbus_connection_signal_unsubscribe(sys_bus, subscription_id);
    g_object_unref(sys_bus);
    g_main_loop_unref(loop);

    return 0;
}
