#ifndef ASTRA_FILE_PAGE_H
#define ASTRA_FILE_PAGE_H

#include "astra.h"

typedef enum {
    FILE_PAGE_NOT_HANDLED,
    FILE_PAGE_DIRECTORY,
    FILE_PAGE_EXTERNAL,
    FILE_PAGE_ERROR
} FilePageResult;

FilePageResult file_page_load_uri(WebKitWebView *view, const gchar *uri,
                                  GtkWindow *parent);
gchar *file_page_navigation_target(const gchar *uri);
gboolean file_page_handle_action(const gchar *uri, WebKitWebView *view,
                                 GtkWindow *parent);

#endif
