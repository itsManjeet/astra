#include "app.h"

#include "downloads.h"

gboolean app_array_contains(GPtrArray *array, const gchar *value) {
    for (guint i = 0; i < array->len; i++)
        if (g_strcmp0(g_ptr_array_index(array, i), value) == 0)
            return TRUE;
    return FALSE;
}

static void config_defaults(AppConfig *c) {
    c->homepage = g_strdup("https://duckduckgo.com/");
    c->search_url = g_strdup("https://duckduckgo.com/?q=%s");
    c->user_agent = g_strdup("");
    c->default_zoom = 1.0;
    c->javascript = TRUE;
    c->images = TRUE;
    c->webgl = TRUE;
    c->media_stream = TRUE;
    c->autoplay = FALSE;
    c->developer_tools = FALSE;
    c->smooth_scrolling = TRUE;
    c->zoom_text_only = FALSE;
    c->dark_ui = FALSE;
}

static void load_string_list(GKeyFile *key, const gchar *name, GPtrArray *out) {
    gsize n = 0;
    gchar **items = g_key_file_get_string_list(key, "data", name, &n, NULL);
    for (gsize i = 0; items && i < n; i++)
        if (*items[i] && !app_array_contains(out, items[i]))
            g_ptr_array_add(out, g_strdup(items[i]));
    g_strfreev(items);
}

static void init_web_storage_paths(App *app) {
    app->data_dir = g_build_filename(g_get_user_data_dir(), "astra", "webkit", NULL);
    app->cache_dir = g_build_filename(g_get_user_cache_dir(), "astra", "webkit", NULL);
    app->cookie_path = g_build_filename(app->data_dir, "cookies.sqlite", NULL);

    g_mkdir_with_parents(app->data_dir, 0700);
    g_mkdir_with_parents(app->cache_dir, 0700);
}

static gboolean key_bool(GKeyFile *key, const gchar *name, gboolean fallback) {
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(key, "browser", name, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static gdouble key_double(GKeyFile *key, const gchar *name, gdouble fallback) {
    GError *error = NULL;
    gdouble value = g_key_file_get_double(key, "browser", name, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

void app_load(App *app) {
    config_defaults(&app->config);
    app->bookmarks = g_ptr_array_new_with_free_func(g_free);
    app->history = g_ptr_array_new_with_free_func(g_free);
    app->completion_store = gtk_list_store_new(1, G_TYPE_STRING);
    downloads_init(app);
    init_web_storage_paths(app);

    gchar *dir = g_build_filename(g_get_user_config_dir(), "astra", NULL);
    g_mkdir_with_parents(dir, 0700);
    app->config_path = g_build_filename(dir, "settings.ini", NULL);
    g_free(dir);

    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, app->config_path, G_KEY_FILE_NONE, NULL)) {
        gchar *s = g_key_file_get_string(key, "browser", "homepage", NULL);
        if (s) {
            g_free(app->config.homepage);
            app->config.homepage = s;
        }
        s = g_key_file_get_string(key, "browser", "search_url", NULL);
        if (s) {
            g_free(app->config.search_url);
            app->config.search_url = s;
        }
        s = g_key_file_get_string(key, "browser", "user_agent", NULL);
        if (s) {
            g_free(app->config.user_agent);
            app->config.user_agent = s;
        }
        app->config.default_zoom = key_double(key, "default_zoom", 1.0);
        app->config.javascript = key_bool(key, "javascript", TRUE);
        app->config.images = key_bool(key, "images", TRUE);
        app->config.webgl = key_bool(key, "webgl", TRUE);
        app->config.media_stream = key_bool(key, "media_stream", TRUE);
        app->config.autoplay = key_bool(key, "autoplay", FALSE);
        app->config.developer_tools = key_bool(key, "developer_tools", FALSE);
        app->config.smooth_scrolling = key_bool(key, "smooth_scrolling", TRUE);
        app->config.zoom_text_only = key_bool(key, "zoom_text_only", FALSE);
        app->config.dark_ui = key_bool(key, "dark_ui", FALSE);
        load_string_list(key, "bookmarks", app->bookmarks);
        load_string_list(key, "history", app->history);
    }
    g_key_file_unref(key);

    app_rebuild_completion(app);
}

void app_rebuild_completion(App *app) {
    gtk_list_store_clear(app->completion_store);
    for (guint i = 0; i < app->history->len; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(app->completion_store, &iter);
        gtk_list_store_set(app->completion_store, &iter, 0,
                           g_ptr_array_index(app->history, i), -1);
    }
}

void app_save(App *app) {
    GKeyFile *key = g_key_file_new();
    AppConfig *c = &app->config;
    g_key_file_set_string(key, "browser", "homepage", c->homepage);
    g_key_file_set_string(key, "browser", "search_url", c->search_url);
    g_key_file_set_string(key, "browser", "user_agent", c->user_agent);
    g_key_file_set_double(key, "browser", "default_zoom", c->default_zoom);
    g_key_file_set_boolean(key, "browser", "javascript", c->javascript);
    g_key_file_set_boolean(key, "browser", "images", c->images);
    g_key_file_set_boolean(key, "browser", "webgl", c->webgl);
    g_key_file_set_boolean(key, "browser", "media_stream", c->media_stream);
    g_key_file_set_boolean(key, "browser", "autoplay", c->autoplay);
    g_key_file_set_boolean(key, "browser", "developer_tools", c->developer_tools);
    g_key_file_set_boolean(key, "browser", "smooth_scrolling", c->smooth_scrolling);
    g_key_file_set_boolean(key, "browser", "zoom_text_only", c->zoom_text_only);
    g_key_file_set_boolean(key, "browser", "dark_ui", c->dark_ui);
    if (app->bookmarks->len)
        g_key_file_set_string_list(key, "data", "bookmarks",
                                   (const gchar *const *)app->bookmarks->pdata,
                                   app->bookmarks->len);
    if (app->history->len)
        g_key_file_set_string_list(key, "data", "history",
                                   (const gchar *const *)app->history->pdata,
                                   app->history->len);

    gsize length;
    gchar *data = g_key_file_to_data(key, &length, NULL);
    g_file_set_contents(app->config_path, data, length, NULL);
    g_free(data);
    g_key_file_unref(key);
}

void app_clear(App *app) {
    g_clear_object(&app->context);
    downloads_clear(app);
    g_ptr_array_unref(app->bookmarks);
    g_ptr_array_unref(app->history);
    g_object_unref(app->completion_store);
    g_free(app->config.homepage);
    g_free(app->config.search_url);
    g_free(app->config.user_agent);
    g_free(app->config_path);
    g_free(app->data_dir);
    g_free(app->cache_dir);
    g_free(app->cookie_path);
}
