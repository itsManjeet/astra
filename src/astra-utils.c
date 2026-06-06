#include "config.h"
#include "astra/astra-utils.h"

char *astra_uri_normalize(const char *input) {
  if (input == NULL || *input == '\0') {
    return g_strdup("resource:///dev/avyos/Astra/assets/new-tab.html");
  }

  if (g_str_has_prefix(input, "http://") ||
      g_str_has_prefix(input, "https://") ||
      g_str_has_prefix(input, "file://") ||
      g_str_has_prefix(input, "resource://") ||
      g_str_has_prefix(input, "astra://") ||
      g_str_has_prefix(input, "app://") ||
      g_str_has_prefix(input, "about:")) {
    return g_strdup(input);
  }

  if (g_path_is_absolute(input)) {
    return g_filename_to_uri(input, NULL, NULL);
  }

  if (g_str_has_prefix(input, "~")) {
    const char *home = g_get_home_dir();
    if (home != NULL) {
      g_autofree char *expanded = g_build_filename(home, input + 1, NULL);
      return g_filename_to_uri(expanded, NULL, NULL);
    }
  }

  if (g_str_has_prefix(input, "localhost") || strchr(input, '.') != NULL) {
    return g_strdup_printf("https://%s", input);
  }

  g_autofree char *escaped = g_uri_escape_string(input, NULL, TRUE);
  return g_strdup_printf("https://duckduckgo.com/?q=%s", escaped);
}

char *astra_uri_origin(const char *uri) {
  if (uri == NULL) return g_strdup("Unknown");

  g_autoptr(GUri) parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
  if (parsed == NULL || g_uri_get_host(parsed) == NULL) {
    return g_strdup(uri);
  }

  const char *scheme = g_uri_get_scheme(parsed);
  const char *host = g_uri_get_host(parsed);
  int port = g_uri_get_port(parsed);

  if (port > 0) {
    return g_strdup_printf("%s://%s:%d", scheme, host, port);
  }
  return g_strdup_printf("%s://%s", scheme, host);
}

void astra_load_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_resource(provider, "/dev/avyos/Astra/styles/astra.css");
  gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
  g_object_unref(provider);
}

void astra_show_error(GtkWindow *parent, const char *title, const char *message) {
  GtkWidget *dialog = gtk_message_dialog_new(
    parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_ERROR,
    GTK_BUTTONS_CLOSE,
    "%s",
    title
  );
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}
