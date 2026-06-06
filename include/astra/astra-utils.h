#pragma once

#include <gtk/gtk.h>

char *astra_uri_normalize(const char *input);
char *astra_uri_origin(const char *uri);
void astra_load_css(void);
void astra_show_error(GtkWindow *parent, const char *title, const char *message);
