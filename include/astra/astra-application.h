#pragma once

#include <gtk/gtk.h>

#define ASTRA_TYPE_APPLICATION (astra_application_get_type())
G_DECLARE_FINAL_TYPE(AstraApplication, astra_application, ASTRA, APPLICATION, GtkApplication)

AstraApplication *astra_application_new(void);
