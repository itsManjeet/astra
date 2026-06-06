#include "astra/astra-downloads.h"

static GtkWindow *download_parent = NULL;

typedef struct {
  gchar *filename;
  gchar *destination;
  gchar *source;
  gchar *status;
} AstraDownloadRecord;

static GPtrArray *download_records = NULL;

static void download_record_free(gpointer data) {
  AstraDownloadRecord *record = data;
  if (record == NULL) return;
  g_free(record->filename);
  g_free(record->destination);
  g_free(record->source);
  g_free(record->status);
  g_free(record);
}

static void ensure_download_records(void) {
  if (download_records == NULL) download_records = g_ptr_array_new_with_free_func(download_record_free);
}

static AstraDownloadRecord *download_record_new(WebKitDownload *download, const char *suggested_filename) {
  AstraDownloadRecord *record = g_new0(AstraDownloadRecord, 1);
  record->filename = g_strdup(suggested_filename && *suggested_filename ? suggested_filename : "Download");
  record->source = g_strdup(webkit_uri_request_get_uri(webkit_download_get_request(download)));
  record->status = g_strdup("Downloading");
  return record;
}

static void decide_destination(WebKitDownload *download, const char *suggested_filename, gpointer user_data) {
  AstraDownloadRecord *record = user_data;
  const char *downloads_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
  if (downloads_dir == NULL) downloads_dir = g_get_home_dir();

  g_autofree char *downloads = g_build_filename(downloads_dir, suggested_filename ? suggested_filename : "download", NULL);
  g_autofree char *uri = g_filename_to_uri(downloads, NULL, NULL);
  webkit_download_set_destination(download, uri);

  if (record != NULL) {
    g_free(record->filename);
    g_free(record->destination);
    record->filename = g_strdup(suggested_filename && *suggested_filename ? suggested_filename : "Download");
    record->destination = g_strdup(uri);
  }
}

static void download_finished(WebKitDownload *download, gpointer user_data) {
  AstraDownloadRecord *record = user_data;
  const char *destination = webkit_download_get_destination(download);
  if (record != NULL) {
    g_free(record->destination);
    g_free(record->status);
    record->destination = g_strdup(destination);
    record->status = g_strdup("Complete");
  }

  GtkWidget *dialog = gtk_message_dialog_new(
    download_parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_INFO,
    GTK_BUTTONS_OK,
    "Download complete"
  );
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", destination ? destination : "Saved to Downloads");
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

static void download_failed(WebKitDownload *download, GError *error, gpointer user_data) {
  (void)download;
  AstraDownloadRecord *record = user_data;
  if (record != NULL) {
    g_free(record->status);
    record->status = g_strdup(error != NULL ? error->message : "Failed");
  }
}

static void download_started(WebKitWebContext *context, WebKitDownload *download, gpointer user_data) {
  (void)context;
  (void)user_data;
  ensure_download_records();
  AstraDownloadRecord *record = download_record_new(download, NULL);
  g_ptr_array_insert(download_records, 0, record);
  while (download_records->len > 50) g_ptr_array_remove_index(download_records, download_records->len - 1);

  g_signal_connect(download, "decide-destination", G_CALLBACK(decide_destination), record);
  g_signal_connect(download, "finished", G_CALLBACK(download_finished), record);
  g_signal_connect(download, "failed", G_CALLBACK(download_failed), record);
}

void astra_downloads_attach(WebKitWebContext *context, GtkWindow *parent) {
  download_parent = parent;
  g_signal_connect(context, "download-started", G_CALLBACK(download_started), NULL);
}

char *astra_downloads_build_page_html(void) {
  GString *rows = g_string_new("");
  if (download_records != NULL && download_records->len > 0) {
    for (guint i = 0; i < download_records->len; i++) {
      AstraDownloadRecord *record = g_ptr_array_index(download_records, i);
      g_autofree char *name = g_markup_escape_text(record->filename ? record->filename : "Download", -1);
      g_autofree char *dest = g_markup_escape_text(record->destination ? record->destination : "Waiting for destination", -1);
      g_autofree char *status = g_markup_escape_text(record->status ? record->status : "Unknown", -1);
      g_string_append_printf(rows,
        "<a class='row' href='%s'><span class='icon'>↓</span><span><strong>%s</strong><small>%s</small></span><em>%s</em></a>",
        dest, name, dest, status);
    }
  } else {
    g_string_append(rows, "<div class='empty'>No downloads yet.</div>");
  }

  char *html = g_strdup_printf(
    "<!doctype html><html><head><meta charset='utf-8'><title>Downloads</title><style>"
    ":root{color-scheme:light dark;--bg:#f8f3ec;--card:#fffdf8;--card2:#efe6da;--text:#201a14;--muted:#75695f;--line:rgba(50,35,20,.14);--accent:#d96f2c}"
    "@media(prefers-color-scheme:dark){:root{--bg:#11100e;--card:#1a1815;--card2:#24201b;--text:#f4efe7;--muted:#b8aea2;--line:rgba(255,255,255,.11)}}"
    "body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}main{max-width:920px;margin:0 auto;padding:42px}h1{font-size:36px;letter-spacing:-.04em}.row{display:flex;align-items:center;gap:14px;background:var(--card);border:1px solid var(--line);border-radius:18px;margin:10px 0;padding:14px;text-decoration:none;color:var(--text)}.icon{width:34px;height:34px;border-radius:50%%;display:grid;place-items:center;background:var(--card2);color:var(--muted)}strong{display:block}small{display:block;color:var(--muted);margin-top:3px;word-break:break-all}em{margin-left:auto;color:var(--muted);font-style:normal}.empty{padding:20px;border:1px dashed var(--line);border-radius:18px;color:var(--muted)}</style></head><body><main><h1>Downloads</h1>%s</main></body></html>", rows->str);
  g_string_free(rows, TRUE);
  return html;
}

static GtkWidget *download_dialog_row(AstraDownloadRecord *record) {
  GtkWidget *row = gtk_list_box_row_new();
  const char *uri = record != NULL && record->destination != NULL ? record->destination : "";
  g_object_set_data_full(G_OBJECT(row), "astra-uri", g_strdup(uri), g_free);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(box), 10);
  gtk_container_add(GTK_CONTAINER(row), box);

  GtkWidget *icon = gtk_label_new("↓");
  gtk_widget_set_size_request(icon, 32, 32);
  gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

  GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start(GTK_BOX(box), texts, TRUE, TRUE, 0);

  GtkWidget *name = gtk_label_new(record != NULL && record->filename != NULL ? record->filename : "Download");
  gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(texts), name, FALSE, FALSE, 0);

  GtkWidget *dest = gtk_label_new(uri);
  gtk_label_set_xalign(GTK_LABEL(dest), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(dest), PANGO_ELLIPSIZE_MIDDLE);
  gtk_style_context_add_class(gtk_widget_get_style_context(dest), "dim-label");
  gtk_box_pack_start(GTK_BOX(texts), dest, FALSE, FALSE, 0);

  GtkWidget *status = gtk_label_new(record != NULL && record->status != NULL ? record->status : "Unknown");
  gtk_style_context_add_class(gtk_widget_get_style_context(status), "dim-label");
  gtk_box_pack_end(GTK_BOX(box), status, FALSE, FALSE, 0);
  return row;
}

static void download_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  GtkWindow *parent = GTK_WINDOW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(row), "astra-uri");
  if (uri == NULL || *uri == '\0') return;

  g_autoptr(GError) error = NULL;
  if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "Could not open download");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", error != NULL ? error->message : uri);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  }
}

void astra_downloads_show_dialog(GtkWindow *parent) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Downloads",
                                                  parent,
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 520);

  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(outer), 16);
  gtk_container_add(GTK_CONTAINER(area), outer);

  GtkWidget *heading = gtk_label_new("Downloads");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(heading), "astra-settings-title");
  gtk_box_pack_start(GTK_BOX(outer), heading, FALSE, FALSE, 0);

  GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_box_pack_start(GTK_BOX(outer), scroller, TRUE, TRUE, 0);

  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_container_add(GTK_CONTAINER(scroller), list);
  g_signal_connect(list, "row-activated", G_CALLBACK(download_row_activated), parent);

  if (download_records != NULL && download_records->len > 0) {
    for (guint i = 0; i < download_records->len; i++) {
      gtk_container_add(GTK_CONTAINER(list), download_dialog_row(g_ptr_array_index(download_records, i)));
    }
  } else {
    GtkWidget *empty = gtk_label_new("No downloads yet.");
    gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(empty), "dim-label");
    gtk_box_pack_start(GTK_BOX(outer), empty, FALSE, FALSE, 0);
  }

  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}
