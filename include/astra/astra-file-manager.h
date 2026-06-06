#pragma once

#include <gtk/gtk.h>

/* Returns TRUE when uri points to a local directory that Astra should render
 * through its own internal file-manager page instead of WebKit's stock
 * directory index.
 */
gboolean astra_file_manager_uri_is_directory(const char *uri);

/* Build the internal, Firefox-like file manager page for a local file:// dir. */
gchar *astra_file_manager_build_page_html(const char *directory_uri);

/* Open a local file URI using the desktop default app. This goes through GIO,
 * shared-mime-info, and the xdg desktop app database where available.
 */
gboolean astra_file_manager_open_uri_external(const char *uri, GError **error);
