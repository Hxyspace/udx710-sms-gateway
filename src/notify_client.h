#ifndef NOTIFY_CLIENT_H
#define NOTIFY_CLIENT_H

#include "config.h"
#include "db.h"

#include <glib.h>

void notify_client_init(const DeviceConfig *config);
void notify_client_send_message(const Message *message);

#endif
