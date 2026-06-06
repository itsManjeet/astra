#include "astra/astra-webapp.h"
#include "astra/astra-utils.h"

#include <time.h>
#include <string.h>
#include <glib/gstdio.h>

#define ASTRA_WEBAPP_ERROR astra_webapp_error_quark()

typedef enum {
  ASTRA_WEBAPP_ERROR_INVALID_URI,
  ASTRA_WEBAPP_ERROR_NOT_FOUND,
  ASTRA_WEBAPP_ERROR_IO
} AstraWebAppError;

static GQuark astra_webapp_error_quark(void) {
  return g_quark_from_static_string("astra-webapp-error");
}

static char *webapps_root_dir(void) {
  return g_build_filename(g_get_user_data_dir(), "astra", "webapps", NULL);
}

static char *webapp_dir_for_id(const char *id) {
  g_autofree char *root = webapps_root_dir();
  return g_build_filename(root, id, NULL);
}

static char *manifest_path_for_id(const char *id) {
  g_autofree char *dir = webapp_dir_for_id(id);
  return g_build_filename(dir, "manifest.ini", NULL);
}

static char *sanitize_id(const char *text) {
  if (text == NULL || *text == '\0') return g_strdup("webapp");

  GString *out = g_string_new(NULL);
  gboolean last_dash = FALSE;

  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    if (g_ascii_isalnum(*p)) {
      g_string_append_c(out, g_ascii_tolower(*p));
      last_dash = FALSE;
    } else if (!last_dash && out->len > 0) {
      g_string_append_c(out, '-');
      last_dash = TRUE;
    }
  }

  while (out->len > 0 && out->str[out->len - 1] == '-') {
    g_string_truncate(out, out->len - 1);
  }

  if (out->len == 0) g_string_append(out, "webapp");
  return g_string_free(out, FALSE);
}

static char *suggest_id_for_uri(const char *uri) {
  g_autoptr(GUri) parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
  const char *host = parsed != NULL ? g_uri_get_host(parsed) : NULL;
  if (host != NULL && *host != '\0') return sanitize_id(host);
  return sanitize_id(uri);
}

static char *suggest_name_for_uri(const char *uri, const char *title) {
  if (title != NULL && *title != '\0' && !g_str_has_prefix(title, "http")) {
    return g_strdup(title);
  }

  g_autoptr(GUri) parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
  const char *host = parsed != NULL ? g_uri_get_host(parsed) : NULL;
  if (host != NULL && *host != '\0') return g_strdup(host);
  return g_strdup("Web App");
}

static char *origin_for_uri(const char *uri) {
  g_autoptr(GUri) parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
  if (parsed == NULL || g_uri_get_host(parsed) == NULL) return g_strdup(uri);

  const char *scheme = g_uri_get_scheme(parsed);
  const char *host = g_uri_get_host(parsed);
  int port = g_uri_get_port(parsed);
  if (port > 0) return g_strdup_printf("%s://%s:%d", scheme, host, port);
  return g_strdup_printf("%s://%s", scheme, host);
}

static gboolean write_desktop_file(const char *id, const char *name, GError **error) {
  g_autofree char *apps_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
  if (g_mkdir_with_parents(apps_dir, 0755) != 0) return FALSE;

  g_autofree char *desktop_name = g_strdup_printf("dev.avyos.Astra.%s.desktop", id);
  g_autofree char *path = g_build_filename(apps_dir, desktop_name, NULL);
  g_autofree char *safe_name = g_strdup(name != NULL && *name ? name : id);
  g_autofree char *contents = g_strdup_printf(
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=%s\n"
    "Comment=Installed Astra web app\n"
    "Exec=astra app://%s\n"
    "Icon=dev.avyos.Astra\n"
    "Terminal=false\n"
    "Categories=Network;WebBrowser;\n"
    "StartupNotify=true\n"
    "X-Astra-WebApp=%s\n",
    safe_name,
    id,
    id
  );

  return g_file_set_contents(path, contents, -1, error);
}

void astra_webapp_init(WebKitWebContext *context) {
  if (context == NULL) return;

  /* Gives installed apps and normal browsing a browser-like cache policy. Real
   * offline behavior still depends on the site providing Service Worker/Cache
   * API support; Astra preserves that site data instead of clearing it. */
  webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_WEB_BROWSER);
}

gboolean astra_webapp_uri_is_app(const char *uri) {
  return uri != NULL && g_str_has_prefix(uri, "app://");
}

char *astra_webapp_id_from_uri(const char *uri) {
  if (!astra_webapp_uri_is_app(uri)) return NULL;

  g_autoptr(GUri) parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
  if (parsed != NULL && g_uri_get_host(parsed) != NULL && *g_uri_get_host(parsed) != '\0') {
    return sanitize_id(g_uri_get_host(parsed));
  }

  const char *raw = uri + strlen("app://");
  const char *slash = strchr(raw, '/');
  if (slash != NULL) {
    g_autofree char *piece = g_strndup(raw, slash - raw);
    return sanitize_id(piece);
  }
  return sanitize_id(raw);
}

static gboolean load_manifest(const char *id, GKeyFile **out_key_file, GError **error) {
  g_autofree char *manifest = manifest_path_for_id(id);
  GKeyFile *key_file = g_key_file_new();
  if (!g_key_file_load_from_file(key_file, manifest, G_KEY_FILE_NONE, error)) {
    g_key_file_unref(key_file);
    return FALSE;
  }
  *out_key_file = key_file;
  return TRUE;
}

char *astra_webapp_resolve_start_uri(const char *app_uri,
                                     char **out_id,
                                     char **out_name,
                                     GError **error) {
  g_autofree char *id = astra_webapp_id_from_uri(app_uri);
  if (id == NULL || *id == '\0') {
    g_set_error(error, ASTRA_WEBAPP_ERROR, ASTRA_WEBAPP_ERROR_INVALID_URI,
                "Invalid app URI: %s", app_uri != NULL ? app_uri : "(null)");
    return NULL;
  }

  g_autoptr(GKeyFile) key_file = NULL;
  if (!load_manifest(id, &key_file, error)) return NULL;

  g_autofree char *start_uri = g_key_file_get_string(key_file, "WebApp", "StartURI", error);
  if (start_uri == NULL || *start_uri == '\0') return NULL;

  if (out_id != NULL) *out_id = g_strdup(id);
  if (out_name != NULL) {
    g_autofree char *name = g_key_file_get_string(key_file, "WebApp", "Name", NULL);
    *out_name = g_strdup(name != NULL && *name ? name : id);
  }
  return g_strdup(start_uri);
}

gboolean astra_webapp_install_dialog(GtkWindow *parent,
                                     const char *uri,
                                     const char *title,
                                     char **out_app_uri,
                                     GError **error) {
  if (uri == NULL || !(g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))) {
    g_set_error(error, ASTRA_WEBAPP_ERROR, ASTRA_WEBAPP_ERROR_INVALID_URI,
                "Only http:// and https:// websites can be installed as web apps.");
    return FALSE;
  }

  g_autofree char *suggested_name = suggest_name_for_uri(uri, title);
  g_autofree char *suggested_id = suggest_id_for_uri(uri);

  GtkWidget *dialog = gtk_dialog_new_with_buttons("Install Website as Web App",
                                                  parent,
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Cancel", GTK_RESPONSE_CANCEL,
                                                  "Install", GTK_RESPONSE_ACCEPT,
                                                  NULL);
  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(box), 18);
  gtk_container_add(GTK_CONTAINER(area), box);

  GtkWidget *heading = gtk_label_new("Install this website as an Astra app");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(heading), "astra-permission-title");
  gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);

  g_autofree char *summary = g_strdup_printf("Astra will create a local app manifest and launcher for:\n%s", uri);
  GtkWidget *body = gtk_label_new(summary);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
  gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
  gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);

  GtkWidget *name_label = gtk_label_new("App name");
  gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
  GtkWidget *name_entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(name_entry), suggested_name);
  gtk_box_pack_start(GTK_BOX(box), name_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), name_entry, FALSE, FALSE, 0);

  GtkWidget *offline = gtk_label_new("Offline support: Astra preserves WebKit site data, cache, and Service Worker storage. Websites that ship a proper PWA/service worker can work offline; server-only sites still need the network.");
  gtk_label_set_xalign(GTK_LABEL(offline), 0.0f);
  gtk_label_set_line_wrap(GTK_LABEL(offline), TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(offline), "astra-muted-label");
  gtk_box_pack_start(GTK_BOX(box), offline, FALSE, FALSE, 0);

  gtk_widget_show_all(dialog);
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));

  if (response != GTK_RESPONSE_ACCEPT) {
    gtk_widget_destroy(dialog);
    return FALSE;
  }

  const char *name_text = gtk_entry_get_text(GTK_ENTRY(name_entry));
  g_autofree char *id = sanitize_id(suggested_id);
  g_autofree char *name = g_strdup(name_text != NULL && *name_text ? name_text : suggested_name);
  gtk_widget_destroy(dialog);

  g_autofree char *dir = webapp_dir_for_id(id);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error, ASTRA_WEBAPP_ERROR, ASTRA_WEBAPP_ERROR_IO,
                "Could not create web app directory: %s", dir);
    return FALSE;
  }

  g_autofree char *origin = origin_for_uri(uri);
  GKeyFile *key_file = g_key_file_new();
  g_key_file_set_string(key_file, "WebApp", "ID", id);
  g_key_file_set_string(key_file, "WebApp", "Name", name);
  g_key_file_set_string(key_file, "WebApp", "StartURI", uri);
  g_key_file_set_string(key_file, "WebApp", "Origin", origin);
  g_key_file_set_string(key_file, "WebApp", "Runtime", "astra");
  g_key_file_set_boolean(key_file, "WebApp", "AppMode", TRUE);
  g_key_file_set_boolean(key_file, "Offline", "UseServiceWorker", TRUE);
  g_key_file_set_boolean(key_file, "Offline", "PreserveWebsiteData", TRUE);

  g_autofree char *data = g_key_file_to_data(key_file, NULL, NULL);
  g_key_file_unref(key_file);

  g_autofree char *manifest = manifest_path_for_id(id);
  if (!g_file_set_contents(manifest, data, -1, error)) return FALSE;
  if (!write_desktop_file(id, name, error)) return FALSE;

  if (out_app_uri != NULL) *out_app_uri = g_strdup_printf("app://%s", id);
  return TRUE;
}

static void append_app_card(GString *html, const char *id) {
  g_autoptr(GKeyFile) key_file = NULL;
  if (!load_manifest(id, &key_file, NULL)) return;

  g_autofree char *name = g_key_file_get_string(key_file, "WebApp", "Name", NULL);
  g_autofree char *start_uri = g_key_file_get_string(key_file, "WebApp", "StartURI", NULL);
  g_autofree char *origin = g_key_file_get_string(key_file, "WebApp", "Origin", NULL);

  g_autofree char *safe_name = g_markup_escape_text(name != NULL ? name : id, -1);
  g_autofree char *safe_uri = g_markup_escape_text(start_uri != NULL ? start_uri : "", -1);
  g_autofree char *safe_origin = g_markup_escape_text(origin != NULL ? origin : "", -1);
  g_autofree char *safe_id = g_markup_escape_text(id, -1);

  g_string_append_printf(html,
    "<a class='app-card' href='astra://launch-app/%s'>"
    "<div class='app-icon'>✦</div>"
    "<div><h3>%s</h3><p>%s</p><code>app://%s</code></div>"
    "</a>",
    safe_id, safe_name, safe_origin[0] ? safe_origin : safe_uri, safe_id);
}

char *astra_webapp_build_apps_page_html(void) {
  GString *html = g_string_new(
    "<!doctype html><html><head><meta charset='utf-8'><title>Installed Web Apps</title>"
    "<style>"
    ":root{color-scheme:light dark}body{margin:0;font:15px system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f8f3ec;color:#201a14}"
    "main{max-width:980px;margin:0 auto;padding:42px 28px}.hero{margin-bottom:28px}.eyebrow{color:#9d5728;font-weight:700;letter-spacing:.08em;text-transform:uppercase;font-size:12px}"
    "h1{font-size:38px;margin:8px 0 10px;letter-spacing:-.04em}p{color:#75695f;line-height:1.55}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:14px}"
    ".app-card{display:flex;gap:14px;text-decoration:none;color:inherit;background:#fffdf8;border:1px solid rgba(90,70,50,.14);border-radius:20px;padding:16px;box-shadow:0 14px 34px rgba(30,20,10,.06)}"
    ".app-card:hover{background:#fff7ef;border-color:rgba(217,111,44,.34)}.app-icon{width:44px;height:44px;border-radius:16px;background:#e06f2c;color:white;display:grid;place-items:center;font-size:20px;flex:0 0 auto}"
    "h3{margin:1px 0 4px;font-size:17px}.app-card p{margin:0 0 8px;font-size:13px}.app-card code{font-size:12px;color:#9d5728}.empty{background:#fffdf8;border:1px dashed rgba(90,70,50,.22);border-radius:20px;padding:24px}"
    "@media(prefers-color-scheme:dark){body{background:#11100e;color:#f4efe7}.app-card,.empty{background:#1a1815;border-color:rgba(255,255,255,.1)}.app-card:hover{background:#24201b}.app-card p,p{color:#b8aea2}}"
    "</style></head><body><main><section class='hero'><div class='eyebrow'>Astra Runtime</div><h1>Installed Web Apps</h1><p>Websites installed as Avyos-style apps launch through <code>app://id</code>. Offline support uses the site's Service Worker/Cache API when available.</p></section><section class='grid'>"
  );

  g_autofree char *root = webapps_root_dir();
  GDir *dir = g_dir_open(root, 0, NULL);
  guint count = 0;
  if (dir != NULL) {
    const char *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
      g_autofree char *manifest = manifest_path_for_id(name);
      if (g_file_test(manifest, G_FILE_TEST_EXISTS)) {
        append_app_card(html, name);
        count++;
      }
    }
    g_dir_close(dir);
  }

  if (count == 0) {
    g_string_append(html, "<div class='empty'><h3>No web apps installed yet</h3><p>Open a website, then choose <b>Install site as Web App</b> from the Astra menu.</p></div>");
  }

  g_string_append(html, "</section></main></body></html>");
  return g_string_free(html, FALSE);
}

char *astra_webapp_build_offline_page_html(const char *name,
                                           const char *start_uri,
                                           const char *error_message) {
  g_autofree char *safe_name = g_markup_escape_text(name != NULL ? name : "Web App", -1);
  g_autofree char *safe_uri = g_markup_escape_text(start_uri != NULL ? start_uri : "", -1);
  g_autofree char *safe_error = g_markup_escape_text(error_message != NULL ? error_message : "The network request failed.", -1);

  return g_strdup_printf(
    "<!doctype html><html><head><meta charset='utf-8'><title>%s Offline</title>"
    "<style>body{margin:0;min-height:100vh;display:grid;place-items:center;font:16px system-ui;background:#f8f3ec;color:#201a14}.card{max-width:620px;background:#fffdf8;border:1px solid rgba(90,70,50,.16);border-radius:26px;padding:30px;box-shadow:0 24px 60px rgba(30,20,10,.08)}h1{margin:0 0 8px;font-size:30px;letter-spacing:-.04em}p{color:#75695f;line-height:1.6}code{color:#9d5728}.pill{display:inline-block;background:#fff0e3;color:#9d5728;border-radius:999px;padding:6px 10px;font-size:12px;font-weight:700;margin-bottom:14px}@media(prefers-color-scheme:dark){body{background:#11100e;color:#f4efe7}.card{background:#1a1815;border-color:rgba(255,255,255,.1)}p{color:#b8aea2}}</style>"
    "</head><body><div class='card'><div class='pill'>Offline</div><h1>%s could not load</h1><p>Astra tried to open <code>%s</code>, but the page is not available right now.</p><p>If this site provides a Service Worker/PWA cache, WebKit can serve the cached app offline. Otherwise the app still needs the network.</p><p><small>%s</small></p></div></body></html>",
    safe_name, safe_name, safe_uri, safe_error);
}
