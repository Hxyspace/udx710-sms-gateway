#ifndef ASSETS_H
#define ASSETS_H

#include <glib.h>

typedef struct {
    const gchar *path;
    const gchar *content_type;
    const guint8 *data;
    gsize size;
} Asset;

const Asset *asset_lookup(const gchar *path);

#endif
