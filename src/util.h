#ifndef UTIL_H
#define UTIL_H

#include <glib.h>

gboolean run_command_checked(const gchar *cmd);
void json_append_escaped(GString *json, const gchar *text);
gchar *form_value_decode(const gchar *encoded);

#endif
