#include "notify_client.h"

#include "util.h"

#include <gio/gio.h>
#include <string.h>

static DeviceConfig notify_config;
static gboolean configured = FALSE;

static gchar *build_url(const gchar *path)
{
    return g_strdup_printf(
            "http://%s:%u%s",
            notify_config.notify_host,
            notify_config.notify_port,
            path);
}

static gboolean http_request(
    const gchar *method,
    const gchar *path,
    const gchar *body)
{
    GSocketClient *client;
    GSocketConnection *connection;
    GOutputStream *out;
    GInputStream *in;
    GError *error = NULL;
    gchar *host_port;
    gchar *request;
    gchar response[256] = {0};
    gssize nread;
    gboolean ok = FALSE;
    gchar *url;

    url = build_url(path);
    host_port = g_strdup_printf(
            "%s:%u",
            notify_config.notify_host,
            notify_config.notify_port);

    client = g_socket_client_new();
    g_socket_client_set_timeout(client, 3);
    connection = g_socket_client_connect_to_host(
            client,
            host_port,
            notify_config.notify_port,
            NULL,
            &error);
    g_free(host_port);

    if (!connection) {
        g_printerr("notify connect failed: url=%s error=%s\n",
                url,
                error ? error->message : "unknown");
        if (error)
            g_error_free(error);
        g_object_unref(client);
        g_free(url);
        return FALSE;
    }

    out = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    request = g_strdup_printf(
            "%s %s HTTP/1.1\r\n"
            "Host: %s:%u\r\n"
            "Connection: close\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "X-Device-Token: %s\r\n"
            "\r\n"
            "%s",
            method,
            path,
            notify_config.notify_host,
            notify_config.notify_port,
            body ? strlen(body) : 0,
            notify_config.token,
            body ? body : "");

    if (!g_output_stream_write_all(
                out,
                request,
                strlen(request),
                NULL,
                NULL,
                &error)) {
        g_printerr("notify write failed: %s\n", error ? error->message : "unknown");
        if (error)
            g_error_free(error);
    } else {
        nread = g_input_stream_read(in, response, sizeof(response) - 1, NULL, NULL);
        if (nread <= 0)
            g_printerr("notify response empty: %s\n", url);

        if (nread > 0 && g_str_has_prefix(response, "HTTP/1.1 200"))
            ok = TRUE;
    }

    g_free(request);
    g_object_unref(connection);
    g_object_unref(client);
    g_free(url);
    return ok;
}

void notify_client_init(const DeviceConfig *config)
{
    gboolean changed;

    if (!config) {
        configured = FALSE;
        g_print("SMS notify disabled: config is NULL.\n");
        return;
    }

    if (!config->notify_enabled) {
        configured = FALSE;
        g_print("SMS notify disabled: notify_enabled=false.\n");
        return;
    }

    if (!*config->notify_host) {
        configured = FALSE;
        g_print("SMS notify disabled: notify_host is empty.\n");
        return;
    }

    changed = !configured ||
        g_strcmp0(notify_config.notify_host, config->notify_host) != 0 ||
        notify_config.notify_port != config->notify_port ||
        g_strcmp0(notify_config.token, config->token) != 0 ||
        g_strcmp0(notify_config.device_id, config->device_id) != 0;

    notify_config = *config;
    configured = TRUE;

    if (changed) {
        gchar *url = build_url("/sms/notify");
        g_print("SMS notify target: %s\n", url);
        g_free(url);
    }
}

void notify_client_send_message(const Message *message)
{
    GString *json;

    if (!message) {
        return;
    }

    if (!configured) {
        return;
    }

    if (!http_request("GET", "/sms/notify/health", NULL)) {
        g_printerr("notify health check failed; skip SMS push.\n");
        return;
    }

    json = g_string_new("{\"device_id\":");
    json_append_escaped(json, notify_config.device_id);
    g_string_append_printf(json, ",\"message_id\":%d,\"direction\":", message->id);
    json_append_escaped(json, message->direction);
    g_string_append(json, ",\"phone\":");
    json_append_escaped(json, message->phone);
    g_string_append(json, ",\"content\":");
    json_append_escaped(json, message->content);
    g_string_append_printf(json, ",\"timestamp\":%ld,\"status\":", (long)message->timestamp);
    json_append_escaped(json, message->status);
    g_string_append_c(json, '}');

    if (http_request("POST", "/sms/notify", json->str))
        g_print("SMS pushed to notify server: id=%d\n", message->id);
    else
        g_printerr("SMS push failed: id=%d\n", message->id);

    g_string_free(json, TRUE);
}
