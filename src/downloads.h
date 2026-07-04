#ifndef ASTRA_DOWNLOADS_H
#define ASTRA_DOWNLOADS_H

#include "astra.h"

typedef struct {
    gboolean has_downloads;
    gboolean active;
    gboolean has_failed;
    guint active_count;
    gdouble progress;
} DownloadStatus;

void downloads_init(App *app);
void downloads_clear(App *app);
void downloads_connect_context(App *app, WebKitWebContext *context);
void downloads_open(BrowserWindow *browser);
void downloads_get_status(App *app, DownloadStatus *status);

#endif
