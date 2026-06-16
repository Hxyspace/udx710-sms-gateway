#ifndef DB_H
#define DB_H

#include <glib.h>
#include <time.h>

typedef struct {
    gint id;
    gchar direction[8];
    gchar phone[64];
    gchar content[1024];
    time_t timestamp;
    gchar status[32];
} Message;

gboolean db_init(const gchar *path);
gint db_add_message(
    const gchar *direction,
    const gchar *phone,
    const gchar *content,
    const gchar *status);
GPtrArray *db_get_messages(gint limit);
GPtrArray *db_get_messages_page(
    const gchar *direction,
    gint page,
    gint page_size);
GPtrArray *db_get_messages_all(const gchar *direction);
gint db_count_messages(const gchar *direction);
Message *db_get_message(gint id);
gboolean db_delete_messages(const gchar *ids_csv);
gboolean db_clear_messages(const gchar *direction);

#endif
