#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_DIR "/home/root/network"
#define CONFIG_PATH CONFIG_DIR "/device.conf"
#define DEFAULT_NOTIFY_HOST "192.168.66.6"
#define DEFAULT_NOTIFY_PORT 18080

static void config_set_defaults(DeviceConfig *config)
{
    memset(config, 0, sizeof(*config));
    g_strlcpy(config->device_id, "udx710", sizeof(config->device_id));
    config->notify_enabled = TRUE;
}

static gboolean detect_usb_peer_ip(gchar *ip, gsize ip_size)
{
    gchar *content = NULL;
    gchar **lines;
    gboolean found = FALSE;

    if (!g_file_get_contents("/proc/net/arp", &content, NULL, NULL))
        return FALSE;

    lines = g_strsplit(content, "\n", -1);
    for (gint i = 1; lines[i]; i++) {
        gchar addr[64] = {0};
        gchar hw_type[32] = {0};
        gchar flags[32] = {0};
        gchar hw_addr[64] = {0};
        gchar mask[64] = {0};
        gchar device[32] = {0};

        if (sscanf(lines[i], "%63s %31s %31s %63s %63s %31s",
                    addr,
                    hw_type,
                    flags,
                    hw_addr,
                    mask,
                    device) != 6) {
            continue;
        }

        if (g_strcmp0(device, "usb0") == 0 &&
                g_ascii_strcasecmp(flags, "0x2") == 0) {
            g_strlcpy(ip, addr, ip_size);
            found = TRUE;
            break;
        }
    }

    g_strfreev(lines);
    g_free(content);
    return found;
}

static void config_write_template(void)
{
    const gchar *template_text =
        "# UDX710 SMS Gateway device config\n"
        "[device]\n"
        "device_id=udx710\n"
        "notify_enabled=true\n"
        "# notify_host=192.168.66.6\n"
        "# notify_port=18080\n"
        "token=\n";

    if (g_mkdir_with_parents(CONFIG_DIR, 0755) != 0) {
        g_printerr("create config dir %s failed: %s\n",
                CONFIG_DIR,
                g_strerror(errno));
        return;
    }

    if (!g_file_test(CONFIG_PATH, G_FILE_TEST_EXISTS)) {
        GError *error = NULL;
        if (!g_file_set_contents(CONFIG_PATH, template_text, -1, &error)) {
            g_printerr("write default config %s failed: %s\n",
                    CONFIG_PATH,
                    error->message);
            g_error_free(error);
        }
    }
}

gboolean config_load(DeviceConfig *config)
{
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *value;
    gint port;
    gchar detected_host[128] = {0};

    if (!config)
        return FALSE;

    config_set_defaults(config);
    config_write_template();

    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(
                key_file,
                CONFIG_PATH,
                G_KEY_FILE_NONE,
                &error)) {
        g_printerr("load config %s failed: %s\n", CONFIG_PATH, error->message);
        g_error_free(error);
        g_key_file_unref(key_file);
        return FALSE;
    }

    value = g_key_file_get_string(key_file, "device", "device_id", NULL);
    if (value && *value)
        g_strlcpy(config->device_id, value, sizeof(config->device_id));
    g_free(value);

    if (g_key_file_has_key(key_file, "device", "notify_host", NULL)) {
        value = g_key_file_get_string(key_file, "device", "notify_host", NULL);
        if (value && *value) {
            g_strlcpy(config->notify_host, value, sizeof(config->notify_host));
            config->notify_host_configured = TRUE;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, "device", "notify_port", NULL)) {
        port = g_key_file_get_integer(key_file, "device", "notify_port", NULL);
        if (port > 0 && port <= 65535)
            config->notify_port = (guint16)port;
    }

    value = g_key_file_get_string(key_file, "device", "token", NULL);
    if (value)
        g_strlcpy(config->token, value, sizeof(config->token));
    g_free(value);

    if (g_key_file_has_key(key_file, "device", "notify_enabled", NULL)) {
        config->notify_enabled = g_key_file_get_boolean(
                key_file,
                "device",
                "notify_enabled",
                NULL);
    }

    if (detect_usb_peer_ip(detected_host, sizeof(detected_host))) {
        g_strlcpy(config->notify_host, detected_host, sizeof(config->notify_host));
        config->notify_host_detected = TRUE;
        g_print("detected USB notify host: %s\n", config->notify_host);
    } else if (!config->notify_host[0]) {
        g_strlcpy(config->notify_host, DEFAULT_NOTIFY_HOST, sizeof(config->notify_host));
    }

    if (config->notify_port == 0)
        config->notify_port = DEFAULT_NOTIFY_PORT;

    g_key_file_unref(key_file);
    return TRUE;
}
