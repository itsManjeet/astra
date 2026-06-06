#include "astra/astra-browser-view.h"
#include "astra/astra-settings.h"
#include "astra/astra-file-manager.h"
#include "astra/astra-webapp.h"
#include "astra/astra-downloads.h"
#include "astra/astra-utils.h"

#include <string.h>

struct _AstraBrowserView {
  GtkBox parent_instance;
  WebKitWebView *web_view;
  GtkWidget *view_stack;
  GtkWidget *overlay;
  GtkWidget *warm_placeholder;
  GtkWidget *settings_host;
  GtkWidget *settings_page;
  GtkWidget *status_label;

  /* Internal pages are rendered with safe WebKit base URIs, but the browser
   * chrome should still show astra://new-tab, astra://settings, etc.
   */
  gchar *virtual_uri;
  gchar *virtual_title;

  /* WebKit does not treat our generated file-manager HTML exactly like
   * normal file:// navigation, so we keep a tiny per-view directory history.
   * Browser chrome still exposes it through the normal back/forward buttons.
   */
  GPtrArray *file_history;
  gint file_history_index;
  gboolean file_manager_active;

  gchar *active_app_uri;
  gchar *active_app_id;
  gchar *active_app_name;
  gchar *active_app_start_uri;
  gboolean private_mode;
};

G_DEFINE_TYPE(AstraBrowserView, astra_browser_view, GTK_TYPE_BOX)

enum {
  SIGNAL_URI_CHANGED,
  SIGNAL_TITLE_CHANGED,
  SIGNAL_LOAD_CHANGED,
  SIGNAL_SETTINGS_CHANGED,
  SIGNAL_OPEN_URI_NEW_TAB,
  N_SIGNALS
};

static guint signals[N_SIGNALS];
static gboolean next_private_mode = FALSE;

typedef struct {
  gchar *title;
  gchar *uri;
} AstraRecentPage;

static GPtrArray *recent_pages = NULL;


typedef struct {
  gchar *title;
  gchar *uri;
} AstraBookmark;

static GPtrArray *bookmarks = NULL;

static void bookmark_free(gpointer data) {
  AstraBookmark *bookmark = data;
  if (bookmark == NULL) return;
  g_free(bookmark->title);
  g_free(bookmark->uri);
  g_free(bookmark);
}

static void bookmarks_ensure(void) {
  if (bookmarks == NULL) bookmarks = g_ptr_array_new_with_free_func(bookmark_free);
}

static gboolean bookmark_uri_should_store(const char *uri) {
  return uri != NULL &&
         (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://") || g_str_has_prefix(uri, "file://") || g_str_has_prefix(uri, "app://"));
}

static void bookmarks_add(const char *title, const char *uri) {
  if (!bookmark_uri_should_store(uri)) return;
  bookmarks_ensure();
  for (guint i = 0; i < bookmarks->len; i++) {
    AstraBookmark *existing = g_ptr_array_index(bookmarks, i);
    if (g_strcmp0(existing->uri, uri) == 0) {
      g_free(existing->title);
      existing->title = g_strdup(title != NULL && *title ? title : uri);
      return;
    }
  }
  AstraBookmark *bookmark = g_new0(AstraBookmark, 1);
  bookmark->title = g_strdup(title != NULL && *title ? title : uri);
  bookmark->uri = g_strdup(uri);
  g_ptr_array_add(bookmarks, bookmark);
}

static char *build_listing_page_html(const char *title, const char *empty, GPtrArray *items, const char *icon) {
  GString *rows = g_string_new("");
  if (items != NULL && items->len > 0) {
    for (guint i = 0; i < items->len; i++) {
      AstraRecentPage *page = g_ptr_array_index(items, i);
      g_autofree char *t = g_markup_escape_text(page->title ? page->title : page->uri, -1);
      g_autofree char *u = g_markup_escape_text(page->uri ? page->uri : "", -1);
      g_string_append_printf(rows, "<a class='row' href='%s'><span class='icon'>%s</span><span><strong>%s</strong><small>%s</small></span></a>", u, icon, t, u);
    }
  } else {
    g_autofree char *e = g_markup_escape_text(empty, -1);
    g_string_append_printf(rows, "<div class='empty'>%s</div>", e);
  }
  g_autofree char *safe_title = g_markup_escape_text(title, -1);
  char *html = g_strdup_printf(
    "<!doctype html><html><head><meta charset='utf-8'><title>%s</title><style>"
    ":root{color-scheme:light dark;--bg:#f8f3ec;--card:#fffdf8;--card2:#efe6da;--text:#201a14;--muted:#75695f;--line:rgba(50,35,20,.14);--accent:#d96f2c}"
    "@media(prefers-color-scheme:dark){:root{--bg:#11100e;--card:#1a1815;--card2:#24201b;--text:#f4efe7;--muted:#b8aea2;--line:rgba(255,255,255,.11)}}"
    "body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}main{max-width:920px;margin:0 auto;padding:42px}h1{font-size:36px;letter-spacing:-.04em}.row{display:flex;align-items:center;gap:14px;background:var(--card);border:1px solid var(--line);border-radius:18px;margin:10px 0;padding:14px;text-decoration:none;color:var(--text)}.row:hover{border-color:rgba(217,111,44,.38)}.icon{width:34px;height:34px;border-radius:50%%;display:grid;place-items:center;background:var(--card2);color:var(--muted)}strong{display:block}small{display:block;color:var(--muted);margin-top:3px;word-break:break-all}.empty{padding:20px;border:1px dashed var(--line);border-radius:18px;color:var(--muted)}</style></head><body><main><h1>%s</h1>%s</main></body></html>",
    safe_title, safe_title, rows->str);
  g_string_free(rows, TRUE);
  return html;
}

static void recent_page_free(gpointer data) {
  AstraRecentPage *page = data;
  if (page == NULL) return;
  g_free(page->title);
  g_free(page->uri);
  g_free(page);
}

static gboolean recent_uri_should_record(const char *uri) {
  return uri != NULL &&
         (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://") || g_str_has_prefix(uri, "file://"));
}

static void recent_pages_add(const char *title, const char *uri) {
  if (!recent_uri_should_record(uri)) return;
  if (recent_pages == NULL) recent_pages = g_ptr_array_new_with_free_func(recent_page_free);

  for (guint i = 0; i < recent_pages->len; i++) {
    AstraRecentPage *page = g_ptr_array_index(recent_pages, i);
    if (g_strcmp0(page->uri, uri) == 0) {
      g_ptr_array_remove_index(recent_pages, i);
      break;
    }
  }

  AstraRecentPage *page = g_new0(AstraRecentPage, 1);
  page->title = g_strdup(title != NULL && *title != '\0' ? title : uri);
  page->uri = g_strdup(uri);
  g_ptr_array_insert(recent_pages, 0, page);
  while (recent_pages->len > 8) g_ptr_array_remove_index(recent_pages, recent_pages->len - 1);
}

static char *build_welcome_new_tab_html(void) {
  const char *real_name = g_get_real_name();
  const char *user_name = g_get_user_name();
  const char *pretty = (real_name != NULL && *real_name != '\0' && g_strcmp0(real_name, "Unknown") != 0) ? real_name : user_name;
  g_autofree char *safe_name = g_markup_escape_text(pretty != NULL ? pretty : "there", -1);

  GString *recent = g_string_new("");
  if (recent_pages != NULL && recent_pages->len > 0) {
    for (guint i = 0; i < recent_pages->len; i++) {
      AstraRecentPage *page = g_ptr_array_index(recent_pages, i);
      g_autofree char *t = g_markup_escape_text(page->title, -1);
      g_autofree char *u = g_markup_escape_text(page->uri, -1);
      g_string_append_printf(recent,
        "<a class='recent-item' href='%s'><span class='recent-icon'>↗</span><span><strong>%s</strong><small>%s</small></span></a>",
        u, t, u);
    }
  } else {
    g_string_append(recent, "<div class='empty'>Recent pages will appear here.</div>");
  }

  char *html = g_strdup_printf(
    "<!doctype html><html><head><meta charset='utf-8'><title>New Tab</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    ":root{color-scheme:light dark;--bg:#f8f3ec;--card:#fffdf8;--card2:#efe6da;--text:#201a14;--muted:#75695f;--accent:#d96f2c;--line:rgba(50,35,20,.14)}"
    "@media(prefers-color-scheme:dark){:root{--bg:#11100e;--card:#1a1815;--card2:#24201b;--text:#f4efe7;--muted:#b8aea2;--line:rgba(255,255,255,.11)}}"
    "*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 50%% -10%%,rgba(217,111,44,.16),transparent 36%%),var(--bg);color:var(--text);font:15px system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
    "main{min-height:100vh;display:grid;place-items:center;padding:42px}.panel{width:min(820px,100%%)}"
    ".hello{font-size:42px;line-height:1.05;letter-spacing:-.045em;margin:0 0 10px}.sub{color:var(--muted);margin:0 0 28px;font-size:16px}"
    ".search{display:flex;align-items:center;gap:12px;background:var(--card);border:1px solid var(--line);border-radius:999px;padding:10px 12px 10px 18px;box-shadow:0 18px 50px rgba(35,20,5,.08)}"
    ".search span{color:var(--muted)}input{flex:1;border:0;outline:0;background:transparent;color:var(--text);font:18px inherit;padding:12px 4px}button{border:0;border-radius:999px;padding:11px 18px;background:var(--accent);color:white;font-weight:750;cursor:pointer}"
    ".section-title{margin:34px 0 12px;font-size:14px;text-transform:uppercase;letter-spacing:.12em;color:var(--muted);font-weight:800}.recent{display:grid;gap:10px}.recent-item{display:flex;align-items:center;gap:13px;text-decoration:none;color:var(--text);background:color-mix(in srgb,var(--card) 86%%,transparent);border:1px solid var(--line);border-radius:18px;padding:14px 16px}.recent-item:hover{background:var(--card)}.recent-icon{width:34px;height:34px;border-radius:50%%;display:grid;place-items:center;background:var(--card2);color:var(--muted)}strong{display:block;font-size:15px}small{display:block;margin-top:2px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:680px}.empty{color:var(--muted);padding:18px;border:1px dashed var(--line);border-radius:18px}"
    "</style></head><body><main><section class='panel'><h1 class='hello'>Welcome, %s</h1><p class='sub'>Search the web or open an address.</p>"
    "<form class='search' id='search'><span>⌕</span><input id='q' autofocus autocomplete='off' spellcheck='false' placeholder='Search or enter address'><button>Go</button></form>"
    "<div class='section-title'>Recent</div><div class='recent'>%s</div>"
    "</section></main><script>document.getElementById('search').addEventListener('submit',e=>{e.preventDefault();let q=document.getElementById('q').value.trim();if(!q)return;if(/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(q)||q.includes('.')||q.startsWith('/')) location.href=q; else location.href='https://duckduckgo.com/?q='+encodeURIComponent(q);});</script></body></html>",
    safe_name, recent->str);
  g_string_free(recent, TRUE);
  return html;
}


static void notify_uri(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = self->active_app_uri != NULL ? self->active_app_uri :
                    self->virtual_uri != NULL ? self->virtual_uri :
                    webkit_web_view_get_uri(web_view);
  g_signal_emit(self, signals[SIGNAL_URI_CHANGED], 0, uri);
}

static void notify_title(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *title = self->virtual_title != NULL ? self->virtual_title : webkit_web_view_get_title(web_view);
  g_signal_emit(self, signals[SIGNAL_TITLE_CHANGED], 0, title);
}


static void file_history_reset(AstraBrowserView *self);
static void app_state_clear(AstraBrowserView *self);

static void virtual_page_clear(AstraBrowserView *self) {
  g_clear_pointer(&self->virtual_uri, g_free);
  g_clear_pointer(&self->virtual_title, g_free);
}

static void show_web_surface(AstraBrowserView *self) {
  if (self->view_stack != NULL) gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "web");
}

static void virtual_page_set(AstraBrowserView *self, const char *uri, const char *title) {
  g_free(self->virtual_uri);
  g_free(self->virtual_title);
  self->virtual_uri = g_strdup(uri);
  self->virtual_title = g_strdup(title);
}

static void show_warm_placeholder(AstraBrowserView *self) {
  if (self->warm_placeholder != NULL) {
    gtk_widget_set_no_show_all(self->warm_placeholder, FALSE);
    gtk_widget_show_all(self->warm_placeholder);
    gtk_widget_set_opacity(self->warm_placeholder, 1.0);
  }
}

static void hide_warm_placeholder(AstraBrowserView *self) {
  if (self->warm_placeholder != NULL) {
    gtk_widget_hide(self->warm_placeholder);
    gtk_widget_set_no_show_all(self->warm_placeholder, TRUE);
  }
}

static void clear_container_children(GtkWidget *container) {
  if (container == NULL || !GTK_IS_CONTAINER(container)) return;
  GList *children = gtk_container_get_children(GTK_CONTAINER(container));
  for (GList *l = children; l != NULL; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);
}

static void load_internal_html(AstraBrowserView *self, const char *html, const char *virtual_uri, const char *title) {
  show_web_surface(self);
  app_state_clear(self);
  file_history_reset(self);
  virtual_page_set(self, virtual_uri, title);
  show_warm_placeholder(self);

  /* Do not use astra:// or app:// as the WebKit base URI. Some WebKitGTK
   * versions can enter an unsupported-scheme state after load_html() with a
   * custom scheme, which then makes later file:// and app:// navigations look
   * blank. Astra keeps the virtual URI itself and gives WebKit a safe base.
   */
  webkit_web_view_load_html(self->web_view, html, "about:blank");
  g_signal_emit(self, signals[SIGNAL_URI_CHANGED], 0, virtual_uri);
  g_signal_emit(self, signals[SIGNAL_TITLE_CHANGED], 0, title);
  g_signal_emit(self, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_FINISHED);
}

static void file_history_reset(AstraBrowserView *self) {
  if (self->file_history == NULL) return;
  g_ptr_array_set_size(self->file_history, 0);
  self->file_history_index = -1;
  self->file_manager_active = FALSE;
}

static void app_state_clear(AstraBrowserView *self) {
  g_clear_pointer(&self->active_app_uri, g_free);
  g_clear_pointer(&self->active_app_id, g_free);
  g_clear_pointer(&self->active_app_name, g_free);
  g_clear_pointer(&self->active_app_start_uri, g_free);
}


static void file_history_push(AstraBrowserView *self, const char *uri) {
  if (uri == NULL) return;
  if (self->file_history == NULL) {
    self->file_history = g_ptr_array_new_with_free_func(g_free);
  }

  if (self->file_history_index >= 0 &&
      self->file_history_index < (gint)self->file_history->len) {
    const char *current = g_ptr_array_index(self->file_history, self->file_history_index);
    if (g_strcmp0(current, uri) == 0) return;
  }

  while ((gint)self->file_history->len > self->file_history_index + 1) {
    g_ptr_array_remove_index(self->file_history, self->file_history->len - 1);
  }

  g_ptr_array_add(self->file_history, g_strdup(uri));
  self->file_history_index = (gint)self->file_history->len - 1;
}

static void astra_browser_view_load_file_manager_internal(AstraBrowserView *view,
                                                          const char *directory_uri,
                                                          gboolean push_history) {
  show_web_surface(view);
  virtual_page_clear(view);
  app_state_clear(view);

  /*
   * The warm placeholder is only for Astra internal pages such as
   * astra://new-tab. If the user types file:/// into a tab that was showing
   * the new-tab placeholder, that overlay must be removed before rendering the
   * file-manager HTML; otherwise the file manager is loaded underneath an
   * invisible/opaque placeholder and looks like file:// is broken.
   */
  hide_warm_placeholder(view);

  if (push_history) file_history_push(view, directory_uri);
  view->file_manager_active = TRUE;

  g_autofree char *html = astra_file_manager_build_page_html(directory_uri);

  /*
   * Keep file-manager pages browser-owned. The chrome still exposes the real
   * file:// URI through astra_browser_view_get_uri(), but WebKit receives a
   * safe base URI so it does not try to treat the generated page itself as a
   * native file:// directory load. All item links are absolute file:// links,
   * and clicks are handled through the astraFiles message bridge.
   */
  webkit_web_view_load_html(view->web_view, html, "about:blank");

  /* load_html() does not always emit the same history/URI signals as a normal
   * WebKit navigation. Emit our public signals so the browser chrome updates
   * immediately and the toolbar buttons reflect the file-manager history.
   */
  recent_pages_add("Files", directory_uri);
  g_signal_emit(view, signals[SIGNAL_URI_CHANGED], 0, directory_uri);
  g_signal_emit(view, signals[SIGNAL_TITLE_CHANGED], 0, "Files");
  g_signal_emit(view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_FINISHED);
}

static void settings_message_received(WebKitUserContentManager *manager,
                                      WebKitJavascriptResult *result,
                                      gpointer user_data) {
  (void)manager;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  JSCValue *value = webkit_javascript_result_get_js_value(result);
  if (value == NULL) return;

  g_autofree char *message = jsc_value_to_string(value);
  astra_settings_set_from_message(message);
  astra_settings_apply_to_web_view(self->web_view);
  g_signal_emit(self, signals[SIGNAL_SETTINGS_CHANGED], 0);
}

static void load_file_manager_error(AstraBrowserView *self, const char *message) {
  g_autofree char *safe = g_markup_escape_text(message != NULL ? message : "Unable to open file", -1);
  g_autofree char *html = g_strdup_printf(
    "<!doctype html><meta charset='utf-8'><title>Files</title>"
    "<style>body{font:15px system-ui;background:#f8f3ec;color:#201a14;padding:44px}"
    ".card{max-width:680px;background:#fffdf8;border:1px solid rgba(90,70,50,.16);border-radius:22px;padding:24px;box-shadow:0 18px 50px rgba(20,10,0,.08)}"
    "</style><div class='card'><h1>Could not open this file</h1><p>%s</p></div>",
    safe
  );
  load_internal_html(self, html, "astra://files/error", "Files");
}

static void files_message_received(WebKitUserContentManager *manager,
                                   WebKitJavascriptResult *result,
                                   gpointer user_data) {
  (void)manager;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  JSCValue *value = webkit_javascript_result_get_js_value(result);
  if (value == NULL) return;

  g_autofree char *message = jsc_value_to_string(value);
  if (message == NULL) return;

  char **parts = g_strsplit(message, "|", 2);
  const char *command = parts[0];
  const char *uri = parts[1];

  if (command != NULL && uri != NULL && g_str_equal(command, "browse")) {
    astra_browser_view_load_file_manager_internal(self, uri, TRUE);
  } else if (command != NULL && uri != NULL && g_str_equal(command, "open")) {
    g_autoptr(GError) error = NULL;
    if (!astra_file_manager_open_uri_external(uri, &error)) {
      load_file_manager_error(self, error != NULL ? error->message : "No default application found for this file.");
    }
  }

  g_strfreev(parts);
}


static void astra_browser_view_load_apps(AstraBrowserView *view);
void astra_browser_view_load_bookmarks(AstraBrowserView *view);
void astra_browser_view_load_history(AstraBrowserView *view);
void astra_browser_view_load_downloads(AstraBrowserView *view);
static void astra_browser_view_load_app(AstraBrowserView *view, const char *app_uri);

static char *app_uri_from_internal_launch_uri(const char *uri) {
  const char *prefix = "astra://launch-app/";
  if (uri == NULL || !g_str_has_prefix(uri, prefix)) return NULL;

  const char *id = uri + strlen(prefix);
  if (id == NULL || *id == '\0') return NULL;

  const char *slash = strchr(id, '/');
  if (slash != NULL) {
    g_autofree char *piece = g_strndup(id, slash - id);
    return g_strdup_printf("app://%s", piece);
  }

  return g_strdup_printf("app://%s", id);
}

static gboolean decide_policy(WebKitWebView *web_view,
                              WebKitPolicyDecision *decision,
                              WebKitPolicyDecisionType decision_type,
                              gpointer user_data) {
  (void)web_view;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);

  if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;

  WebKitNavigationPolicyDecision *nav = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
  WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(nav);
  WebKitURIRequest *request = webkit_navigation_action_get_request(action);
  const char *uri = request != NULL ? webkit_uri_request_get_uri(request) : NULL;
  if (uri == NULL) return FALSE;

  if (g_str_has_prefix(uri, "astra://new-tab")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_new_tab(self);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "astra://settings")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_settings(self);
    return TRUE;
  }

  g_autofree char *launched_app_uri = app_uri_from_internal_launch_uri(uri);
  if (launched_app_uri != NULL) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_app(self, launched_app_uri);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "astra://apps")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_apps(self);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "astra://bookmarks")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_bookmarks(self);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "astra://history")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_history(self);
    return TRUE;
  }

  if (g_str_has_prefix(uri, "astra://downloads")) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_downloads(self);
    return TRUE;
  }

  if (astra_webapp_uri_is_app(uri)) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_app(self, uri);
    return TRUE;
  }

  if (astra_file_manager_uri_is_directory(uri)) {
    webkit_policy_decision_ignore(decision);
    astra_browser_view_load_file_manager_internal(self, uri, TRUE);
    return TRUE;
  }

  return FALSE;
}

static gboolean load_failed(WebKitWebView *web_view,
                            WebKitLoadEvent load_event,
                            const gchar *failing_uri,
                            GError *error,
                            gpointer user_data) {
  (void)web_view;
  (void)load_event;
  (void)failing_uri;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);

  /* Policy-cancelled virtual app:// navigations are expected. They are
   * resolved by Astra to a real https:// StartURI before WebKit should ever
   * try to render them. Treat these as handled/no-op, not as offline failures.
   */
  if (failing_uri != NULL && astra_webapp_uri_is_app(failing_uri)) return TRUE;
  if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) return FALSE;

  if (self->active_app_start_uri == NULL) return FALSE;

  g_autofree char *html = astra_webapp_build_offline_page_html(self->active_app_name,
                                                               self->active_app_start_uri,
                                                               error != NULL ? error->message : NULL);
  /* Use a safe internal base URI. Using app:// as the base can leave some
   * WebKitGTK builds stuck in an unsupported-scheme error state after a failed
   * installed-app load.
   */
  virtual_page_set(self, self->active_app_uri != NULL ? self->active_app_uri : "astra://offline",
                   self->active_app_name != NULL ? self->active_app_name : "Web App");
  webkit_web_view_load_html(self->web_view, html, "about:blank");
  g_signal_emit(self, signals[SIGNAL_URI_CHANGED], 0, self->virtual_uri);
  g_signal_emit(self, signals[SIGNAL_TITLE_CHANGED], 0, self->virtual_title);
  return TRUE;
}

static void load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *text = "Ready";
  if (event == WEBKIT_LOAD_STARTED) text = "Loading…";
  if (event == WEBKIT_LOAD_REDIRECTED) text = "Redirecting…";
  if (event == WEBKIT_LOAD_COMMITTED) text = "Rendering…";
  if (event == WEBKIT_LOAD_FINISHED) {
    text = "Ready";
    hide_warm_placeholder(self);
    if (self->virtual_uri == NULL && self->active_app_uri == NULL && !self->file_manager_active) {
      if (!self->private_mode) recent_pages_add(webkit_web_view_get_title(web_view), webkit_web_view_get_uri(web_view));
    }
  }
  gtk_label_set_text(GTK_LABEL(self->status_label), text);
  g_signal_emit(self, signals[SIGNAL_LOAD_CHANGED], 0, event);
}


static const char *current_file_manager_uri(AstraBrowserView *self) {
  if (!self->file_manager_active || self->file_history == NULL || self->file_history_index < 0) return NULL;
  if (self->file_history_index >= (gint)self->file_history->len) return NULL;
  return g_ptr_array_index(self->file_history, self->file_history_index);
}

static gboolean uri_is_local_dir(const char *uri) {
  if (uri == NULL || !g_str_has_prefix(uri, "file://")) return FALSE;
  g_autofree char *path = g_filename_from_uri(uri, NULL, NULL);
  return path != NULL && g_file_test(path, G_FILE_TEST_IS_DIR);
}

static void show_message_dialog(AstraBrowserView *self,
                                GtkMessageType type,
                                const char *title,
                                const char *message) {
  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
  GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                             GTK_DIALOG_MODAL,
                                             type,
                                             GTK_BUTTONS_CLOSE,
                                             "%s",
                                             title != NULL ? title : "Astra Files");
  if (message != NULL) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

static gboolean confirm_action(AstraBrowserView *self, const char *title, const char *message) {
  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
  GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_WARNING,
                                             GTK_BUTTONS_CANCEL,
                                             "%s",
                                             title);
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Delete", GTK_RESPONSE_ACCEPT);
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  return response == GTK_RESPONSE_ACCEPT;
}

static char *prompt_for_text(AstraBrowserView *self,
                             const char *title,
                             const char *label_text,
                             const char *initial_text) {
  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                                  GTK_DIALOG_MODAL,
                                                  "Cancel", GTK_RESPONSE_CANCEL,
                                                  "OK", GTK_RESPONSE_ACCEPT,
                                                  NULL);
  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width(GTK_CONTAINER(box), 14);
  GtkWidget *label = gtk_label_new(label_text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  GtkWidget *entry = gtk_entry_new();
  if (initial_text != NULL) gtk_entry_set_text(GTK_ENTRY(entry), initial_text);
  gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(area), box);
  gtk_widget_show_all(dialog);
  char *result = NULL;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    if (text != NULL && *text != '\0') result = g_strdup(text);
  }
  gtk_widget_destroy(dialog);
  return result;
}

static void reload_current_file_manager(AstraBrowserView *self) {
  const char *current = current_file_manager_uri(self);
  if (current != NULL) astra_browser_view_load_file_manager_internal(self, current, FALSE);
}

static void menu_open_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  if (uri_is_local_dir(uri)) {
    astra_browser_view_load_file_manager_internal(self, uri, TRUE);
    return;
  }
  g_autoptr(GError) error = NULL;
  if (!astra_file_manager_open_uri_external(uri, &error)) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not open file", error ? error->message : "No default app found.");
  }
}

static void menu_open_external_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  g_autoptr(GError) error = NULL;
  if (!astra_file_manager_open_uri_external(uri, &error)) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not open with default app", error ? error->message : "No default app found.");
  }
}

static void menu_open_new_tab_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri != NULL) g_signal_emit(self, signals[SIGNAL_OPEN_URI_NEW_TAB], 0, uri);
}

static void menu_copy_path_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  g_autofree char *path = uri != NULL ? g_filename_from_uri(uri, NULL, NULL) : NULL;
  if (path == NULL) return;
  GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text(clipboard, path, -1);
}

static void menu_rename_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  g_autoptr(GFile) file = g_file_new_for_uri(uri);
  g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::display-name", G_FILE_QUERY_INFO_NONE, NULL, NULL);
  const char *old_name = info ? g_file_info_get_display_name(info) : NULL;
  g_autofree char *new_name = prompt_for_text(self, "Rename", "New name", old_name);
  if (new_name == NULL) return;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) renamed = g_file_set_display_name(file, new_name, NULL, &error);
  if (renamed == NULL) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not rename", error ? error->message : "Rename failed.");
    return;
  }
  reload_current_file_manager(self);
}

static void menu_trash_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  g_autoptr(GFile) file = g_file_new_for_uri(uri);
  g_autoptr(GError) error = NULL;
  if (!g_file_trash(file, NULL, &error)) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not move to Trash", error ? error->message : "Trash operation failed.");
    return;
  }
  reload_current_file_manager(self);
}

static void menu_delete_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  if (!confirm_action(self, "Delete permanently?", "This removes the selected item without moving it to Trash.")) return;
  g_autoptr(GFile) file = g_file_new_for_uri(uri);
  g_autoptr(GError) error = NULL;
  if (!g_file_delete(file, NULL, &error)) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not delete", error ? error->message : "Delete failed.");
    return;
  }
  reload_current_file_manager(self);
}

static void menu_properties_cb(GtkMenuItem *item, gpointer user_data) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(item), "astra-uri");
  if (uri == NULL) return;
  g_autoptr(GFile) file = g_file_new_for_uri(uri);
  g_autoptr(GError) error = NULL;
  g_autoptr(GFileInfo) info = g_file_query_info(file,
                                                "standard::display-name,standard::type,standard::content-type,standard::size,time::modified",
                                                G_FILE_QUERY_INFO_NONE,
                                                NULL,
                                                &error);
  if (info == NULL) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not read properties", error ? error->message : "Unable to query file info.");
    return;
  }
  const char *name = g_file_info_get_display_name(info);
  const char *content_type = g_file_info_get_content_type(info);
  g_autofree char *type_desc = content_type != NULL ? g_content_type_get_description(content_type) : g_strdup("Unknown");
  guint64 size = g_file_info_get_size(info);
  g_autofree char *size_text = g_format_size(size);
  g_autofree char *path = g_filename_from_uri(uri, NULL, NULL);
  g_autofree char *message = g_strdup_printf("Type: %s\nSize: %s\nLocation: %s", type_desc, size_text, path ? path : uri);
  show_message_dialog(self, GTK_MESSAGE_INFO, name ? name : "Properties", message);
}

static void menu_new_folder_cb(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  const char *dir_uri = current_file_manager_uri(self);
  if (dir_uri == NULL) return;
  g_autofree char *dir_path = g_filename_from_uri(dir_uri, NULL, NULL);
  if (dir_path == NULL) return;
  g_autofree char *name = prompt_for_text(self, "New Folder", "Folder name", "New Folder");
  if (name == NULL) return;
  g_autofree char *child_path = g_build_filename(dir_path, name, NULL);
  g_autoptr(GFile) child = g_file_new_for_path(child_path);
  g_autoptr(GError) error = NULL;
  if (!g_file_make_directory(child, NULL, &error)) {
    show_message_dialog(self, GTK_MESSAGE_ERROR, "Could not create folder", error ? error->message : "Create folder failed.");
    return;
  }
  reload_current_file_manager(self);
}

static void menu_refresh_cb(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  reload_current_file_manager(ASTRA_BROWSER_VIEW(user_data));
}

static GtkWidget *menu_item(const char *label, GCallback callback, AstraBrowserView *self, const char *uri) {
  GtkWidget *item = gtk_menu_item_new_with_label(label);
  if (uri != NULL) g_object_set_data_full(G_OBJECT(item), "astra-uri", g_strdup(uri), g_free);
  g_signal_connect(item, "activate", callback, self);
  return item;
}

static gboolean file_manager_context_menu(WebKitWebView *web_view,
                                          WebKitContextMenu *context_menu,
                                          GdkEvent *event,
                                          WebKitHitTestResult *hit_test,
                                          gpointer user_data) {
  (void)web_view;
  (void)context_menu;
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(user_data);
  if (!self->file_manager_active) return FALSE;

  const char *target_uri = NULL;
  if (hit_test != NULL && webkit_hit_test_result_context_is_link(hit_test)) {
    target_uri = webkit_hit_test_result_get_link_uri(hit_test);
  }

  GtkWidget *menu = gtk_menu_new();
  if (target_uri != NULL && g_str_has_prefix(target_uri, "file://")) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Open", G_CALLBACK(menu_open_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Open in New Tab", G_CALLBACK(menu_open_new_tab_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Open With Default App", G_CALLBACK(menu_open_external_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Copy Path", G_CALLBACK(menu_copy_path_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Rename…", G_CALLBACK(menu_rename_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Move to Trash", G_CALLBACK(menu_trash_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Delete Permanently…", G_CALLBACK(menu_delete_cb), self, target_uri));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Properties", G_CALLBACK(menu_properties_cb), self, target_uri));
  } else {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("New Folder…", G_CALLBACK(menu_new_folder_cb), self, NULL));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Refresh", G_CALLBACK(menu_refresh_cb), self, NULL));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    const char *current = current_file_manager_uri(self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item("Properties", G_CALLBACK(menu_properties_cb), self, current));
  }

  gtk_widget_show_all(menu);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
  return TRUE;
}


static void astra_browser_view_dispose(GObject *object) {
  AstraBrowserView *self = ASTRA_BROWSER_VIEW(object);
  if (self->file_history != NULL) {
    g_ptr_array_unref(self->file_history);
    self->file_history = NULL;
  }
  app_state_clear(self);
  virtual_page_clear(self);
  G_OBJECT_CLASS(astra_browser_view_parent_class)->dispose(object);
}

static void astra_browser_view_class_init(AstraBrowserViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = astra_browser_view_dispose;
  signals[SIGNAL_URI_CHANGED] = g_signal_new("astra-uri-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[SIGNAL_TITLE_CHANGED] = g_signal_new("astra-title-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[SIGNAL_LOAD_CHANGED] = g_signal_new("astra-load-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
  signals[SIGNAL_SETTINGS_CHANGED] = g_signal_new("astra-settings-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_OPEN_URI_NEW_TAB] = g_signal_new("astra-open-uri-new-tab", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void astra_browser_view_init(AstraBrowserView *self) {
  gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);

  self->file_history = g_ptr_array_new_with_free_func(g_free);
  self->file_history_index = -1;
  self->file_manager_active = FALSE;
  self->private_mode = next_private_mode;

  WebKitWebContext *context = self->private_mode ? webkit_web_context_new_ephemeral() : webkit_web_context_get_default();
  astra_settings_apply_to_context(context);

  self->web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
  if (self->private_mode) g_object_unref(context);

  WebKitUserContentManager *content_manager = webkit_web_view_get_user_content_manager(self->web_view);
  webkit_user_content_manager_register_script_message_handler(content_manager, "astraSettings");
  g_signal_connect(content_manager, "script-message-received::astraSettings", G_CALLBACK(settings_message_received), self);

  webkit_user_content_manager_register_script_message_handler(content_manager, "astraFiles");
  g_signal_connect(content_manager, "script-message-received::astraFiles", G_CALLBACK(files_message_received), self);
  gtk_widget_set_hexpand(GTK_WIDGET(self->web_view), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self->web_view), TRUE);

  /* Match WebKit's clear/background color to the Astra page surface. Without
   * this, creating a new internal tab can briefly expose WebKit's default
   * white/blank backing store before the new-tab HTML paints.
   */
  GdkRGBA astra_bg;
  if (gdk_rgba_parse(&astra_bg, "#f8f3ec")) {
    webkit_web_view_set_background_color(self->web_view, &astra_bg);
  }

  astra_settings_apply_to_web_view(self->web_view);

  self->overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(self->overlay, TRUE);
  gtk_widget_set_vexpand(self->overlay, TRUE);
  gtk_container_add(GTK_CONTAINER(self->overlay), GTK_WIDGET(self->web_view));

  self->warm_placeholder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(self->warm_placeholder, TRUE);
  gtk_widget_set_vexpand(self->warm_placeholder, TRUE);
  gtk_widget_set_halign(self->warm_placeholder, GTK_ALIGN_FILL);
  gtk_widget_set_valign(self->warm_placeholder, GTK_ALIGN_FILL);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->warm_placeholder), "astra-warm-placeholder");
  GtkWidget *placeholder_label = gtk_label_new("Astra");
  gtk_widget_set_halign(placeholder_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(placeholder_label, GTK_ALIGN_CENTER);
  gtk_style_context_add_class(gtk_widget_get_style_context(placeholder_label), "astra-warm-placeholder-label");
  gtk_box_pack_start(GTK_BOX(self->warm_placeholder), placeholder_label, TRUE, TRUE, 0);
  gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->warm_placeholder);
  gtk_widget_set_no_show_all(self->warm_placeholder, TRUE);
  gtk_widget_hide(self->warm_placeholder);

  self->view_stack = gtk_stack_new();
  gtk_widget_set_hexpand(self->view_stack, TRUE);
  gtk_widget_set_vexpand(self->view_stack, TRUE);
  gtk_stack_add_named(GTK_STACK(self->view_stack), self->overlay, "web");

  /* Keep the settings route as a permanent GtkStack child. Earlier builds
   * removed and re-added a child named "settings" every time the page was
   * opened; on some GTK/theme combinations that left the stack showing a blank
   * child or the previous WebKit surface. The host stays in the stack and only
   * its contents are rebuilt.
   */
  self->settings_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(self->settings_host, TRUE);
  gtk_widget_set_vexpand(self->settings_host, TRUE);
  gtk_stack_add_named(GTK_STACK(self->view_stack), self->settings_host, "settings");

  gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "web");
  gtk_box_pack_start(GTK_BOX(self), self->view_stack, TRUE, TRUE, 0);

  self->status_label = gtk_label_new("Ready");
  gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->status_label), "astra-statusbar");
  gtk_box_pack_end(GTK_BOX(self), self->status_label, FALSE, FALSE, 0);

  g_signal_connect(self->web_view, "notify::uri", G_CALLBACK(notify_uri), self);
  g_signal_connect(self->web_view, "notify::title", G_CALLBACK(notify_title), self);
  g_signal_connect(self->web_view, "load-changed", G_CALLBACK(load_changed), self);
  g_signal_connect(self->web_view, "load-failed", G_CALLBACK(load_failed), self);
  g_signal_connect(self->web_view, "decide-policy", G_CALLBACK(decide_policy), self);
  g_signal_connect(self->web_view, "context-menu", G_CALLBACK(file_manager_context_menu), self);
}

AstraBrowserView *astra_browser_view_new(void) {
  return astra_browser_view_new_private(FALSE);
}

AstraBrowserView *astra_browser_view_new_private(gboolean private_mode) {
  next_private_mode = private_mode;
  AstraBrowserView *view = g_object_new(ASTRA_TYPE_BROWSER_VIEW, NULL);
  next_private_mode = FALSE;
  return view;
}

WebKitWebView *astra_browser_view_get_web_view(AstraBrowserView *view) {
  return view->web_view;
}

void astra_browser_view_load_uri(AstraBrowserView *view, const char *uri) {
  g_autofree char *normalized = astra_uri_normalize(uri);

  g_autofree char *launched_app_uri = app_uri_from_internal_launch_uri(normalized);
  if (launched_app_uri != NULL) {
    astra_browser_view_load_app(view, launched_app_uri);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://new-tab")) {
    astra_browser_view_load_new_tab(view);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://settings")) {
    astra_browser_view_load_settings(view);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://apps")) {
    astra_browser_view_load_apps(view);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://bookmarks")) {
    astra_browser_view_load_bookmarks(view);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://history")) {
    astra_browser_view_load_history(view);
    return;
  }

  if (g_str_has_prefix(normalized, "astra://downloads")) {
    astra_browser_view_load_downloads(view);
    return;
  }

  if (astra_webapp_uri_is_app(normalized)) {
    astra_browser_view_load_app(view, normalized);
    return;
  }

  if (astra_file_manager_uri_is_directory(normalized)) {
    astra_browser_view_load_file_manager_internal(view, normalized, TRUE);
    return;
  }

  show_web_surface(view);
  virtual_page_clear(view);
  app_state_clear(view);
  file_history_reset(view);
  webkit_web_view_load_uri(view->web_view, normalized);
}

void astra_browser_view_load_new_tab(AstraBrowserView *view) {
  g_autofree char *html = build_welcome_new_tab_html();
  load_internal_html(view, html, "astra://new-tab", "New Tab");
}

void astra_browser_view_load_file_manager(AstraBrowserView *view, const char *directory_uri) {
  astra_browser_view_load_file_manager_internal(view, directory_uri, TRUE);
}

static void settings_page_changed(gpointer user_data) {
  AstraBrowserView *view = ASTRA_BROWSER_VIEW(user_data);
  astra_settings_apply_to_web_view(view->web_view);
  g_signal_emit(view, signals[SIGNAL_SETTINGS_CHANGED], 0);
}

void astra_browser_view_load_settings(AstraBrowserView *view) {
  app_state_clear(view);
  file_history_reset(view);
  hide_warm_placeholder(view);
  virtual_page_set(view, "astra://settings", "Settings");

  if (view->settings_host == NULL) return;

  /* Settings is a native GTK page, not WebKit HTML. The settings host is a
   * permanent GtkStack child; only its contents are rebuilt. This avoids
   * fragile remove/add/rename behavior in GtkStack and prevents WebKit or the
   * warm placeholder from covering the GTK settings page.
   */
  clear_container_children(view->settings_host);
  view->settings_page = astra_settings_build_gtk_page(view->web_view, settings_page_changed, view);
  gtk_widget_set_hexpand(view->settings_page, TRUE);
  gtk_widget_set_vexpand(view->settings_page, TRUE);
  gtk_box_pack_start(GTK_BOX(view->settings_host), view->settings_page, TRUE, TRUE, 0);

  gtk_stack_set_visible_child_name(GTK_STACK(view->view_stack), "settings");
  gtk_widget_show_all(view->settings_host);
  gtk_widget_show_all(view->view_stack);

  g_signal_emit(view, signals[SIGNAL_URI_CHANGED], 0, "astra://settings");
  g_signal_emit(view, signals[SIGNAL_TITLE_CHANGED], 0, "Settings");
  g_signal_emit(view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_FINISHED);
}

static void astra_browser_view_load_apps(AstraBrowserView *view) {
  g_autofree char *html = astra_webapp_build_apps_page_html();
  load_internal_html(view, html, "astra://apps", "Installed Web Apps");
}

void astra_browser_view_load_bookmarks(AstraBrowserView *view) {
  g_autofree char *html = build_listing_page_html("Bookmarks", "No bookmarks yet. Use the Astra menu to bookmark the current page.", bookmarks, "★");
  load_internal_html(view, html, "astra://bookmarks", "Bookmarks");
}

void astra_browser_view_load_history(AstraBrowserView *view) {
  g_autofree char *html = build_listing_page_html("History", "No history yet.", recent_pages, "↺");
  load_internal_html(view, html, "astra://history", "History");
}

void astra_browser_view_load_downloads(AstraBrowserView *view) {
  g_autofree char *html = astra_downloads_build_page_html();
  load_internal_html(view, html, "astra://downloads", "Downloads");
}

void astra_browser_view_bookmark_current_page(AstraBrowserView *view) {
  const char *uri = astra_browser_view_get_uri(view);
  const char *title = view->virtual_title != NULL ? view->virtual_title : webkit_web_view_get_title(view->web_view);
  bookmarks_add(title, uri);
}

gboolean astra_browser_view_is_private(AstraBrowserView *view) {
  return view != NULL && view->private_mode;
}

static GtkWidget *dialog_row_for_item(const char *icon, const char *title, const char *uri) {
  GtkWidget *row = gtk_list_box_row_new();
  g_object_set_data_full(G_OBJECT(row), "astra-uri", g_strdup(uri), g_free);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(box), 10);
  gtk_container_add(GTK_CONTAINER(row), box);

  GtkWidget *icon_label = gtk_label_new(icon);
  gtk_widget_set_size_request(icon_label, 32, 32);
  gtk_box_pack_start(GTK_BOX(box), icon_label, FALSE, FALSE, 0);

  GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start(GTK_BOX(box), texts, TRUE, TRUE, 0);

  GtkWidget *title_label = gtk_label_new(title != NULL && *title ? title : uri);
  gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(texts), title_label, FALSE, FALSE, 0);

  GtkWidget *uri_label = gtk_label_new(uri != NULL ? uri : "");
  gtk_label_set_xalign(GTK_LABEL(uri_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(uri_label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_style_context_add_class(gtk_widget_get_style_context(uri_label), "dim-label");
  gtk_box_pack_start(GTK_BOX(texts), uri_label, FALSE, FALSE, 0);

  return row;
}

static void library_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  AstraBrowserView *view = ASTRA_BROWSER_VIEW(user_data);
  const char *uri = g_object_get_data(G_OBJECT(row), "astra-uri");
  GtkWidget *dialog = g_object_get_data(G_OBJECT(box), "astra-dialog");
  if (uri != NULL && *uri != '\0') astra_browser_view_load_uri(view, uri);
  if (dialog != NULL) gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
}

static void show_library_dialog(AstraBrowserView *view,
                                GtkWindow *parent,
                                const char *title,
                                const char *empty,
                                GPtrArray *items,
                                const char *icon) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                  parent,
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 520);

  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(outer), 16);
  gtk_container_add(GTK_CONTAINER(area), outer);

  GtkWidget *heading = gtk_label_new(title);
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(heading), "astra-settings-title");
  gtk_box_pack_start(GTK_BOX(outer), heading, FALSE, FALSE, 0);

  GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_box_pack_start(GTK_BOX(outer), scroller, TRUE, TRUE, 0);

  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  g_object_set_data(G_OBJECT(list), "astra-dialog", dialog);
  gtk_container_add(GTK_CONTAINER(scroller), list);
  g_signal_connect(list, "row-activated", G_CALLBACK(library_row_activated), view);

  if (items != NULL && items->len > 0) {
    for (guint i = 0; i < items->len; i++) {
      AstraRecentPage *page = g_ptr_array_index(items, i);
      gtk_container_add(GTK_CONTAINER(list), dialog_row_for_item(icon, page->title, page->uri));
    }
  } else {
    GtkWidget *empty_label = gtk_label_new(empty);
    gtk_label_set_xalign(GTK_LABEL(empty_label), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(empty_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(outer), empty_label, FALSE, FALSE, 0);
  }

  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

void astra_browser_view_show_bookmarks_dialog(AstraBrowserView *view, GtkWindow *parent) {
  show_library_dialog(view, parent, "Bookmarks", "No bookmarks yet. Use Ctrl+D or the Astra menu to bookmark the current page.", bookmarks, "★");
}

void astra_browser_view_show_history_dialog(AstraBrowserView *view, GtkWindow *parent) {
  show_library_dialog(view, parent, "History", "No history yet.", recent_pages, "↺");
}


static void astra_browser_view_load_app(AstraBrowserView *view, const char *app_uri) {
  g_autoptr(GError) error = NULL;
  g_autofree char *id = NULL;
  g_autofree char *name = NULL;
  g_autofree char *start_uri = astra_webapp_resolve_start_uri(app_uri, &id, &name, &error);

  if (start_uri == NULL) {
    app_state_clear(view);
    file_history_reset(view);
    g_autofree char *safe = g_markup_escape_text(error != NULL ? error->message : "App not found", -1);
    g_autofree char *html = g_strdup_printf("<!doctype html><meta charset=\"utf-8\"><title>App not found</title><body style=\"font:16px system-ui;background:#f8f3ec;color:#201a14;padding:42px\"><h1>App not found</h1><p>%s</p><p>Open <code>astra://apps</code> to see installed web apps.</p></body>", safe);
    load_internal_html(view, html, "astra://apps/error", "App not found");
    return;
  }

  show_web_surface(view);
  virtual_page_clear(view);
  app_state_clear(view);
  file_history_reset(view);
  view->active_app_uri = g_strdup(app_uri);
  view->active_app_id = g_steal_pointer(&id);
  view->active_app_name = g_steal_pointer(&name);
  view->active_app_start_uri = g_strdup(start_uri);

  g_signal_emit(view, signals[SIGNAL_URI_CHANGED], 0, view->active_app_uri);
  g_signal_emit(view, signals[SIGNAL_TITLE_CHANGED], 0, view->active_app_name != NULL ? view->active_app_name : "Web App");
  g_signal_emit(view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_STARTED);
  webkit_web_view_load_uri(view->web_view, start_uri);
}

void astra_browser_view_reload_settings_if_open(AstraBrowserView *view) {
  const char *uri = astra_browser_view_get_uri(view);
  if (uri != NULL && g_str_has_prefix(uri, "astra://settings")) {
    astra_browser_view_load_settings(view);
  } else if (uri != NULL && g_str_has_prefix(uri, "astra://apps")) {
    astra_browser_view_load_apps(view);
  } else if (uri != NULL && g_str_has_prefix(uri, "astra://bookmarks")) {
    astra_browser_view_load_bookmarks(view);
  } else if (uri != NULL && g_str_has_prefix(uri, "astra://history")) {
    astra_browser_view_load_history(view);
  } else if (uri != NULL && g_str_has_prefix(uri, "astra://downloads")) {
    astra_browser_view_load_downloads(view);
  }
}


gboolean astra_browser_view_can_go_back(AstraBrowserView *view) {
  if (view->file_manager_active) return view->file_history_index > 0;
  return webkit_web_view_can_go_back(view->web_view);
}

gboolean astra_browser_view_can_go_forward(AstraBrowserView *view) {
  if (view->file_manager_active) {
    return view->file_history_index >= 0 &&
           view->file_history_index < (gint)view->file_history->len - 1;
  }
  return webkit_web_view_can_go_forward(view->web_view);
}

void astra_browser_view_go_back(AstraBrowserView *view) {
  if (view->file_manager_active) {
    if (!astra_browser_view_can_go_back(view)) return;
    view->file_history_index--;
    const char *uri = g_ptr_array_index(view->file_history, view->file_history_index);
    astra_browser_view_load_file_manager_internal(view, uri, FALSE);
    return;
  }
  if (webkit_web_view_can_go_back(view->web_view)) webkit_web_view_go_back(view->web_view);
}

void astra_browser_view_go_forward(AstraBrowserView *view) {
  if (view->file_manager_active) {
    if (!astra_browser_view_can_go_forward(view)) return;
    view->file_history_index++;
    const char *uri = g_ptr_array_index(view->file_history, view->file_history_index);
    astra_browser_view_load_file_manager_internal(view, uri, FALSE);
    return;
  }
  if (webkit_web_view_can_go_forward(view->web_view)) webkit_web_view_go_forward(view->web_view);
}

void astra_browser_view_reload(AstraBrowserView *view) {
  if (view->file_manager_active && view->file_history_index >= 0) {
    const char *uri = g_ptr_array_index(view->file_history, view->file_history_index);
    astra_browser_view_load_file_manager_internal(view, uri, FALSE);
    return;
  }
  webkit_web_view_reload(view->web_view);
}

const char *astra_browser_view_get_uri(AstraBrowserView *view) {
  if (view->active_app_uri != NULL) return view->active_app_uri;
  const char *file_uri = current_file_manager_uri(view);
  if (file_uri != NULL) return file_uri;
  if (view->virtual_uri != NULL) return view->virtual_uri;
  return webkit_web_view_get_uri(view->web_view);
}
