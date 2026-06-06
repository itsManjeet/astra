#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

void astra_webapp_init(WebKitWebContext *context);

gboolean astra_webapp_uri_is_app(const char *uri);
char *astra_webapp_id_from_uri(const char *uri);
char *astra_webapp_resolve_start_uri(const char *app_uri,
                                     char **out_id,
                                     char **out_name,
                                     GError **error);

char *astra_webapp_build_apps_page_html(void);
char *astra_webapp_build_offline_page_html(const char *name,
                                           const char *start_uri,
                                           const char *error_message);

gboolean astra_webapp_install_dialog(GtkWindow *parent,
                                     const char *uri,
                                     const char *title,
                                     char **out_app_uri,
                                     GError **error);
