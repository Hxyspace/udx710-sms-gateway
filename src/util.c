#include "util.h"

#include <stdlib.h>
#include <sys/wait.h>

gboolean run_command_checked(const gchar *cmd)
{
    int status = system(cmd);

    if (status == -1) {
        g_printerr("command failed to start: %s\n", cmd);
        return FALSE;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        g_printerr("command failed: %s (status=%d)\n", cmd, status);
        return FALSE;
    }

    return TRUE;
}

void json_append_escaped(GString *json, const gchar *text)
{
    const guchar *p = (const guchar *)(text ? text : "");

    g_string_append_c(json, '"');
    while (*p) {
        switch (*p) {
        case '"':
            g_string_append(json, "\\\"");
            break;
        case '\\':
            g_string_append(json, "\\\\");
            break;
        case '\b':
            g_string_append(json, "\\b");
            break;
        case '\f':
            g_string_append(json, "\\f");
            break;
        case '\n':
            g_string_append(json, "\\n");
            break;
        case '\r':
            g_string_append(json, "\\r");
            break;
        case '\t':
            g_string_append(json, "\\t");
            break;
        default:
            if (*p < 0x20)
                g_string_append_printf(json, "\\u%04x", *p);
            else
                g_string_append_c(json, *p);
            break;
        }
        p++;
    }
    g_string_append_c(json, '"');
}

gchar *form_value_decode(const gchar *encoded)
{
    gchar *copy;
    gchar *decoded;

    if (!encoded)
        return g_strdup("");

    copy = g_strdup(encoded);
    for (gchar *p = copy; *p; p++) {
        if (*p == '+')
            *p = ' ';
    }

    decoded = g_uri_unescape_string(copy, NULL);
    g_free(copy);
    return decoded ? decoded : g_strdup("");
}
