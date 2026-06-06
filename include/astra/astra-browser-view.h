#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#define ASTRA_TYPE_BROWSER_VIEW (astra_browser_view_get_type())
G_DECLARE_FINAL_TYPE(AstraBrowserView, astra_browser_view, ASTRA, BROWSER_VIEW, GtkBox)

AstraBrowserView *astra_browser_view_new(void);
AstraBrowserView *astra_browser_view_new_private(gboolean private_mode);
WebKitWebView *astra_browser_view_get_web_view(AstraBrowserView *view);
void astra_browser_view_load_uri(AstraBrowserView *view, const char *uri);
void astra_browser_view_load_new_tab(AstraBrowserView *view);
void astra_browser_view_load_file_manager(AstraBrowserView *view, const char *directory_uri);
void astra_browser_view_load_settings(AstraBrowserView *view);
void astra_browser_view_load_bookmarks(AstraBrowserView *view);
void astra_browser_view_load_history(AstraBrowserView *view);
void astra_browser_view_load_downloads(AstraBrowserView *view);
void astra_browser_view_show_bookmarks_dialog(AstraBrowserView *view, GtkWindow *parent);
void astra_browser_view_show_history_dialog(AstraBrowserView *view, GtkWindow *parent);
void astra_browser_view_bookmark_current_page(AstraBrowserView *view);
gboolean astra_browser_view_is_private(AstraBrowserView *view);
void astra_browser_view_reload_settings_if_open(AstraBrowserView *view);

gboolean astra_browser_view_can_go_back(AstraBrowserView *view);
gboolean astra_browser_view_can_go_forward(AstraBrowserView *view);
void astra_browser_view_go_back(AstraBrowserView *view);
void astra_browser_view_go_forward(AstraBrowserView *view);
void astra_browser_view_reload(AstraBrowserView *view);

const char *astra_browser_view_get_uri(AstraBrowserView *view);
