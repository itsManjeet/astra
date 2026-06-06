#include "astra/astra-permissions.h"

static const char *feature_label(WebKitPermissionRequest *request) {
  if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) return "Camera / microphone access";
  if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) return "Location access";
  if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) return "Notification access";
#if WEBKIT_CHECK_VERSION(2, 40, 0)
  if (WEBKIT_IS_WEBSITE_DATA_ACCESS_PERMISSION_REQUEST(request)) return "Website data access";
#endif
  return "System feature access";
}

AstraPermissionDecision astra_permissions_prompt(GtkWindow *parent,
                                                 const char *origin,
                                                 const char *feature) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
    feature,
    parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "Block", GTK_RESPONSE_REJECT,
    "Allow once", GTK_RESPONSE_ACCEPT,
    NULL
  );
  gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "astra-card");

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(box), 20);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE, 0);

  GtkWidget *title = gtk_label_new(feature);
  gtk_style_context_add_class(gtk_widget_get_style_context(title), "astra-permission-title");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

  g_autofree char *message = g_strdup_printf("%s wants permission to use this feature.", origin ? origin : "This site");
  GtkWidget *body = gtk_label_new(message);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
  gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
  gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);

  GtkWidget *hint = gtk_label_new("Astra will isolate the site even when access is allowed.");
  gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
  gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

  gtk_widget_show_all(dialog);
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  return response == GTK_RESPONSE_ACCEPT ? ASTRA_PERMISSION_ALLOW_ONCE : ASTRA_PERMISSION_BLOCK;
}

gboolean astra_permissions_handle_request(GtkWindow *parent,
                                          WebKitPermissionRequest *request,
                                          const char *origin) {
  AstraPermissionDecision decision = astra_permissions_prompt(parent, origin, feature_label(request));
  if (decision == ASTRA_PERMISSION_BLOCK) {
    webkit_permission_request_deny(request);
  } else {
    webkit_permission_request_allow(request);
  }
  return TRUE;
}
