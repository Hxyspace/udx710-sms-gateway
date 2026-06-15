#ifndef HTTP_H
#define HTTP_H

#include <gio/gio.h>

gboolean http_server_start(guint16 port);
void http_server_stop(void);

#endif
