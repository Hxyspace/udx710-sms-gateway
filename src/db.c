#include "db.h"

#include <errno.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <stdlib.h>

static gchar db_path[256] = "/home/root/network/messages.db";
static sqlite3 *db = NULL;
static GMutex db_mutex;

static const gchar *normalize_direction(const gchar *direction)
{
    if (g_strcmp0(direction, "in") == 0)
        return "in";
    if (g_strcmp0(direction, "out") == 0)
        return "out";
    return NULL;
}

static gboolean db_exec(const gchar *sql)
{
    gchar *error = NULL;
    gint rc = sqlite3_exec(db, sql, NULL, NULL, &error);

    if (rc != SQLITE_OK) {
        g_printerr("sqlite exec failed: %s\n", error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        return FALSE;
    }

    return TRUE;
}

static gboolean prepare_stmt(const gchar *sql, sqlite3_stmt **stmt)
{
    gint rc = sqlite3_prepare_v2(db, sql, -1, stmt, NULL);

    if (rc != SQLITE_OK) {
        g_printerr("sqlite prepare failed: %s\n", sqlite3_errmsg(db));
        return FALSE;
    }

    return TRUE;
}

static void fill_message_from_stmt(Message *msg, sqlite3_stmt *stmt)
{
    const guchar *text;

    msg->id = sqlite3_column_int(stmt, 0);

    text = sqlite3_column_text(stmt, 1);
    g_strlcpy(msg->direction, text ? (const gchar *)text : "", sizeof(msg->direction));

    text = sqlite3_column_text(stmt, 2);
    g_strlcpy(msg->phone, text ? (const gchar *)text : "", sizeof(msg->phone));

    text = sqlite3_column_text(stmt, 3);
    g_strlcpy(msg->content, text ? (const gchar *)text : "", sizeof(msg->content));

    msg->timestamp = (time_t)sqlite3_column_int64(stmt, 4);

    text = sqlite3_column_text(stmt, 5);
    g_strlcpy(msg->status, text ? (const gchar *)text : "", sizeof(msg->status));
}

static GPtrArray *query_messages(
    const gchar *sql,
    const gchar *direction,
    gint page_size,
    gint offset)
{
    GPtrArray *messages = g_ptr_array_new_with_free_func(g_free);
    sqlite3_stmt *stmt = NULL;
    gint bind_index = 1;

    g_mutex_lock(&db_mutex);
    if (!prepare_stmt(sql, &stmt)) {
        g_mutex_unlock(&db_mutex);
        return messages;
    }

    if (direction)
        sqlite3_bind_text(stmt, bind_index++, direction, -1, SQLITE_STATIC);
    if (page_size > 0)
        sqlite3_bind_int(stmt, bind_index++, page_size);
    if (offset >= 0)
        sqlite3_bind_int(stmt, bind_index++, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message *msg = g_new0(Message, 1);
        fill_message_from_stmt(msg, stmt);
        g_ptr_array_add(messages, msg);
    }

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_mutex);
    return messages;
}

gboolean db_init(const gchar *path)
{
    gchar *dir;
    gint rc;
    gboolean ok;

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

    g_mutex_lock(&db_mutex);
    if (db) {
        g_mutex_unlock(&db_mutex);
        return TRUE;
    }

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        g_printerr("sqlite open %s failed: %s\n",
                db_path,
                db ? sqlite3_errmsg(db) : "unknown");
        if (db) {
            sqlite3_close(db);
            db = NULL;
        }
        g_mutex_unlock(&db_mutex);
        return FALSE;
    }

    sqlite3_busy_timeout(db, 3000);
    ok = db_exec(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "direction TEXT NOT NULL,"
            "phone TEXT NOT NULL,"
            "content TEXT NOT NULL,"
            "timestamp INTEGER NOT NULL,"
            "status TEXT DEFAULT 'ok'"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_msg_dir_id "
            "ON messages(direction, id DESC);");

    g_mutex_unlock(&db_mutex);
    return ok;
}

gint db_add_message(
    const gchar *direction,
    const gchar *phone,
    const gchar *content,
    const gchar *status)
{
    sqlite3_stmt *stmt = NULL;
    gint id = 0;

    g_mutex_lock(&db_mutex);
    if (!prepare_stmt(
                "INSERT INTO messages "
                "(direction, phone, content, timestamp, status) "
                "VALUES (?, ?, ?, ?, ?);",
                &stmt)) {
        g_mutex_unlock(&db_mutex);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, direction ? direction : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, phone ? phone : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content ? content : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(stmt, 5, status ? status : "ok", -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_DONE)
        id = (gint)sqlite3_last_insert_rowid(db);
    else
        g_printerr("sqlite insert message failed: %s\n", sqlite3_errmsg(db));

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_mutex);
    return id;
}

GPtrArray *db_get_messages_page(
    const gchar *direction,
    gint page,
    gint page_size)
{
    const gchar *normalized_direction = normalize_direction(direction);
    gint offset;

    if (page < 1)
        page = 1;
    if (page_size <= 0 || page_size > 500)
        page_size = 10;
    offset = (page - 1) * page_size;

    if (normalized_direction) {
        return query_messages(
                "SELECT id, direction, phone, content, timestamp, status "
                "FROM messages WHERE direction=? "
                "ORDER BY id DESC LIMIT ? OFFSET ?;",
                normalized_direction,
                page_size,
                offset);
    }

    return query_messages(
            "SELECT id, direction, phone, content, timestamp, status "
            "FROM messages ORDER BY id DESC LIMIT ? OFFSET ?;",
            NULL,
            page_size,
            offset);
}

GPtrArray *db_get_messages(gint limit)
{
    return db_get_messages_page(NULL, 1, limit);
}

GPtrArray *db_get_messages_all(const gchar *direction)
{
    const gchar *normalized_direction = normalize_direction(direction);

    if (normalized_direction) {
        return query_messages(
                "SELECT id, direction, phone, content, timestamp, status "
                "FROM messages WHERE direction=? ORDER BY id DESC;",
                normalized_direction,
                0,
                -1);
    }

    return query_messages(
            "SELECT id, direction, phone, content, timestamp, status "
            "FROM messages ORDER BY id DESC;",
            NULL,
            0,
            -1);
}

gint db_count_messages(const gchar *direction)
{
    const gchar *normalized_direction = normalize_direction(direction);
    sqlite3_stmt *stmt = NULL;
    gint count = 0;

    g_mutex_lock(&db_mutex);
    if (!prepare_stmt(
                normalized_direction ?
                "SELECT COUNT(*) FROM messages WHERE direction=?;" :
                "SELECT COUNT(*) FROM messages;",
                &stmt)) {
        g_mutex_unlock(&db_mutex);
        return 0;
    }

    if (normalized_direction)
        sqlite3_bind_text(stmt, 1, normalized_direction, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_mutex);
    return count;
}

Message *db_get_message(gint id)
{
    sqlite3_stmt *stmt = NULL;
    Message *message = NULL;

    if (id <= 0)
        return NULL;

    g_mutex_lock(&db_mutex);
    if (!prepare_stmt(
                "SELECT id, direction, phone, content, timestamp, status "
                "FROM messages WHERE id=? LIMIT 1;",
                &stmt)) {
        g_mutex_unlock(&db_mutex);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        message = g_new0(Message, 1);
        fill_message_from_stmt(message, stmt);
    }

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_mutex);
    return message;
}

gboolean db_delete_messages(const gchar *ids_csv)
{
    sqlite3_stmt *stmt = NULL;
    gchar **parts;
    gboolean ok = TRUE;
    gboolean any = FALSE;

    if (!ids_csv || !*ids_csv)
        return FALSE;

    parts = g_strsplit(ids_csv, ",", -1);

    g_mutex_lock(&db_mutex);
    if (!db_exec("BEGIN IMMEDIATE;")) {
        g_mutex_unlock(&db_mutex);
        g_strfreev(parts);
        return FALSE;
    }

    if (!prepare_stmt("DELETE FROM messages WHERE id=?;", &stmt)) {
        db_exec("ROLLBACK;");
        g_mutex_unlock(&db_mutex);
        g_strfreev(parts);
        return FALSE;
    }

    for (gint i = 0; parts[i]; i++) {
        gint id = atoi(parts[i]);

        if (id <= 0)
            continue;

        any = TRUE;
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int(stmt, 1, id);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            g_printerr("sqlite delete message failed: %s\n", sqlite3_errmsg(db));
            ok = FALSE;
            break;
        }
    }

    sqlite3_finalize(stmt);
    db_exec(ok ? "COMMIT;" : "ROLLBACK;");
    g_mutex_unlock(&db_mutex);
    g_strfreev(parts);
    return ok && any;
}

gboolean db_clear_messages(const gchar *direction)
{
    const gchar *normalized_direction = normalize_direction(direction);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;

    g_mutex_lock(&db_mutex);
    if (!prepare_stmt(
                normalized_direction ?
                "DELETE FROM messages WHERE direction=?;" :
                "DELETE FROM messages;",
                &stmt)) {
        g_mutex_unlock(&db_mutex);
        return FALSE;
    }

    if (normalized_direction)
        sqlite3_bind_text(stmt, 1, normalized_direction, -1, SQLITE_STATIC);

    ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        g_printerr("sqlite clear messages failed: %s\n", sqlite3_errmsg(db));

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_mutex);
    return ok;
}
