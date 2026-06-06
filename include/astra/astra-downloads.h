#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

void astra_downloads_attach(WebKitWebContext *context, GtkWindow *parent);

char *astra_downloads_build_page_html(void);
void astra_downloads_show_dialog(GtkWindow *parent);
