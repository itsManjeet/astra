#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

typedef enum {
  ASTRA_PERMISSION_BLOCK,
  ASTRA_PERMISSION_ALLOW_ONCE,
  ASTRA_PERMISSION_ALLOW_SESSION,
  ASTRA_PERMISSION_ALLOW_ALWAYS,
} AstraPermissionDecision;

AstraPermissionDecision astra_permissions_prompt(GtkWindow *parent,
                                                 const char *origin,
                                                 const char *feature);

gboolean astra_permissions_handle_request(GtkWindow *parent,
                                          WebKitPermissionRequest *request,
                                          const char *origin);
