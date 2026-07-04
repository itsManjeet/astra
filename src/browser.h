#ifndef ASTRA_BROWSER_H
#define ASTRA_BROWSER_H

#include "astra.h"

BrowserWindow *browser_window_new(App *app, gboolean is_private,
                                  gboolean app_mode,
                                  const gchar *app_name,
                                  const gchar *app_id);
BrowserTab *browser_tab_new(BrowserWindow *browser, const gchar *uri,
                            gboolean switch_to);
void browser_apply_settings(App *app);
void browser_apply_zoom(App *app);
void browser_update_tab_visibility(BrowserWindow *browser);
void browser_update_download_indicator(BrowserWindow *browser);
void browser_show_download_notification(BrowserWindow *browser,
                                        const gchar *title,
                                        const gchar *detail,
                                        gdouble progress,
                                        gboolean failed);

#endif
