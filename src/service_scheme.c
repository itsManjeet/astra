#include "service_scheme.h"

typedef struct {
    GHashTable *cache;
} ServiceScheme;

static void service_scheme_free(gpointer data) {
    ServiceScheme *scheme = data;
    g_hash_table_unref(scheme->cache);
    g_free(scheme);
}

static GdkPixbuf *load_theme_icon(const gchar *name, gint size,
                                  gboolean symbolic, GError **error) {
    gchar *resolved = symbolic && !g_str_has_suffix(name, "-symbolic")
        ? g_strdup_printf("%s-symbolic", name) : g_strdup(name);
    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(
        gtk_icon_theme_get_default(), resolved, size,
        GTK_ICON_LOOKUP_FORCE_SIZE, error);
    if (!pixbuf && symbolic) {
        g_clear_error(error);
        pixbuf = gtk_icon_theme_load_icon(
            gtk_icon_theme_get_default(), name, size,
            GTK_ICON_LOOKUP_FORCE_SIZE, error);
    }
    g_free(resolved);
    return pixbuf;
}

static GdkPixbuf *load_mime_icon(const gchar *content_type, gint size,
                                 GError **error) {
    GIcon *icon = g_content_type_get_icon(content_type);
    GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon(
        gtk_icon_theme_get_default(), icon, size, GTK_ICON_LOOKUP_FORCE_SIZE);
    g_object_unref(icon);
    if (!info) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "No icon for content type %s", content_type);
        return NULL;
    }
    GdkPixbuf *pixbuf = gtk_icon_info_load_icon(info, error);
    g_object_unref(info);
    return pixbuf;
}

static gboolean parse_boolean(const gchar *value) {
    return value && (g_ascii_strcasecmp(value, "true") == 0 ||
                     g_str_equal(value, "1"));
}

static GBytes *render_system_icon(GUri *uri, GError **error) {
    const gchar *path = g_uri_get_path(uri);
    const gchar *query = g_uri_get_query(uri);
    GHashTable *params = query
        ? g_uri_parse_params(query, -1, "&", G_URI_PARAMS_NONE, error)
        : NULL;
    if (!params)
        return NULL;

    const gchar *name = g_hash_table_lookup(params, "name");
    const gchar *type = g_hash_table_lookup(params, "type");
    const gchar *size_text = g_hash_table_lookup(params, "size");
    const gchar *symbolic_text = g_hash_table_lookup(params, "symbolic");
    gint size = size_text ? CLAMP((gint)g_ascii_strtoll(size_text, NULL, 10),
                                  16, 256) : 64;
    gboolean symbolic = parse_boolean(symbolic_text);
    GdkPixbuf *pixbuf = NULL;
    if (g_strcmp0(path, "/theme/icon") == 0 && name) {
        pixbuf = load_theme_icon(name, size, symbolic, error);
    } else if (g_strcmp0(path, "/mime/icon") == 0 && type) {
        pixbuf = load_mime_icon(type, size, error);
    } else {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Unknown dev.avyos.system service route");
    }
    g_hash_table_unref(params);
    if (!pixbuf) {
        g_clear_error(error);
        pixbuf = load_theme_icon("text-x-generic", size, symbolic, error);
    }
    if (!pixbuf)
        return NULL;

    gchar *buffer = NULL;
    gsize length = 0;
    gboolean saved = gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &length,
                                                "png", error, NULL);
    g_object_unref(pixbuf);
    return saved ? g_bytes_new_take(buffer, length) : NULL;
}

static void service_request(WebKitURISchemeRequest *request, gpointer data) {
    ServiceScheme *scheme = data;
    const gchar *request_uri = webkit_uri_scheme_request_get_uri(request);
    GBytes *bytes = g_hash_table_lookup(scheme->cache, request_uri);
    if (!bytes) {
        GError *error = NULL;
        GUri *uri = g_uri_parse(request_uri, G_URI_FLAGS_NONE, &error);
        const gchar *service = uri ? g_uri_get_host(uri) : NULL;
        if (uri && g_strcmp0(service, "dev.avyos.system") == 0)
            bytes = render_system_icon(uri, &error);
        else if (!error)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                "Unknown service provider");
        if (bytes)
            g_hash_table_insert(scheme->cache, g_strdup(request_uri), bytes);
        if (uri)
            g_uri_unref(uri);
        if (error) {
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }
    }

    gsize length;
    g_bytes_get_data(bytes, &length);
    GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
    webkit_uri_scheme_request_finish(request, stream, length, "image/png");
    g_object_unref(stream);
}

void service_scheme_register(WebKitWebContext *context) {
    static GQuark registered_quark = 0;
    if (!registered_quark)
        registered_quark = g_quark_from_static_string(
            "astra-service-scheme-registered");
    if (g_object_get_qdata(G_OBJECT(context), registered_quark))
        return;

    ServiceScheme *scheme = g_new0(ServiceScheme, 1);
    scheme->cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                          (GDestroyNotify)g_bytes_unref);
    webkit_web_context_register_uri_scheme(context, "service", service_request,
                                           scheme, service_scheme_free);
    g_object_set_qdata(G_OBJECT(context), registered_quark, GINT_TO_POINTER(1));
}
