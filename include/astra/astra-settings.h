#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

typedef enum {
  ASTRA_THEME_SYSTEM,
  ASTRA_THEME_LIGHT,
  ASTRA_THEME_DARK
} AstraThemeMode;

typedef struct {
  AstraThemeMode theme_mode;
  gboolean enable_javascript;
  gboolean auto_load_images;
  gboolean enable_smooth_scrolling;
  gboolean enable_page_cache;
  gboolean enable_local_storage;
  gboolean enable_developer_extras;
  gchar *serif_font_family;
  gchar *sans_serif_font_family;
  gchar *monospace_font_family;
  gint default_font_size;
  gint default_monospace_font_size;
  gint minimum_font_size;
} AstraSettingsPrefs;

typedef void (*AstraSettingsChangedFunc)(gpointer user_data);

const AstraSettingsPrefs *astra_settings_get_prefs(void);
void astra_settings_set_theme(AstraThemeMode mode);
void astra_settings_set_boolean(const char *key, gboolean value);
void astra_settings_set_font_family(const char *key, const char *value);
void astra_settings_set_font_size(const char *key, gint value);
void astra_settings_set_from_message(const char *message);
char *astra_settings_build_page_html(void);

GtkWidget *astra_settings_build_gtk_page(WebKitWebView *web_view,
                                         AstraSettingsChangedFunc changed_cb,
                                         gpointer user_data);
void astra_settings_show_dialog(GtkWindow *parent,
                                WebKitWebView *web_view,
                                AstraSettingsChangedFunc changed_cb,
                                gpointer user_data);

void astra_settings_apply_to_web_view(WebKitWebView *web_view);
void astra_settings_apply_to_context(WebKitWebContext *context);
void astra_settings_apply_to_window(GtkWidget *window);
