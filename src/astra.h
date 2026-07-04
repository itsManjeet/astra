#ifndef ASTRA_H
#define ASTRA_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#define APP_ID "dev.avyos.Astra"
#define APP_NAME "Astra"

#ifndef ASTRA_VERSION
#define ASTRA_VERSION "0.1.0"
#endif

typedef struct _App App;
typedef struct _BrowserWindow BrowserWindow;
typedef struct _BrowserTab BrowserTab;
typedef struct _DownloadItem DownloadItem;

typedef struct {
    gchar *homepage;
    gchar *search_url;
    gchar *user_agent;
    gdouble default_zoom;
    gboolean javascript;
    gboolean images;
    gboolean webgl;
    gboolean media_stream;
    gboolean autoplay;
    gboolean developer_tools;
    gboolean smooth_scrolling;
    gboolean zoom_text_only;
    gboolean dark_ui;
} AppConfig;

struct _App {
    GtkApplication *application;
    AppConfig config;
    gchar *config_path;
    gchar *data_dir;
    gchar *cache_dir;
    gchar *cookie_path;
    WebKitWebContext *context;
    GPtrArray *bookmarks;
    GPtrArray *history;
    GtkListStore *completion_store;
    GPtrArray *downloads;
    GList *download_views;
    GList *windows;
};

struct _BrowserWindow {
    App *app;
    GtkWidget *window;
    GtkWidget *header;
    GtkCssProvider *header_css;
    gchar *header_css_class;
    guint header_color_serial;
    GtkWidget *notebook;
    GtkWidget *back;
    GtkWidget *forward;
    GtkWidget *reload;
    GtkWidget *home;
    GtkWidget *new_tab;
    GtkWidget *favicon;
    GtkWidget *address;
    GtkWidget *progress;
    GtkWidget *download_button;
    GtkWidget *download_icon;
    GtkWidget *download_progress;
    GtkWidget *download_popover;
    GtkWidget *download_popover_icon;
    GtkWidget *download_popover_title;
    GtkWidget *download_popover_subtitle;
    GtkWidget *download_popover_progress;
    guint download_popover_hide_id;
    gdouble download_progress_fraction;
    gboolean download_progress_active;
    gboolean download_button_hold;
    GtkWidget *bookmark;
    GtkWidget *go;
    GtkWidget *app_title;
    WebKitWebContext *context;
    gboolean is_private;
    gboolean app_mode;
    gchar *app_name;
    gchar *app_id;
};

struct _BrowserTab {
    BrowserWindow *browser;
    GtkWidget *page;
    WebKitWebView *view;
    gchar *file_uri;
    GPtrArray *file_history;
    gint file_history_index;
    gboolean file_history_navigation;
    GtkWidget *tab_icon;
    GtkWidget *tab_title;
};

#endif
