#include "astra/astra-file-manager.h"

#include <gio/gio.h>
#include <string.h>

typedef struct {
  gchar *name;
  gchar *path;
  gchar *uri;
  gchar *content_type;
  gboolean is_dir;
} AstraFileItem;

static void file_item_free(AstraFileItem *item) {
  if (item == NULL) return;
  g_free(item->name);
  g_free(item->path);
  g_free(item->uri);
  g_free(item->content_type);
  g_free(item);
}

static gint file_item_compare(gconstpointer a, gconstpointer b) {
  const AstraFileItem *ia = a;
  const AstraFileItem *ib = b;
  if (ia->is_dir != ib->is_dir) return ia->is_dir ? -1 : 1;
  return g_utf8_collate(ia->name, ib->name);
}

static gchar *html_escape(const char *text) {
  return g_markup_escape_text(text != NULL ? text : "", -1);
}

static gchar *js_escape(const char *text) {
  if (text == NULL) return g_strdup("");
  GString *out = g_string_new(NULL);
  for (const char *p = text; *p != '\0'; p++) {
    switch (*p) {
      case '\\': g_string_append(out, "\\\\"); break;
      case '\'': g_string_append(out, "\\\'"); break;
      case '\n': g_string_append(out, "\\n"); break;
      case '\r': g_string_append(out, "\\r"); break;
      case '\t': g_string_append(out, "\\t"); break;
      default: g_string_append_c(out, *p); break;
    }
  }
  return g_string_free(out, FALSE);
}

static gchar *uri_to_path(const char *uri) {
  if (uri == NULL || !g_str_has_prefix(uri, "file://")) return NULL;
  return g_filename_from_uri(uri, NULL, NULL);
}

gboolean astra_file_manager_uri_is_directory(const char *uri) {
  g_autofree gchar *path = uri_to_path(uri);
  if (path == NULL) return FALSE;
  return g_file_test(path, G_FILE_TEST_IS_DIR);
}

gboolean astra_file_manager_open_uri_external(const char *uri, GError **error) {
  if (uri == NULL || !g_str_has_prefix(uri, "file://")) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Only local file:// URIs can be opened externally");
    return FALSE;
  }

  return g_app_info_launch_default_for_uri(uri, NULL, error);
}

static const char *icon_for_item(const AstraFileItem *item) {
  if (item->is_dir) return "📁";
  if (item->content_type == NULL) return "📄";
  if (g_str_has_prefix(item->content_type, "image/")) return "🖼";
  if (g_str_has_prefix(item->content_type, "video/")) return "🎞";
  if (g_str_has_prefix(item->content_type, "audio/")) return "🎵";
  if (g_str_has_prefix(item->content_type, "text/")) return "📝";
  if (g_str_equal(item->content_type, "application/pdf")) return "📕";
  if (strstr(item->content_type, "zip") || strstr(item->content_type, "tar") || strstr(item->content_type, "compressed")) return "📦";
  if (strstr(item->content_type, "executable") || strstr(item->content_type, "x-shellscript")) return "⚙";
  return "📄";
}

static const char *sidebar_icon_for_label(const char *label) {
  if (g_strcmp0(label, "Home") == 0) return "⌂";
  if (g_strcmp0(label, "Desktop") == 0) return "▣";
  if (g_strcmp0(label, "Documents") == 0) return "☷";
  if (g_strcmp0(label, "Downloads") == 0) return "↓";
  if (g_strcmp0(label, "Pictures") == 0) return "◉";
  if (g_strcmp0(label, "Music") == 0) return "♪";
  if (g_strcmp0(label, "Videos") == 0) return "▶";
  if (g_strcmp0(label, "File System") == 0) return "/";
  return "•";
}

static void append_sidebar_item(GString *html, const char *label, const char *path) {
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR)) return;
  g_autofree gchar *uri = g_filename_to_uri(path, NULL, NULL);
  if (uri == NULL) return;
  g_autofree gchar *safe_label = html_escape(label);
  g_autofree gchar *safe_uri_attr = html_escape(uri);
  g_autofree gchar *safe_uri_js = js_escape(uri);
  g_string_append_printf(html,
    "<a class='side-row' href='%s' onclick=\"browse('%s');return false;\"><span class='side-icon'>%s</span><span>%s</span></a>",
    safe_uri_attr,
    safe_uri_js,
    sidebar_icon_for_label(label),
    safe_label
  );
}

static GList *load_directory_items(const char *path) {
  GList *items = NULL;
  GDir *dir = g_dir_open(path, 0, NULL);
  if (dir == NULL) return NULL;

  const gchar *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (name[0] == '.') continue;

    g_autofree gchar *child_path = g_build_filename(path, name, NULL);
    g_autofree gchar *child_uri = g_filename_to_uri(child_path, NULL, NULL);
    if (child_uri == NULL) continue;

    AstraFileItem *item = g_new0(AstraFileItem, 1);
    item->name = g_strdup(name);
    item->path = g_strdup(child_path);
    item->uri = g_strdup(child_uri);
    item->is_dir = g_file_test(child_path, G_FILE_TEST_IS_DIR);
    if (!item->is_dir) {
      item->content_type = g_content_type_guess(child_path, NULL, 0, NULL);
    }
    items = g_list_prepend(items, item);
  }

  g_dir_close(dir);
  return g_list_sort(items, file_item_compare);
}

gchar *astra_file_manager_build_page_html(const char *directory_uri) {
  g_autofree gchar *path = uri_to_path(directory_uri);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
    return g_strdup("<!doctype html><meta charset='utf-8'><title>Files</title><body>Not a readable directory.</body>");
  }

  g_autofree gchar *base_name = g_path_get_basename(path);
  g_autofree gchar *safe_title = html_escape(base_name);
  GList *items = load_directory_items(path);

  GString *html = g_string_new(NULL);
  g_string_append(html,
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Files</title>"
    "<style>"
    ":root{color-scheme:light dark;--bg:#f8f3ec;--surface:#fffdf8;--surface2:#f0e7db;--sidebar:#fffdf8;--content:#f8f3ec;--text:#201a14;--muted:#75695f;--accent:#d96f2c;--line:rgba(90,70,50,.16);--hover:rgba(217,111,44,.11);--active:rgba(217,111,44,.16);--shadow:rgba(20,10,0,.05)}"
    "@media(prefers-color-scheme:dark){:root{--bg:#11100e;--surface:#1a1815;--surface2:#24201b;--sidebar:#24201b;--content:#11100e;--text:#f4efe7;--muted:#b8aea2;--line:rgba(255,255,255,.10);--hover:rgba(217,111,44,.18);--active:rgba(217,111,44,.24);--shadow:rgba(0,0,0,.18)}}"
    "*{box-sizing:border-box}body{margin:0;background:var(--content);color:var(--text);font:14px system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
    ".shell{display:grid;grid-template-columns:248px 1fr;min-height:100vh}.sidebar{border-right:1px solid var(--line);background:var(--sidebar);padding:18px 12px;position:sticky;top:0;height:100vh;box-shadow:18px 0 40px var(--shadow);z-index:1}.brand{display:flex;align-items:center;gap:10px;font-weight:780;font-size:19px;margin:4px 10px 22px;letter-spacing:-.03em}.brand-mark{width:34px;height:34px;border-radius:13px;display:grid;place-items:center;background:var(--active);color:var(--accent);font-weight:800}.side-title{font-size:11px;text-transform:uppercase;letter-spacing:.11em;color:var(--muted);margin:20px 10px 8px}.side-row{width:100%;border:0;background:transparent;color:var(--text);text-align:left;border-radius:14px;padding:10px 12px;font:inherit;cursor:pointer;display:flex;align-items:center;gap:11px;text-decoration:none}.side-row:hover{background:var(--hover)}.side-icon{width:26px;height:26px;border-radius:10px;display:grid;place-items:center;background:var(--surface2);color:var(--accent);font-weight:700;flex:0 0 auto}.side-row:hover .side-icon{background:var(--active)}.side-note{margin:8px 10px 0;color:var(--muted);font-size:12px;line-height:1.45}"
    ".main{padding:30px;background:var(--content)}.main-head{margin-bottom:22px;display:flex;align-items:end;justify-content:space-between;gap:18px}.crumb-title{font-size:26px;font-weight:760;letter-spacing:-.035em}.crumb-subtitle{font-size:13px;color:var(--muted);margin-top:4px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(136px,1fr));gap:14px}.card{border:1px solid var(--line);background:var(--surface);border-radius:20px;min-height:132px;padding:14px 12px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;cursor:pointer;box-shadow:0 10px 30px var(--shadow);text-decoration:none;color:inherit}.card:hover,.card.context-active{background:var(--hover);transform:translateY(-1px)}.icon{font-size:38px;line-height:1}.name{max-width:100%;text-align:center;overflow:hidden;text-overflow:ellipsis;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}.meta{font-size:12px;color:var(--muted);max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.empty{border:1px dashed var(--line);border-radius:20px;padding:34px;color:var(--muted);background:var(--surface)}"
    "</style>"
    "<script>"
    "document.addEventListener('contextmenu', function(e){var item=e.target.closest('a[href]'); if(item){item.classList.add('context-active'); setTimeout(function(){item.classList.remove('context-active')},180)}});"
    "function send(kind,uri){if(window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.astraFiles){window.webkit.messageHandlers.astraFiles.postMessage(kind+'|'+uri)}}"
    "function browse(uri){send('browse',uri)} function openFile(uri){send('open',uri)}"
    "</script></head><body><div class='shell'><aside class='sidebar'><div class='brand'><span class='brand-mark'>A</span><span>Astra Files</span></div>"
  );

  g_string_append(html, "<div class='side-title'>Places</div>");
  append_sidebar_item(html, "Home", g_get_home_dir());
  append_sidebar_item(html, "Desktop", g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP));
  append_sidebar_item(html, "Documents", g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
  append_sidebar_item(html, "Downloads", g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
  append_sidebar_item(html, "Pictures", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES));
  append_sidebar_item(html, "Music", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC));
  append_sidebar_item(html, "Videos", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS));
  append_sidebar_item(html, "File System", "/");

  g_string_append(html, "<div class='side-title'>Behavior</div><p class='side-note'>Folders use the browser back and forward buttons. Files open with your default desktop apps.</p></aside><main class='main'>");

  g_string_append_printf(html,
    "<header class='main-head'><div><div class='crumb-title'>%s</div><div class='crumb-subtitle'>Icon view</div></div></header><section class='grid'>",
    safe_title
  );

  if (items == NULL) {
    g_string_append(html, "<div class='empty'>This folder is empty or cannot be read.</div>");
  }

  for (GList *l = items; l != NULL; l = l->next) {
    AstraFileItem *item = l->data;
    g_autofree gchar *safe_name = html_escape(item->name);
    g_autofree gchar *safe_uri = js_escape(item->uri);
    g_autofree gchar *safe_meta = html_escape(item->is_dir ? "Folder" : (item->content_type ? item->content_type : "File"));
    g_autofree gchar *safe_uri_attr = html_escape(item->uri);
    g_string_append_printf(html,
      "<a class='card' title='%s' href='%s' onclick=\"%s('%s');return false;\"><div class='icon'>%s</div><div class='name'>%s</div><div class='meta'>%s</div></a>",
      safe_name,
      safe_uri_attr,
      item->is_dir ? "browse" : "openFile",
      safe_uri,
      icon_for_item(item),
      safe_name,
      safe_meta
    );
  }

  g_list_free_full(items, (GDestroyNotify)file_item_free);
  g_string_append(html, "</section></main></div></body></html>");
  return g_string_free(html, FALSE);
}
