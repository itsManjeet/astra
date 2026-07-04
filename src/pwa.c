#include "pwa.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar *pwa_host_from_uri(const gchar *uri) {
    if (!uri)
        return g_strdup("webapp");

    const gchar *start = strstr(uri, "://");
    start = start ? start + 3 : uri;
    while (*start == '/')
        start++;

    const gchar *end = start;
    while (*end && *end != '/' && *end != '?' && *end != '#')
        end++;

    gchar *host = g_strndup(start, end - start);
    gchar *at = strrchr(host, '@');
    if (at)
        memmove(host, at + 1, strlen(at));

    if (host[0] == '[') {
        gchar *closing = strchr(host, ']');
        if (closing)
            closing[1] = '\0';
    } else {
        gchar *colon = strchr(host, ':');
        if (colon)
            *colon = '\0';
    }

    if (!*host) {
        g_free(host);
        return g_strdup("webapp");
    }
    return host;
}

static gchar *pwa_slugify(const gchar *text) {
    GString *out = g_string_new(NULL);
    gboolean last_sep = FALSE;

    for (const gchar *p = text && *text ? text : "webapp"; *p; p++) {
        gchar c = *p;
        if (g_ascii_isalnum(c)) {
            g_string_append_c(out, g_ascii_tolower(c));
            last_sep = FALSE;
        } else if (!last_sep && out->len > 0) {
            /* Keep generated app IDs valid D-Bus/GApplication IDs. */
            g_string_append_c(out, '_');
            last_sep = TRUE;
        }
        if (out->len >= 48)
            break;
    }

    while (out->len > 0 && out->str[out->len - 1] == '_')
        g_string_truncate(out, out->len - 1);
    if (out->len == 0)
        g_string_append(out, "webapp");
    if (g_ascii_isdigit(out->str[0]))
        g_string_prepend(out, "app_");
    return g_string_free(out, FALSE);
}

gchar *pwa_app_id_for_uri(const gchar *uri, const gchar *name) {
    gchar *slug = pwa_slugify(name);
    gchar *sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
                                               uri ? uri : "", -1);
    gchar hash[13];
    g_strlcpy(hash, sum, sizeof(hash));
    gchar *id = g_strdup_printf("%s%s.h%s", ASTRA_PWA_DESKTOP_PREFIX, slug, hash);
    g_free(sum);
    g_free(slug);
    return id;
}

gchar *pwa_runtime_app_id(const gchar *uri, const gchar *name,
                          const gchar *app_id) {
    if (app_id && *app_id && g_application_id_is_valid(app_id))
        return g_strdup(app_id);

    /* Older Astra builds generated IDs such as astra-webapp-foo-deadbeef,
     * which are fine for filenames but not valid GApplication/Wayland app IDs.
     * Generate a valid reverse-DNS ID so app-mode windows can get their own
     * taskbar group and desktop-file identity. */
    return pwa_app_id_for_uri(uri, name);
}

static gchar *pwa_executable_path(void) {
    GError *error = NULL;
    gchar *path = g_file_read_link("/proc/self/exe", &error);
    if (path) {
        g_clear_error(&error);
        return path;
    }
    g_clear_error(&error);

    const gchar *prg = g_get_prgname();
    if (prg && strchr(prg, G_DIR_SEPARATOR))
        return g_canonicalize_filename(prg, NULL);
    if (prg)
        return g_find_program_in_path(prg);
    return g_find_program_in_path("astra");
}

static gchar *pwa_icon_dir_for_size(gint size) {
    gchar *size_dir = g_strdup_printf("%dx%d", size, size);
    gchar *dir = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                  size_dir, "apps", NULL);
    g_free(size_dir);
    return dir;
}

static gchar *pwa_icon_path_for_size(const gchar *app_id, gint size) {
    gchar *size_dir = g_strdup_printf("%dx%d", size, size);
    gchar *path = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                   size_dir, "apps", app_id, NULL);
    gchar *with_ext = g_strconcat(path, ".png", NULL);
    g_free(path);
    g_free(size_dir);
    return with_ext;
}

gchar *pwa_icon_path_for_app_id(const gchar *app_id) {
    if (!app_id || !*app_id)
        return NULL;
    return pwa_icon_path_for_size(app_id, 256);
}

static void pwa_run_update_icon_cache(void) {
    gchar *tool = g_find_program_in_path("gtk-update-icon-cache");
    if (!tool)
        return;

    gchar *theme_dir = g_build_filename(g_get_user_data_dir(), "icons",
                                        "hicolor", NULL);
    gchar *argv[] = {tool, "-q", "-t", "-f", theme_dir, NULL};
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, NULL);
    g_free(theme_dir);
    g_free(tool);
}

static GdkPixbuf *pwa_pixbuf_from_surface(cairo_surface_t *surface) {
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return NULL;
    if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return NULL;

    cairo_surface_flush(surface);
    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);
    if (width <= 0 || height <= 0)
        return NULL;

    return gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
}

static GdkPixbuf *pwa_scale_icon(GdkPixbuf *pixbuf, gint size) {
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);
    if (width <= 0 || height <= 0)
        return NULL;

    gint target_w = size;
    gint target_h = size;
    if (width > height)
        target_h = MAX(1, (height * size) / width);
    else if (height > width)
        target_w = MAX(1, (width * size) / height);

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, target_w, target_h,
                                                GDK_INTERP_BILINEAR);
    if (!scaled)
        return NULL;

    GdkPixbuf *canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
    if (!canvas) {
        g_object_unref(scaled);
        return NULL;
    }

    gdk_pixbuf_fill(canvas, 0x00000000);
    gint x = (size - target_w) / 2;
    gint y = (size - target_h) / 2;
    gdk_pixbuf_copy_area(scaled, 0, 0, target_w, target_h, canvas, x, y);
    g_object_unref(scaled);
    return canvas;
}

static gchar *pwa_save_icon(const gchar *app_id, cairo_surface_t *surface) {
    GdkPixbuf *pixbuf = pwa_pixbuf_from_surface(surface);
    if (!pixbuf)
        return NULL;

    const gint sizes[] = {16, 32, 48, 64, 128, 256};
    gchar *largest_path = NULL;

    for (guint i = 0; i < G_N_ELEMENTS(sizes); i++) {
        gint size = sizes[i];
        GdkPixbuf *scaled = pwa_scale_icon(pixbuf, size);
        if (!scaled)
            continue;

        gchar *dir = pwa_icon_dir_for_size(size);
        g_mkdir_with_parents(dir, 0700);
        gchar *path = pwa_icon_path_for_size(app_id, size);

        GError *error = NULL;
        gboolean ok = gdk_pixbuf_save(scaled, path, "png", &error, NULL);
        g_object_unref(scaled);
        g_free(dir);

        if (ok) {
            if (size == 256) {
                g_free(largest_path);
                largest_path = g_strdup(path);
            }
        } else {
            g_warning("Could not save PWA icon %s: %s", path,
                      error ? error->message : "unknown error");
            g_clear_error(&error);
        }
        g_free(path);
    }

    g_object_unref(pixbuf);
    if (largest_path)
        pwa_run_update_icon_cache();
    return largest_path;
}

typedef struct {
    GMainLoop *loop;
    cairo_surface_t *surface;
} PwaFaviconLookup;

static void pwa_favicon_lookup_done(GObject *source, GAsyncResult *result,
                                    gpointer data) {
    PwaFaviconLookup *lookup = data;
    GError *error = NULL;
    lookup->surface = webkit_favicon_database_get_favicon_finish(
        WEBKIT_FAVICON_DATABASE(source), result, &error);
    if (error)
        g_clear_error(&error);
    g_main_loop_quit(lookup->loop);
}

static cairo_surface_t *pwa_lookup_favicon(App *app, const gchar *uri) {
    if (!app || !app->context || !uri || !*uri)
        return NULL;

    WebKitFaviconDatabase *db = webkit_web_context_get_favicon_database(app->context);
    if (!db)
        return NULL;

    PwaFaviconLookup lookup = {0};
    lookup.loop = g_main_loop_new(NULL, FALSE);
    webkit_favicon_database_get_favicon(db, uri, NULL,
                                         pwa_favicon_lookup_done, &lookup);
    g_main_loop_run(lookup.loop);
    g_main_loop_unref(lookup.loop);
    return lookup.surface;
}

void pwa_apply_window_icon(GtkWindow *window, const gchar *app_id) {
    if (!window || !app_id || !*app_id)
        return;

    gchar *path = pwa_icon_path_for_app_id(app_id);
    if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
        GError *error = NULL;
        gtk_window_set_icon_from_file(window, path, &error);
        if (error)
            g_clear_error(&error);
    }
    g_free(path);
}

static void pwa_show_message(GtkWindow *parent, GtkMessageType type,
                             const gchar *primary, const gchar *secondary) {
    GtkWidget *dialog = gtk_message_dialog_new(
        parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", primary);
    if (secondary)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "%s", secondary);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

gboolean pwa_uri_is_installable(const gchar *uri) {
    return uri && (g_str_has_prefix(uri, "https://") ||
                   g_str_has_prefix(uri, "http://"));
}

gchar *pwa_default_name_for_uri(const gchar *uri, const gchar *title) {
    if (title && *title) {
        gchar *copy = g_strstrip(g_strdup(title));
        if (*copy)
            return copy;
        g_free(copy);
    }

    gchar *host = pwa_host_from_uri(uri);
    if (g_str_has_prefix(host, "www."))
        memmove(host, host + 4, strlen(host + 4) + 1);
    return host;
}

gboolean pwa_install_site(App *app, GtkWindow *parent, const gchar *uri,
                          const gchar *title, cairo_surface_t *favicon) {
    if (!pwa_uri_is_installable(uri)) {
        pwa_show_message(parent, GTK_MESSAGE_INFO, "This page cannot be installed",
                         "Open an http:// or https:// site first, then try again.");
        return FALSE;
    }

    gchar *default_name = pwa_default_name_for_uri(uri, title);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Install web app", parent, GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
        "Cancel", GTK_RESPONSE_CANCEL, "Install", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    GtkWidget *label = gtk_label_new(
        "Install this site as a standalone app in your desktop launcher.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), default_name);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("App name"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_widget_show_all(box);

    gboolean result = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
        if (!*name) {
            pwa_show_message(parent, GTK_MESSAGE_ERROR, "App name is empty", NULL);
        } else {
            gchar *app_id = pwa_app_id_for_uri(uri, name);
            gchar *apps_dir = g_build_filename(g_get_user_data_dir(),
                                               "applications", NULL);
            g_mkdir_with_parents(apps_dir, 0700);
            gchar *desktop_path = g_strdup_printf("%s%c%s.desktop", apps_dir,
                                                  G_DIR_SEPARATOR, app_id);
            gchar *exe = pwa_executable_path();
            gchar *icon_path = pwa_save_icon(app_id, favicon);
            if (!icon_path) {
                cairo_surface_t *db_favicon = pwa_lookup_favicon(app, uri);
                icon_path = pwa_save_icon(app_id, db_favicon);
                if (db_favicon)
                    cairo_surface_destroy(db_favicon);
            }
            const gchar *icon_name = icon_path ? app_id : "web-browser";

            if (!exe) {
                pwa_show_message(parent, GTK_MESSAGE_ERROR,
                                 "Could not find Astra executable",
                                 "Build or install Astra first, then try installing the web app again.");
            } else {
                gchar *q_exe = g_shell_quote(exe);
                gchar *q_uri = g_shell_quote(uri);
                gchar *q_name = g_shell_quote(name);
                gchar *q_id = g_shell_quote(app_id);
                gchar *exec_line = g_strdup_printf(
                    "%s --app %s --name %s --app-id %s",
                    q_exe, q_uri, q_name, q_id);

                GKeyFile *key = g_key_file_new();
                g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
                g_key_file_set_string(key, "Desktop Entry", "Name", name);
                g_key_file_set_string(key, "Desktop Entry", "Comment",
                                      "Astra standalone web app");
                g_key_file_set_string(key, "Desktop Entry", "Exec", exec_line);
                g_key_file_set_string(key, "Desktop Entry", "Icon", icon_name);
                g_key_file_set_string(key, "Desktop Entry", "Categories",
                                      "Network;WebBrowser;");
                g_key_file_set_boolean(key, "Desktop Entry", "Terminal", FALSE);
                g_key_file_set_boolean(key, "Desktop Entry", "StartupNotify", TRUE);
                g_key_file_set_string(key, "Desktop Entry", "StartupWMClass", app_id);
                g_key_file_set_string(key, "Desktop Entry", "X-GNOME-WMClass", app_id);
                g_key_file_set_string(key, "Desktop Entry", "X-Astra-WebApp-URL", uri);

                GError *error = NULL;
                if (g_key_file_save_to_file(key, desktop_path, &error)) {
                    g_chmod(desktop_path, 0755);
                    pwa_show_message(parent, GTK_MESSAGE_INFO, "Web app installed",
                                     "You can now launch it from your app menu.");
                    result = TRUE;
                } else {
                    pwa_show_message(parent, GTK_MESSAGE_ERROR,
                                     "Could not install web app",
                                     error ? error->message : "Unknown error");
                    g_clear_error(&error);
                }

                gchar *update = g_find_program_in_path("update-desktop-database");
                if (update) {
                    gchar *argv[] = {update, apps_dir, NULL};
                    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                  NULL, NULL, NULL, NULL);
                    g_free(update);
                }

                g_key_file_unref(key);
                g_free(exec_line);
                g_free(q_id);
                g_free(q_name);
                g_free(q_uri);
                g_free(q_exe);
            }

            g_free(icon_path);
            g_free(exe);
            g_free(desktop_path);
            g_free(apps_dir);
            g_free(app_id);
        }
        g_free(name);
    }

    gtk_widget_destroy(dialog);
    g_free(default_name);
    return result;
}
