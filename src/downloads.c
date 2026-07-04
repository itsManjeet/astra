#include "downloads.h"

#include "browser.h"

#include <string.h>

struct _DownloadItem {
    App *app;
    WebKitDownload *download;
    gchar *uri;
    gchar *destination;
    gchar *filename;
    gchar *status;
    gchar *error;
    guint64 received;
    gdouble progress;
    gboolean finished;
    gboolean failed;
    gboolean canceled;
};

typedef struct {
    App *app;
    GtkWidget *window;
    GtkWidget *list;
} DownloadView;

static void downloads_refresh_views(App *app);
static void downloads_changed(App *app);

static void download_item_free(gpointer data) {
    DownloadItem *item = data;
    if (!item)
        return;
    g_clear_object(&item->download);
    g_free(item->uri);
    g_free(item->destination);
    g_free(item->filename);
    g_free(item->status);
    g_free(item->error);
    g_free(item);
}

void downloads_init(App *app) {
    app->downloads = g_ptr_array_new_with_free_func(download_item_free);
    app->download_views = NULL;
}

void downloads_clear(App *app) {
    while (app->download_views) {
        DownloadView *view = app->download_views->data;
        app->download_views = g_list_delete_link(app->download_views,
                                                 app->download_views);
        g_free(view);
    }
    if (app->downloads) {
        g_ptr_array_unref(app->downloads);
        app->downloads = NULL;
    }
}

static gchar *downloads_dir(void) {
    const gchar *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dir || !*dir)
        return g_build_filename(g_get_home_dir(), "Downloads", NULL);
    return g_strdup(dir);
}

static gchar *filename_from_uri(const gchar *uri) {
    if (!uri || !*uri)
        return g_strdup("download");

    gchar *scheme = g_uri_parse_scheme(uri);
    gchar *path = scheme ? g_uri_unescape_string(uri, NULL) : g_strdup(uri);
    g_free(scheme);
    gchar *base = g_path_get_basename(path);
    g_free(path);

    if (!base || !*base || g_str_equal(base, "/") || g_str_equal(base, ".")) {
        g_free(base);
        return g_strdup("download");
    }
    return base;
}

static gchar *safe_suggested_filename(const gchar *suggested,
                                      const gchar *fallback_uri) {
    gchar *name = NULL;
    if (suggested && *suggested)
        name = g_path_get_basename(suggested);
    if (!name || !*name || g_str_equal(name, ".") || g_str_equal(name, "/")) {
        g_free(name);
        name = filename_from_uri(fallback_uri);
    }
    if (!name || !*name) {
        g_free(name);
        name = g_strdup("download");
    }
    return name;
}

static gchar *unique_download_path(const gchar *dir, const gchar *filename) {
    gchar *candidate = g_build_filename(dir, filename, NULL);
    if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
        return candidate;

    gchar *dot = strrchr(filename, '.');
    gchar *stem = NULL;
    gchar *ext = NULL;
    if (dot && dot != filename) {
        stem = g_strndup(filename, dot - filename);
        ext = g_strdup(dot);
    } else {
        stem = g_strdup(filename);
        ext = g_strdup("");
    }

    for (guint i = 1; i < 10000; i++) {
        g_free(candidate);
        gchar *numbered = g_strdup_printf("%s (%u)%s", stem, i, ext);
        candidate = g_build_filename(dir, numbered, NULL);
        g_free(numbered);
        if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
            break;
    }

    g_free(stem);
    g_free(ext);
    return candidate;
}

static void set_status(DownloadItem *item, const gchar *status) {
    g_free(item->status);
    item->status = g_strdup(status);
}

static DownloadItem *download_item_for(WebKitDownload *download) {
    return g_object_get_data(G_OBJECT(download), "astra-download-item");
}

static const gchar *download_display_name(DownloadItem *item) {
    if (!item)
        return "Download";
    if (item->filename && *item->filename)
        return item->filename;
    if (item->uri && *item->uri)
        return item->uri;
    return "Download";
}

static void notify_download_windows(App *app, const gchar *title,
                                    DownloadItem *item, gdouble progress,
                                    gboolean failed) {
    if (!app)
        return;
    const gchar *detail = download_display_name(item);
    for (GList *l = app->windows; l; l = l->next)
        browser_show_download_notification(l->data, title, detail, progress, failed);
}

static gboolean on_decide_destination(WebKitDownload *download,
                                      gchar *suggested_filename,
                                      gpointer data) {
    DownloadItem *item = data;
    WebKitURIRequest *request = webkit_download_get_request(download);
    const gchar *uri = request ? webkit_uri_request_get_uri(request) : item->uri;

    gchar *dir = downloads_dir();
    g_mkdir_with_parents(dir, 0700);
    gchar *name = safe_suggested_filename(suggested_filename, uri);
    gchar *path = unique_download_path(dir, name);
    gchar *destination = g_filename_to_uri(path, NULL, NULL);

    g_free(item->filename);
    item->filename = g_strdup(name);
    g_free(item->destination);
    item->destination = g_strdup(destination);
    set_status(item, "Downloading");

    gboolean handled = destination != NULL;
    if (handled)
        webkit_download_set_destination(download, destination);

    g_free(destination);
    g_free(path);
    g_free(name);
    g_free(dir);
    downloads_changed(item->app);
    if (handled)
        notify_download_windows(item->app, "Download started", item, 0.01, FALSE);
    return handled;
}

static void on_created_destination(WebKitDownload *download,
                                   gchar *destination,
                                   gpointer data) {
    (void)download;
    DownloadItem *item = data;
    g_free(item->destination);
    item->destination = g_strdup(destination);
    if (!item->filename && destination) {
        GFile *file = g_file_new_for_uri(destination);
        item->filename = g_file_get_basename(file);
        g_object_unref(file);
    }
    downloads_changed(item->app);
}

static void on_received_data(WebKitDownload *download,
                             guint64 data_length,
                             gpointer data) {
    (void)download;
    DownloadItem *item = data;
    item->received += data_length;
    downloads_changed(item->app);
}

static void on_progress_changed(WebKitDownload *download,
                                GParamSpec *pspec,
                                gpointer data) {
    (void)pspec;
    DownloadItem *item = data;
    item->progress = webkit_download_get_estimated_progress(download);
    if (!item->finished && !item->failed && !item->canceled)
        set_status(item, "Downloading");
    downloads_changed(item->app);
}

static void on_finished(WebKitDownload *download, gpointer data) {
    (void)download;
    DownloadItem *item = data;
    item->progress = 1.0;
    item->finished = TRUE;
    set_status(item, "Completed");
    downloads_changed(item->app);
    notify_download_windows(item->app, "Download complete", item, 1.0, FALSE);
}

static void on_failed(WebKitDownload *download, GError *error, gpointer data) {
    (void)download;
    DownloadItem *item = data;
    item->failed = TRUE;
    if (error && error->domain == WEBKIT_DOWNLOAD_ERROR &&
        error->code == WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER) {
        item->canceled = TRUE;
        set_status(item, "Canceled");
    } else {
        set_status(item, "Failed");
    }
    g_free(item->error);
    item->error = error ? g_strdup(error->message) : NULL;
    downloads_changed(item->app);
    notify_download_windows(item->app, item->canceled ? "Download canceled" : "Download failed",
                            item, item->progress, TRUE);
}

static void on_download_started(WebKitWebContext *context,
                                WebKitDownload *download,
                                gpointer data) {
    (void)context;
    App *app = data;
    DownloadItem *item = g_new0(DownloadItem, 1);
    item->app = app;
    item->download = g_object_ref(download);
    item->progress = 0.0;
    set_status(item, "Starting");

    WebKitURIRequest *request = webkit_download_get_request(download);
    if (request)
        item->uri = g_strdup(webkit_uri_request_get_uri(request));

    g_object_set_data(G_OBJECT(download), "astra-download-item", item);
    g_ptr_array_add(app->downloads, item);

    g_signal_connect(download, "decide-destination",
                     G_CALLBACK(on_decide_destination), item);
    g_signal_connect(download, "created-destination",
                     G_CALLBACK(on_created_destination), item);
    g_signal_connect(download, "received-data",
                     G_CALLBACK(on_received_data), item);
    g_signal_connect(download, "notify::estimated-progress",
                     G_CALLBACK(on_progress_changed), item);
    g_signal_connect(download, "finished", G_CALLBACK(on_finished), item);
    g_signal_connect(download, "failed", G_CALLBACK(on_failed), item);

    downloads_changed(app);
}

void downloads_connect_context(App *app, WebKitWebContext *context) {
    if (!app || !context)
        return;
    if (g_object_get_data(G_OBJECT(context), "astra-downloads-connected"))
        return;
    g_signal_connect(context, "download-started",
                     G_CALLBACK(on_download_started), app);
    g_object_set_data(G_OBJECT(context), "astra-downloads-connected",
                      GINT_TO_POINTER(1));
}

static gchar *format_received(guint64 bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f GB", bytes / 1073741824.0);
    if (bytes >= 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f MB", bytes / 1048576.0);
    if (bytes >= 1024ULL)
        return g_strdup_printf("%.1f KB", bytes / 1024.0);
    return g_strdup_printf("%" G_GUINT64_FORMAT " B", bytes);
}

static void open_uri(const gchar *uri, GtkWindow *parent) {
    if (!uri || !*uri)
        return;
    GError *error = NULL;
    gtk_show_uri_on_window(parent, uri, GDK_CURRENT_TIME, &error);
    if (error)
        g_error_free(error);
}

static void open_download_action(GtkButton *button, gpointer data) {
    DownloadItem *item = data;
    open_uri(item->destination,
             GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))));
}

static void open_folder_action(GtkButton *button, gpointer data) {
    DownloadItem *item = data;
    if (!item->destination)
        return;
    GFile *file = g_file_new_for_uri(item->destination);
    GFile *parent = g_file_get_parent(file);
    if (parent) {
        gchar *uri = g_file_get_uri(parent);
        open_uri(uri, GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))));
        g_free(uri);
        g_object_unref(parent);
    }
    g_object_unref(file);
}

static void cancel_download_action(GtkButton *button, gpointer data) {
    (void)button;
    DownloadItem *item = data;
    if (item->download && !item->finished && !item->failed && !item->canceled) {
        item->canceled = TRUE;
        set_status(item, "Canceling");
        webkit_download_cancel(item->download);
        downloads_changed(item->app);
    }
}

static GtkWidget *row_button(const gchar *icon, const gchar *tooltip,
                             GCallback callback, gpointer data) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");
    g_signal_connect(button, "clicked", callback, data);
    return button;
}

static GtkWidget *download_row(DownloadItem *item) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    const gchar *icon = item->finished ? "emblem-ok-symbolic" :
        item->failed || item->canceled ? "dialog-warning-symbolic" :
        "folder-download-symbolic";
    gtk_box_pack_start(GTK_BOX(box),
        gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG), FALSE, FALSE, 0);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    const gchar *name = item->filename && *item->filename ? item->filename :
        item->uri && *item->uri ? item->uri : "Download";
    GtkWidget *title = gtk_label_new(name);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);

    gchar *received = format_received(item->received);
    gchar *status = NULL;
    if (item->error && *item->error)
        status = g_strdup_printf("%s — %s", item->status, item->error);
    else if (item->finished)
        status = g_strdup_printf("%s — %s", item->status, received);
    else if (item->received)
        status = g_strdup_printf("%s — %.0f%% — %s", item->status,
                                 item->progress * 100.0, received);
    else
        status = g_strdup_printf("%s — %.0f%%", item->status,
                                 item->progress * 100.0);
    GtkWidget *subtitle = gtk_label_new(status);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle),
                                "dim-label");

    GtkWidget *progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),
                                  CLAMP(item->progress, 0.0, 1.0));
    gtk_widget_set_hexpand(progress, TRUE);
    if (item->finished || item->failed || item->canceled)
        gtk_widget_set_opacity(progress, 0.35);

    gtk_box_pack_start(GTK_BOX(text), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), progress, FALSE, FALSE, 0);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);

    if (item->finished && item->destination) {
        gtk_box_pack_start(GTK_BOX(box), row_button("document-open-symbolic",
            "Open file", G_CALLBACK(open_download_action), item), FALSE, FALSE, 0);
    }
    if (item->destination) {
        gtk_box_pack_start(GTK_BOX(box), row_button("folder-open-symbolic",
            "Show in folder", G_CALLBACK(open_folder_action), item), FALSE, FALSE, 0);
    }
    if (!item->finished && !item->failed && !item->canceled) {
        gtk_box_pack_start(GTK_BOX(box), row_button("process-stop-symbolic",
            "Cancel download", G_CALLBACK(cancel_download_action), item), FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(row), box);
    g_free(received);
    g_free(status);
    return row;
}

static void list_remove_all(GtkWidget *container) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(children);
}

static void downloads_refresh_view(DownloadView *view) {
    list_remove_all(view->list);

    if (!view->app->downloads || view->app->downloads->len == 0) {
        GtkWidget *empty = gtk_label_new("No downloads yet");
        gtk_widget_set_margin_top(empty, 36);
        gtk_widget_set_margin_bottom(empty, 36);
        gtk_style_context_add_class(gtk_widget_get_style_context(empty),
                                    "dim-label");
        gtk_container_add(GTK_CONTAINER(view->list), empty);
        gtk_widget_show_all(view->list);
        return;
    }

    for (gint i = (gint)view->app->downloads->len - 1; i >= 0; i--) {
        DownloadItem *item = g_ptr_array_index(view->app->downloads, i);
        gtk_container_add(GTK_CONTAINER(view->list), download_row(item));
    }
    gtk_widget_show_all(view->list);
}

static void downloads_refresh_views(App *app) {
    for (GList *l = app->download_views; l; l = l->next)
        downloads_refresh_view(l->data);
}

static void downloads_update_indicators(App *app) {
    if (!app)
        return;
    for (GList *l = app->windows; l; l = l->next)
        browser_update_download_indicator(l->data);
}

static void downloads_changed(App *app) {
    downloads_refresh_views(app);
    downloads_update_indicators(app);
}

void downloads_get_status(App *app, DownloadStatus *status) {
    if (!status)
        return;
    memset(status, 0, sizeof(*status));
    if (!app || !app->downloads || app->downloads->len == 0)
        return;

    status->has_downloads = TRUE;
    gdouble progress_sum = 0.0;

    for (guint i = 0; i < app->downloads->len; i++) {
        DownloadItem *item = g_ptr_array_index(app->downloads, i);
        if (!item)
            continue;
        if (item->failed && !item->canceled)
            status->has_failed = TRUE;
        if (!item->finished && !item->failed && !item->canceled) {
            status->active = TRUE;
            status->active_count++;
            progress_sum += CLAMP(item->progress, 0.0, 1.0);
        }
    }

    if (status->active_count > 0)
        status->progress = progress_sum / status->active_count;
}

static void clear_finished_action(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (!app->downloads)
        return;
    for (gint i = (gint)app->downloads->len - 1; i >= 0; i--) {
        DownloadItem *item = g_ptr_array_index(app->downloads, i);
        if (item->finished || item->failed || item->canceled)
            g_ptr_array_remove_index(app->downloads, i);
    }
    downloads_changed(app);
}

static void download_view_destroyed(GtkWidget *widget, gpointer data) {
    (void)widget;
    DownloadView *view = data;
    view->app->download_views = g_list_remove(view->app->download_views, view);
    g_free(view);
}

void downloads_open(BrowserWindow *browser) {
    DownloadView *view = g_new0(DownloadView, 1);
    view->app = browser->app;

    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(view->window),
                                 GTK_WINDOW(browser->window));
    gtk_window_set_title(GTK_WINDOW(view->window), "Downloads");
    gtk_window_set_default_size(GTK_WINDOW(view->window), 620, 420);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Downloads");
    GtkWidget *clear = gtk_button_new_with_label("Clear finished");
    gtk_widget_set_tooltip_text(clear, "Remove completed, failed, and canceled downloads from this list");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_finished_action), browser->app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), clear);
    gtk_window_set_titlebar(GTK_WINDOW(view->window), header);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    view->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(view->list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), view->list);
    gtk_container_add(GTK_CONTAINER(view->window), scroll);

    browser->app->download_views = g_list_prepend(browser->app->download_views,
                                                  view);
    g_signal_connect(view->window, "destroy",
                     G_CALLBACK(download_view_destroyed), view);
    downloads_refresh_view(view);
    gtk_widget_show_all(view->window);
    gtk_window_present(GTK_WINDOW(view->window));
}
