#ifndef ASTRA_APP_H
#define ASTRA_APP_H

#include "astra.h"

void app_load(App *app);
void app_save(App *app);
void app_clear(App *app);
gboolean app_array_contains(GPtrArray *array, const gchar *value);
void app_rebuild_completion(App *app);

#endif
