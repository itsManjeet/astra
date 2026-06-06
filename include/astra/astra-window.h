#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "astra/astra-application.h"

#define ASTRA_TYPE_WINDOW (astra_window_get_type())
G_DECLARE_FINAL_TYPE(AstraWindow, astra_window, ASTRA, WINDOW, GtkApplicationWindow)

AstraWindow *astra_window_new(AstraApplication *app);
AstraWindow *astra_window_new_private(AstraApplication *app);
void astra_window_load_uri(AstraWindow *window, const char *uri);
void astra_window_show_new_tab(AstraWindow *window);
