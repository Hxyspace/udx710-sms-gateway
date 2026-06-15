#ifndef SMS_H
#define SMS_H

#include <gio/gio.h>

gboolean sms_init(GDBusConnection *bus);
void sms_deinit(void);
gboolean sms_send_message(
    const gchar *recipient,
    const gchar *content,
    gchar *result_path,
    gsize path_size);

#endif
