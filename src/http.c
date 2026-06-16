#include "http.h"

#include "assets.h"
#include "db.h"
#include "sms.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static GSocketService *service = NULL;

static void write_response_bytes(
    GOutputStream *out,
    const gchar *status,
    const gchar *content_type,
    const guint8 *body,
    gsize body_len)
{
    gchar *header;
    gsize written = 0;

    header = g_strdup_printf(
            "HTTP/1.1 %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store\r\n"
            "\r\n",
            status,
            content_type,
            body_len);

    g_output_stream_write_all(out, header, strlen(header), &written, NULL, NULL);
    g_output_stream_write_all(out, body, body_len, &written, NULL, NULL);
    g_free(header);
}

static void write_response(
    GOutputStream *out,
    const gchar *status,
    const gchar *content_type,
    const gchar *body)
{
    write_response_bytes(
            out,
            status,
            content_type,
            (const guint8 *)body,
            strlen(body));
}

static gboolean write_asset_response(GOutputStream *out, const gchar *path)
{
    const Asset *asset = asset_lookup(path);

    if (!asset)
        return FALSE;

    write_response_bytes(out, "200 OK", asset->content_type,
            asset->data, asset->size);
    return TRUE;
}

static gint parse_content_length(const gchar *headers)
{
    gchar **lines = g_strsplit(headers, "\r\n", -1);
    gint content_length = 0;

    for (gint i = 0; lines[i]; i++) {
        if (g_ascii_strncasecmp(lines[i], "Content-Length:", 15) == 0) {
            content_length = atoi(lines[i] + 15);
            break;
        }
    }

    g_strfreev(lines);
    return content_length;
}

static gchar *read_request(GInputStream *in)
{
    GString *request = g_string_new("");
    gchar buf[2048];
    gssize nread;
    gchar *header_end = NULL;
    gint content_length = 0;

    while ((nread = g_input_stream_read(in, buf, sizeof(buf), NULL, NULL)) > 0) {
        g_string_append_len(request, buf, nread);
        header_end = strstr(request->str, "\r\n\r\n");
        if (header_end) {
            gsize header_len = (header_end - request->str) + 4;
            content_length = parse_content_length(request->str);
            if (request->len >= header_len + (gsize)content_length)
                break;
        }
        if (request->len > 65536)
            break;
    }

    return g_string_free(request, FALSE);
}

static gchar *get_form_value(const gchar *body, const gchar *key)
{
    gchar **pairs = g_strsplit(body ? body : "", "&", -1);
    gchar *value = NULL;

    for (gint i = 0; pairs[i]; i++) {
        gchar **kv = g_strsplit(pairs[i], "=", 2);
        if (kv[0] && kv[1] && g_strcmp0(kv[0], key) == 0) {
            value = form_value_decode(kv[1]);
            g_strfreev(kv);
            break;
        }
        g_strfreev(kv);
    }

    g_strfreev(pairs);
    return value ? value : g_strdup("");
}

static gchar *get_query_value(const gchar *path, const gchar *key)
{
    const gchar *query = path ? strchr(path, '?') : NULL;

    if (!query)
        return g_strdup("");

    return get_form_value(query + 1, key);
}

static gint get_query_int(const gchar *path, const gchar *key, gint default_value)
{
    gchar *value = get_query_value(path, key);
    gint parsed = *value ? atoi(value) : default_value;

    g_free(value);
    return parsed;
}

static void append_message_json(GString *json, const Message *msg)
{
    g_string_append_printf(json, "{\"id\":%d,\"direction\":", msg->id);
    json_append_escaped(json, msg->direction);
    g_string_append(json, ",\"phone\":");
    json_append_escaped(json, msg->phone);
    g_string_append(json, ",\"content\":");
    json_append_escaped(json, msg->content);
    g_string_append_printf(json, ",\"timestamp\":%ld,\"status\":", (long)msg->timestamp);
    json_append_escaped(json, msg->status);
    g_string_append_c(json, '}');
}

static gchar *messages_to_json(const gchar *path)
{
    gchar *direction = get_query_value(path, "direction");
    gchar *all_value = get_query_value(path, "all");
    gint page = get_query_int(path, "page", 1);
    gint page_size = get_query_int(path, "page_size", 10);
    gboolean return_all = g_strcmp0(all_value, "1") == 0 ||
        g_ascii_strcasecmp(all_value, "true") == 0;
    gint total;
    GPtrArray *messages;
    GString *json;

    if (page < 1)
        page = 1;
    if (page_size <= 0 || page_size > 500)
        page_size = 10;

    total = db_count_messages(direction);
    messages = return_all ?
        db_get_messages_all(direction) :
        db_get_messages_page(direction, page, page_size);
    json = g_string_new("{\"items\":[");

    for (guint i = 0; i < messages->len; i++) {
        Message *msg = g_ptr_array_index(messages, i);
        if (i > 0)
            g_string_append_c(json, ',');
        append_message_json(json, msg);
    }

    g_string_append_printf(
            json,
            "],\"total\":%d,\"page\":%d,\"page_size\":%d,\"all\":%s}",
            total,
            page,
            page_size,
            return_all ? "true" : "false");
    g_ptr_array_free(messages, TRUE);
    g_free(all_value);
    g_free(direction);
    return g_string_free(json, FALSE);
}

static void handle_api_message(GOutputStream *out, const gchar *path)
{
    gint id = get_query_int(path, "id", 0);
    Message *msg = db_get_message(id);

    if (!msg) {
        write_response(out, "404 Not Found", "application/json",
                "{\"status\":\"error\",\"error\":\"message not found\"}");
        return;
    }

    GString *json = g_string_new("");
    append_message_json(json, msg);
    write_response(out, "200 OK", "application/json", json->str);
    g_string_free(json, TRUE);
    g_free(msg);
}

static void handle_api_send(GOutputStream *out, const gchar *body)
{
    gchar *recipient = get_form_value(body, "recipient");
    gchar *content = get_form_value(body, "content");
    gchar result_path[256] = {0};
    GString *json = g_string_new("{");

    if (!*recipient || !*content) {
        g_string_append(json, "\"status\":\"error\",\"error\":\"recipient and content are required\"}");
        write_response(out, "400 Bad Request", "application/json", json->str);
    } else if (sms_send_message(recipient, content, result_path, sizeof(result_path))) {
        g_string_append(json, "\"status\":\"success\",\"message\":\"sent\",\"path\":");
        json_append_escaped(json, result_path);
        g_string_append_c(json, '}');
        write_response(out, "200 OK", "application/json", json->str);
    } else {
        g_string_append(json, "\"status\":\"error\",\"error\":\"send failed\"}");
        write_response(out, "500 Internal Server Error", "application/json", json->str);
    }

    g_string_free(json, TRUE);
    g_free(recipient);
    g_free(content);
}

static void handle_api_delete(GOutputStream *out, const gchar *body)
{
    gchar *ids = get_form_value(body, "ids");

    if (!*ids) {
        write_response(out, "400 Bad Request", "application/json",
                "{\"status\":\"error\",\"error\":\"ids are required\"}");
    } else if (db_delete_messages(ids)) {
        write_response(out, "200 OK", "application/json",
                "{\"status\":\"success\"}");
    } else {
        write_response(out, "500 Internal Server Error", "application/json",
                "{\"status\":\"error\",\"error\":\"delete failed\"}");
    }

    g_free(ids);
}

static void handle_api_clear(GOutputStream *out, const gchar *body)
{
    gchar *direction = get_form_value(body, "direction");

    if (g_strcmp0(direction, "in") != 0 && g_strcmp0(direction, "out") != 0) {
        write_response(out, "400 Bad Request", "application/json",
                "{\"status\":\"error\",\"error\":\"direction must be in or out\"}");
    } else if (db_clear_messages(direction)) {
        write_response(out, "200 OK", "application/json",
                "{\"status\":\"success\"}");
    } else {
        write_response(out, "500 Internal Server Error", "application/json",
                "{\"status\":\"error\",\"error\":\"clear failed\"}");
    }

    g_free(direction);
}

static gboolean on_incoming_connection(
    GSocketService *svc,
    GSocketConnection *connection,
    GObject *source_object,
    gpointer user_data)
{
    GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    GSocket *socket = g_socket_connection_get_socket(connection);
    gchar *request;
    gchar **parts;
    gchar *method = NULL;
    gchar *path = NULL;
    gchar *body = NULL;

    (void)svc;
    (void)source_object;
    (void)user_data;

    g_socket_set_timeout(socket, 5);
    request = read_request(in);
    parts = g_strsplit(request, " ", 3);
    if (parts[0] && parts[1]) {
        method = parts[0];
        path = parts[1];
    }

    body = strstr(request, "\r\n\r\n");
    if (body)
        body += 4;
    else
        body = "";

    if (method && path && g_strcmp0(method, "GET") == 0 &&
            g_str_has_prefix(path, "/api/messages")) {
        gchar *json = messages_to_json(path);
        write_response(out, "200 OK", "application/json", json);
        g_free(json);
    } else if (method && path && g_strcmp0(method, "GET") == 0 &&
            g_str_has_prefix(path, "/api/message")) {
        handle_api_message(out, path);
    } else if (method && path && g_strcmp0(method, "POST") == 0 &&
            g_strcmp0(path, "/api/send") == 0) {
        handle_api_send(out, body);
    } else if (method && path && g_strcmp0(method, "POST") == 0 &&
            g_strcmp0(path, "/api/delete") == 0) {
        handle_api_delete(out, body);
    } else if (method && path && g_strcmp0(method, "POST") == 0 &&
            g_strcmp0(path, "/api/clear") == 0) {
        handle_api_clear(out, body);
    } else if (method && path && g_strcmp0(path, "/favicon.ico") == 0) {
        write_response(out, "404 Not Found", "text/plain", "not found");
    } else {
        if (!write_asset_response(out, path))
            write_response(out, "404 Not Found", "text/plain", "not found");
    }

    g_strfreev(parts);
    g_free(request);
    return TRUE;
}

gboolean http_server_start(guint16 port)
{
    GError *error = NULL;

    service = g_socket_service_new();
    g_signal_connect(service, "incoming", G_CALLBACK(on_incoming_connection), NULL);

    if (!g_socket_listener_add_inet_port(
                G_SOCKET_LISTENER(service),
                port,
                NULL,
                &error)) {
        g_printerr("listen port %u failed: %s\n", port, error->message);
        g_error_free(error);
        g_object_unref(service);
        service = NULL;
        return FALSE;
    }

    g_socket_service_start(service);
    g_print("HTTP server listening on %u.\n", port);
    return TRUE;
}

void http_server_stop(void)
{
    if (service) {
        g_socket_service_stop(service);
        g_object_unref(service);
        service = NULL;
    }
}
