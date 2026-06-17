#include "assets.h"

#include <string.h>

extern const guint8 _binary_web_index_html_start[];
extern const guint8 _binary_web_index_html_end[];
extern const guint8 _binary_web_styles_css_start[];
extern const guint8 _binary_web_styles_css_end[];
extern const guint8 _binary_web_app_js_start[];
extern const guint8 _binary_web_app_js_end[];
extern const guint8 _binary_web_vendor_layui_js_start[];
extern const guint8 _binary_web_vendor_layui_js_end[];
extern const guint8 _binary_web_vendor_css_layui_css_start[];
extern const guint8 _binary_web_vendor_css_layui_css_end[];
extern const guint8 _binary_web_vendor_font_iconfont_woff2_start[];
extern const guint8 _binary_web_vendor_font_iconfont_woff2_end[];

#define ASSET_SIZE(start, end) ((gsize)((end) - (start)))

static Asset assets[] = {
    {
        "/index.html",
        "text/html; charset=utf-8",
        _binary_web_index_html_start,
        0,
    },
    {
        "/styles.css",
        "text/css; charset=utf-8",
        _binary_web_styles_css_start,
        0,
    },
    {
        "/app.js",
        "application/javascript; charset=utf-8",
        _binary_web_app_js_start,
        0,
    },
    {
        "/layui.js",
        "application/javascript; charset=utf-8",
        _binary_web_vendor_layui_js_start,
        0,
    },
    {
        "/vendor/css/layui.css",
        "text/css; charset=utf-8",
        _binary_web_vendor_css_layui_css_start,
        0,
    },
    {
        "/vendor/font/iconfont.woff2",
        "font/woff2",
        _binary_web_vendor_font_iconfont_woff2_start,
        0,
    },
};

static void init_asset_sizes(void)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    assets[0].size = ASSET_SIZE(
            _binary_web_index_html_start,
            _binary_web_index_html_end);
    assets[1].size = ASSET_SIZE(
            _binary_web_styles_css_start,
            _binary_web_styles_css_end);
    assets[2].size = ASSET_SIZE(
            _binary_web_app_js_start,
            _binary_web_app_js_end);
    assets[3].size = ASSET_SIZE(
            _binary_web_vendor_layui_js_start,
            _binary_web_vendor_layui_js_end);
    assets[4].size = ASSET_SIZE(
            _binary_web_vendor_css_layui_css_start,
            _binary_web_vendor_css_layui_css_end);
    assets[5].size = ASSET_SIZE(
            _binary_web_vendor_font_iconfont_woff2_start,
            _binary_web_vendor_font_iconfont_woff2_end);
    initialized = TRUE;
}

const Asset *asset_lookup(const gchar *path)
{
    gsize path_len;

    if (!path || !*path)
        return NULL;

    init_asset_sizes();

    path_len = strcspn(path, "?");
    if (path_len == 1 && path[0] == '/')
        return &assets[0];

    for (guint i = 0; i < G_N_ELEMENTS(assets); i++) {
        if (strlen(assets[i].path) == path_len &&
                strncmp(path, assets[i].path, path_len) == 0) {
            return &assets[i];
        }
    }

    return NULL;
}
