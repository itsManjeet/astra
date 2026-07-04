#ifndef ASTRA_PWA_H
#define ASTRA_PWA_H

#include "astra.h"

#define ASTRA_PWA_DESKTOP_PREFIX "dev.avyos.Astra.WebApp."

gboolean pwa_uri_is_installable(const gchar *uri);
gchar *pwa_default_name_for_uri(const gchar *uri, const gchar *title);
gchar *pwa_app_id_for_uri(const gchar *uri, const gchar *name);
gchar *pwa_runtime_app_id(const gchar *uri, const gchar *name, const gchar *app_id);
gchar *pwa_icon_path_for_app_id(const gchar *app_id);
void pwa_apply_window_icon(GtkWindow *window, const gchar *app_id);
gboolean pwa_install_site(App *app, GtkWindow *parent, const gchar *uri,
                          const gchar *title, cairo_surface_t *favicon);

#endif
