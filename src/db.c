#include "db.h"

#include <errno.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static gchar db_path[256] = "/home/root/network/messages.db";
static GMutex db_mutex;

static gchar *sql_escape(const gchar *src)
{
    GString *out = g_string_new("");

    for (const gchar *p = src ? src : ""; *p; p++) {
        if (*p == '\'')
            g_string_append(out, "''");
        else
            g_string_append_c(out, *p);
    }

    return g_string_free(out, FALSE);
}

static gchar *hex_decode_alloc(const gchar *hex)
{
    gsize len = hex ? strlen(hex) : 0;
    gchar *out = g_malloc0(len / 2 + 1);
    gsize j = 0;

    for (gsize i = 0; i + 1 < len; i += 2) {
        guint byte = 0;
        if (sscanf(hex + i, "%2x", &byte) != 1)
            break;
        out[j++] = (gchar)byte;
    }

    out[j] = '\0';
    return out;
}

static const gchar *normalize_direction(const gchar *direction)
{
    if (g_strcmp0(direction, "in") == 0)
        return "in";
    if (g_strcmp0(direction, "out") == 0)
        return "out";
    return NULL;
}

static gboolean sqlite_run(const gchar *sql, gchar **stdout_out)
{
    gchar *argv[] = { "sqlite3", db_path, (gchar *)sql, NULL };
    gchar *stdout_buf = NULL;
    gchar *stderr_buf = NULL;
    GError *error = NULL;
    gint status = 0;
    gboolean ok;

    if (stdout_out)
        *stdout_out = NULL;

    g_mutex_lock(&db_mutex);
    ok = g_spawn_sync(
            NULL,
            argv,
            NULL,
            G_SPAWN_SEARCH_PATH,
            NULL,
            NULL,
            stdout_out ? &stdout_buf : NULL,
            &stderr_buf,
            &status,
            &error);
    g_mutex_unlock(&db_mutex);

    if (!ok) {
        g_printerr("sqlite3 failed: %s\n", error ? error->message : "unknown");
        if (error)
            g_error_free(error);
        g_free(stderr_buf);
        return FALSE;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        g_printerr("sqlite3 sql failed: %s\n", stderr_buf ? stderr_buf : sql);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return FALSE;
    }

    if (stdout_out)
        *stdout_out = stdout_buf;
    else
        g_free(stdout_buf);
    g_free(stderr_buf);
    return TRUE;
}

gboolean db_init(const gchar *path)
{
    gchar *dir;
    const gchar *schema =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "direction TEXT NOT NULL,"
        "phone TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "timestamp INTEGER NOT NULL,"
        "status TEXT DEFAULT 'ok'"
        ");";

    if (path && *path)
        g_strlcpy(db_path, path, sizeof(db_path));

    dir = g_path_get_dirname(db_path);
    if (dir && g_strcmp0(dir, ".") != 0 &&
            g_mkdir_with_parents(dir, 0755) != 0) {
        g_printerr("create database directory %s failed: %s\n",
                dir,
                g_strerror(errno));
        g_free(dir);
        return FALSE;
    }
    g_free(dir);

    return sqlite_run(schema, NULL);
}

gboolean db_add_message(
    const gchar *direction,
    const gchar *phone,
    const gchar *content,
    const gchar *status)
{
    gchar *safe_direction = sql_escape(direction);
    gchar *safe_phone = sql_escape(phone);
    gchar *safe_content = sql_escape(content);
    gchar *safe_status = sql_escape(status);
    gchar *sql;
    gboolean ok;

    sql = g_strdup_printf(
            "INSERT INTO messages (direction, phone, content, timestamp, status) "
            "VALUES ('%s', '%s', '%s', %ld, '%s');",
            safe_direction,
            safe_phone,
            safe_content,
            (long)time(NULL),
            safe_status);

    ok = sqlite_run(sql, NULL);

    g_free(sql);
    g_free(safe_direction);
    g_free(safe_phone);
    g_free(safe_content);
    g_free(safe_status);
    return ok;
}

static void append_message_rows(GPtrArray *messages, const gchar *output)
{
    gchar **lines;

    if (!output || !*output)
        return;

    lines = g_strsplit(output, "\n", -1);
    for (gint i = 0; lines[i]; i++) {
        gchar **fields;
        Message *msg;
        gchar *phone;
        gchar *content;

        if (!*lines[i])
            continue;

        fields = g_strsplit(lines[i], "|", 6);
        if (!fields[0] || !fields[1] || !fields[2] ||
                !fields[3] || !fields[4] || !fields[5]) {
            g_strfreev(fields);
            continue;
        }

        phone = hex_decode_alloc(fields[2]);
        content = hex_decode_alloc(fields[3]);
        msg = g_new0(Message, 1);
        msg->id = atoi(fields[0]);
        g_strlcpy(msg->direction, fields[1], sizeof(msg->direction));
        g_strlcpy(msg->phone, phone, sizeof(msg->phone));
        g_strlcpy(msg->content, content, sizeof(msg->content));
        msg->timestamp = (time_t)atol(fields[4]);
        g_strlcpy(msg->status, fields[5], sizeof(msg->status));

        g_ptr_array_add(messages, msg);

        g_free(phone);
        g_free(content);
        g_strfreev(fields);
    }

    g_strfreev(lines);
}

GPtrArray *db_get_messages_page(
    const gchar *direction,
    gint page,
    gint page_size)
{
    GPtrArray *messages = g_ptr_array_new_with_free_func(g_free);
    const gchar *normalized_direction = normalize_direction(direction);
    gchar *sql;
    gchar *output = NULL;
    gint offset;

    if (page < 1)
        page = 1;
    if (page_size <= 0 || page_size > 50)
        page_size = 10;
    offset = (page - 1) * page_size;

    if (normalized_direction) {
        sql = g_strdup_printf(
                "SELECT id || '|' || direction || '|' || hex(phone) || '|' || "
                "hex(content) || '|' || timestamp || '|' || status "
                "FROM messages WHERE direction='%s' "
                "ORDER BY id DESC LIMIT %d OFFSET %d;",
                normalized_direction,
                page_size,
                offset);
    } else {
        sql = g_strdup_printf(
                "SELECT id || '|' || direction || '|' || hex(phone) || '|' || "
                "hex(content) || '|' || timestamp || '|' || status "
                "FROM messages ORDER BY id DESC LIMIT %d OFFSET %d;",
                page_size,
                offset);
    }

    if (!sqlite_run(sql, &output)) {
        g_free(sql);
        return messages;
    }

    append_message_rows(messages, output);

    g_free(output);
    g_free(sql);
    return messages;
}

GPtrArray *db_get_messages(gint limit)
{
    return db_get_messages_page(NULL, 1, limit);
}

gint db_count_messages(const gchar *direction)
{
    const gchar *normalized_direction = normalize_direction(direction);
    gchar *sql;
    gchar *output = NULL;
    gint count = 0;

    if (normalized_direction) {
        sql = g_strdup_printf(
                "SELECT COUNT(*) FROM messages WHERE direction='%s';",
                normalized_direction);
    } else {
        sql = g_strdup("SELECT COUNT(*) FROM messages;");
    }

    if (sqlite_run(sql, &output) && output)
        count = atoi(output);

    g_free(output);
    g_free(sql);
    return count;
}

Message *db_get_message(gint id)
{
    GPtrArray *messages;
    Message *message = NULL;
    gchar *sql;
    gchar *output = NULL;

    if (id <= 0)
        return NULL;

    messages = g_ptr_array_new();
    sql = g_strdup_printf(
            "SELECT id || '|' || direction || '|' || hex(phone) || '|' || "
            "hex(content) || '|' || timestamp || '|' || status "
            "FROM messages WHERE id=%d LIMIT 1;",
            id);

    if (sqlite_run(sql, &output)) {
        append_message_rows(messages, output);
        if (messages->len > 0)
            message = g_ptr_array_index(messages, 0);
    }

    for (guint i = message ? 1 : 0; i < messages->len; i++)
        g_free(g_ptr_array_index(messages, i));
    g_ptr_array_free(messages, TRUE);
    g_free(output);
    g_free(sql);
    return message;
}

gboolean db_delete_messages(const gchar *ids_csv)
{
    GString *ids = g_string_new("");
    gchar **parts;
    gchar *sql;
    gboolean ok;

    if (!ids_csv || !*ids_csv) {
        g_string_free(ids, TRUE);
        return FALSE;
    }

    parts = g_strsplit(ids_csv, ",", -1);
    for (gint i = 0; parts[i]; i++) {
        gint id = atoi(parts[i]);

        if (id <= 0)
            continue;

        if (ids->len > 0)
            g_string_append_c(ids, ',');
        g_string_append_printf(ids, "%d", id);
    }
    g_strfreev(parts);

    if (ids->len == 0) {
        g_string_free(ids, TRUE);
        return FALSE;
    }

    sql = g_strdup_printf("DELETE FROM messages WHERE id IN (%s);", ids->str);
    ok = sqlite_run(sql, NULL);

    g_free(sql);
    g_string_free(ids, TRUE);
    return ok;
}

gboolean db_clear_messages(const gchar *direction)
{
    const gchar *normalized_direction = normalize_direction(direction);
    gchar *sql;
    gboolean ok;

    if (normalized_direction) {
        sql = g_strdup_printf(
                "DELETE FROM messages WHERE direction='%s';",
                normalized_direction);
    } else {
        sql = g_strdup("DELETE FROM messages;");
    }

    ok = sqlite_run(sql, NULL);
    g_free(sql);
    return ok;
}
