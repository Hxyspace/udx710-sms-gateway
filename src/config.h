#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

typedef struct {
    gchar device_id[64];
    gchar notify_host[128];
    guint16 notify_port;
    gchar token[128];
    gboolean notify_enabled;
    gboolean notify_host_detected;
    gboolean notify_host_configured;
} DeviceConfig;

gboolean config_load(DeviceConfig *config);

#endif
