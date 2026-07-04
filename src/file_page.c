#include "file_page.h"

#include <string.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gchar *content_type;
    goffset size;
    gboolean directory;
    gboolean executable;
} FileItem;

#define MAX_RENDERED_ITEMS 1500

static gchar *clipboard_uri;
static gboolean clipboard_cut;


typedef struct {
    gchar *scheme;
    gchar *bg;
    gchar *panel;
    gchar *card;
    gchar *text;
    gchar *muted;
    gchar *accent;
    gchar *border;
    gchar *symbolic_filter;
} FilePageTheme;

static gboolean rgba_is_visible(const GdkRGBA *color) {
    return color && color->alpha > 0.01;
}

static gdouble rgba_luminance(const GdkRGBA *color) {
    gdouble r = color->red * color->alpha + (1.0 - color->alpha);
    gdouble g = color->green * color->alpha + (1.0 - color->alpha);
    gdouble b = color->blue * color->alpha + (1.0 - color->alpha);
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

static gchar *rgba_to_css_string(const GdkRGBA *color) {
    return g_strdup_printf("rgba(%d,%d,%d,%.3f)",
                           (gint)CLAMP(color->red * 255.0, 0.0, 255.0),
                           (gint)CLAMP(color->green * 255.0, 0.0, 255.0),
                           (gint)CLAMP(color->blue * 255.0, 0.0, 255.0),
                           CLAMP(color->alpha, 0.0, 1.0));
}

static GdkRGBA rgba_mix(GdkRGBA base, GdkRGBA overlay, gdouble amount) {
    amount = CLAMP(amount, 0.0, 1.0);
    GdkRGBA result;
    result.red = base.red * (1.0 - amount) + overlay.red * amount;
    result.green = base.green * (1.0 - amount) + overlay.green * amount;
    result.blue = base.blue * (1.0 - amount) + overlay.blue * amount;
    result.alpha = 1.0;
    return result;
}

static gboolean lookup_gtk_color(GtkStyleContext *context, const gchar *name,
                                 GdkRGBA *out) {
    return context && gtk_style_context_lookup_color(context, name, out) &&
           rgba_is_visible(out);
}

static void file_page_theme_clear(FilePageTheme *theme) {
    g_free(theme->scheme);
    g_free(theme->bg);
    g_free(theme->panel);
    g_free(theme->card);
    g_free(theme->text);
    g_free(theme->muted);
    g_free(theme->accent);
    g_free(theme->border);
    g_free(theme->symbolic_filter);
}

static void file_page_theme_init(FilePageTheme *theme, GtkWindow *parent) {
    memset(theme, 0, sizeof(*theme));

    GtkSettings *settings = gtk_settings_get_default();
    gboolean prefer_dark = FALSE;
    if (settings)
        g_object_get(settings, "gtk-application-prefer-dark-theme",
                     &prefer_dark, NULL);

    GdkRGBA bg = {0.96, 0.96, 0.98, 1.0};
    GdkRGBA panel = {0.91, 0.91, 0.95, 1.0};
    GdkRGBA card = {1.0, 1.0, 1.0, 1.0};
    GdkRGBA text = {0.12, 0.13, 0.14, 1.0};
    GdkRGBA accent = {0.21, 0.52, 0.89, 1.0};
    GdkRGBA border = {0.84, 0.84, 0.88, 1.0};

    if (prefer_dark) {
        bg = (GdkRGBA){0.12, 0.12, 0.14, 1.0};
        panel = (GdkRGBA){0.16, 0.16, 0.18, 1.0};
        card = (GdkRGBA){0.20, 0.20, 0.23, 1.0};
        text = (GdkRGBA){0.96, 0.96, 0.97, 1.0};
        border = (GdkRGBA){0.28, 0.28, 0.31, 1.0};
    }

    GtkStyleContext *context = parent
        ? gtk_widget_get_style_context(GTK_WIDGET(parent)) : NULL;
    GdkRGBA candidate;
    if (lookup_gtk_color(context, "theme_bg_color", &candidate))
        bg = candidate;
    if (lookup_gtk_color(context, "theme_base_color", &candidate))
        card = candidate;
    if (lookup_gtk_color(context, "theme_fg_color", &candidate))
        text = candidate;
    if (lookup_gtk_color(context, "theme_text_color", &candidate))
        text = candidate;
    if (lookup_gtk_color(context, "theme_unfocused_bg_color", &candidate))
        panel = candidate;
    if (lookup_gtk_color(context, "theme_selected_bg_color", &candidate))
        accent = candidate;
    if (lookup_gtk_color(context, "borders", &candidate))
        border = candidate;

    gboolean dark = rgba_luminance(&bg) < 0.45;
    if (!lookup_gtk_color(context, "theme_unfocused_bg_color", &candidate))
        panel = rgba_mix(bg, text, dark ? 0.08 : 0.06);
    if (!lookup_gtk_color(context, "theme_base_color", &candidate))
        card = rgba_mix(bg, text, dark ? 0.13 : 0.015);
    if (!lookup_gtk_color(context, "borders", &candidate))
        border = rgba_mix(bg, text, dark ? 0.20 : 0.16);
    GdkRGBA muted = rgba_mix(text, bg, dark ? 0.36 : 0.42);

    theme->scheme = g_strdup(dark ? "dark" : "light");
    theme->bg = rgba_to_css_string(&bg);
    theme->panel = rgba_to_css_string(&panel);
    theme->card = rgba_to_css_string(&card);
    theme->text = rgba_to_css_string(&text);
    theme->muted = rgba_to_css_string(&muted);
    theme->accent = rgba_to_css_string(&accent);
    theme->border = rgba_to_css_string(&border);
    theme->symbolic_filter = g_strdup(dark ? "invert(1)" : "none");
}

static void file_item_free(gpointer data) {
    FileItem *item = data;
    g_free(item->name);
    g_free(item->uri);
    g_free(item->content_type);
    g_free(item);
}

static gint compare_items(gconstpointer a, gconstpointer b) {
    const FileItem *left = *(FileItem *const *)a;
    const FileItem *right = *(FileItem *const *)b;
    if (left->directory != right->directory)
        return left->directory ? -1 : 1;
    return g_utf8_collate(left->name, right->name);
}

static gchar *item_icon_uri(const FileItem *item) {
    if (item->directory)
        return g_strdup("service://dev.avyos.system/theme/icon?name=folder&size=64&symbolic=false");
    if (item->executable)
        return g_strdup("service://dev.avyos.system/theme/icon?name=application-x-executable&size=64&symbolic=false");
    const gchar *content_type = item->content_type
        ? item->content_type : "application/octet-stream";
    gchar *encoded = g_uri_escape_string(content_type, NULL, TRUE);
    gchar *uri = g_strdup_printf(
        "service://dev.avyos.system/mime/icon?type=%s&size=64&symbolic=false",
        encoded);
    g_free(encoded);
    return uri;
}

static void show_error(GtkWindow *parent, const gchar *title,
                       const gchar *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
        "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                              "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean launch_open(const gchar *uri, GError **error) {
    gchar *path = g_filename_from_uri(uri, NULL, error);
    if (!path)
        return FALSE;
    gchar *argv[] = {"xdg-open", path, NULL};
    gboolean result = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, NULL, error);
    g_free(path);
    return result;
}

static gboolean launch_executable(const gchar *uri, GError **error) {
    gchar *path = g_filename_from_uri(uri, NULL, error);
    if (!path)
        return FALSE;
    GFile *file = g_file_new_for_path(path);
    GFileInfo *info = g_file_query_info(
        file, G_FILE_ATTRIBUTE_STANDARD_TYPE ","
              G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
        G_FILE_QUERY_INFO_NONE, NULL, error);
    gboolean allowed = info &&
        g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR &&
        g_file_info_get_attribute_boolean(info,
                                           G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
    if (!allowed && info)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "This file is not marked as executable.");
    gboolean result = FALSE;
    if (allowed) {
        gchar *directory = g_path_get_dirname(path);
        gchar *argv[] = {path, NULL};
        result = g_spawn_async(directory, argv, NULL, G_SPAWN_DEFAULT,
                               NULL, NULL, NULL, error);
        g_free(directory);
    }
    g_clear_object(&info);
    g_object_unref(file);
    g_free(path);
    return result;
}

static gboolean copy_recursive(GFile *source, GFile *destination,
                               GError **error) {
    GFileType type = g_file_query_file_type(source, G_FILE_QUERY_INFO_NONE, NULL);
    if (type != G_FILE_TYPE_DIRECTORY)
        return g_file_copy(source, destination, G_FILE_COPY_NONE, NULL,
                           NULL, NULL, error);

    if (!g_file_make_directory(destination, NULL, error))
        return FALSE;
    GFileEnumerator *enumerator = g_file_enumerate_children(
        source, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE,
        NULL, error);
    if (!enumerator)
        return FALSE;
    gboolean success = TRUE;
    GFileInfo *info;
    while (success && (info = g_file_enumerator_next_file(enumerator, NULL,
                                                           error))) {
        const gchar *name = g_file_info_get_name(info);
        GFile *source_child = g_file_get_child(source, name);
        GFile *destination_child = g_file_get_child(destination, name);
        success = copy_recursive(source_child, destination_child, error);
        g_object_unref(destination_child);
        g_object_unref(source_child);
        g_object_unref(info);
    }
    if (error && *error)
        success = FALSE;
    g_object_unref(enumerator);
    return success;
}

static gboolean valid_new_name(const gchar *name) {
    return name && *name && !strchr(name, '/') &&
           !g_str_equal(name, ".") && !g_str_equal(name, "..");
}

static gboolean open_with_dialog(const gchar *uri, GtkWindow *parent,
                                 GError **error) {
    GFile *file = g_file_new_for_uri(uri);
    GtkWidget *dialog = gtk_app_chooser_dialog_new(parent, GTK_DIALOG_MODAL,
                                                   file);
    gboolean success = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GAppInfo *app = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
        if (app) {
            GList files = {file, NULL, NULL};
            success = g_app_info_launch(app, &files, NULL, error);
            g_object_unref(app);
        }
    } else {
        success = TRUE;
    }
    gtk_widget_destroy(dialog);
    g_object_unref(file);
    return success;
}

static void append_place(GString *html, const gchar *icon, const gchar *label,
                         const gchar *path, const gchar *current_uri) {
    if (!path)
        return;
    gchar *uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri)
        return;
    gchar *safe_uri = g_markup_escape_text(uri, -1);
    gchar *encoded = g_uri_escape_string(uri, NULL, TRUE);
    gchar *href = g_strdup_printf(
        "service://dev.avyos.astra/navigate?uri=%s", encoded);
    gchar *safe_href = g_markup_escape_text(href, -1);
    gchar *safe_label = g_markup_escape_text(label, -1);
    gchar *safe_icon = g_markup_escape_text(icon, -1);
    gboolean active = g_strcmp0(uri, current_uri) == 0;
    g_string_append_printf(html,
        "<a class='place%s' href='%s'><img class='place-icon' "
        "src='service://dev.avyos.system/theme/icon?name=%s&amp;size=24&amp;symbolic=true' alt=''>%s</a>",
        active ? " active" : "", safe_href, safe_icon, safe_label);
    g_free(safe_icon);
    g_free(safe_label);
    g_free(safe_href);
    g_free(href);
    g_free(encoded);
    g_free(safe_uri);
    g_free(uri);
}

static gchar *build_directory_html(GFile *directory, const gchar *uri,
                                   GtkWindow *parent, GError **error) {
    GFileEnumerator *enumerator = g_file_enumerate_children(
        directory,
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
        G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!enumerator)
        return NULL;

    GPtrArray *items = g_ptr_array_new_with_free_func(file_item_free);
    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(enumerator, NULL, error))) {
        const gchar *file_name = g_file_info_get_name(info);
        if (file_name && file_name[0] != '.') {
            GFile *child = g_file_get_child(directory, file_name);
            FileItem *item = g_new0(FileItem, 1);
            item->name = g_strdup(g_file_info_get_display_name(info));
            item->uri = g_file_get_uri(child);
            item->content_type = g_strdup(g_file_info_get_content_type(info));
            item->size = g_file_info_get_size(info);
            item->directory =
                g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
            item->executable = g_file_info_get_attribute_boolean(
                info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
            g_ptr_array_add(items, item);
            g_object_unref(child);
            if (items->len >= MAX_RENDERED_ITEMS) {
                g_object_unref(info);
                break;
            }
        }
        g_object_unref(info);
    }
    g_object_unref(enumerator);
    if (error && *error) {
        g_ptr_array_unref(items);
        return NULL;
    }
    g_ptr_array_sort(items, compare_items);

    FilePageTheme theme;
    file_page_theme_init(&theme, parent);

    GString *html = g_string_new(
        "<!doctype html><html><head><meta charset='utf-8'>");
    g_string_append_printf(html,
        "<meta name='color-scheme' content='%s'>"
        "<meta name='theme-color' content='%s'><style>"
        ":root{font:14px system-ui,sans-serif;color-scheme:%s;"
        "--bg:%s;--panel:%s;--card:%s;--text:%s;--muted:%s;"
        "--accent:%s;--border:%s;--symbolic-filter:%s}",
        theme.scheme, theme.bg, theme.scheme, theme.bg, theme.panel,
        theme.card, theme.text, theme.muted, theme.accent, theme.border,
        theme.symbolic_filter);
    g_string_append(html,
        "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);"
        "height:100vh;overflow:hidden;user-select:none;-webkit-user-select:none}.layout{display:grid;"
        "grid-template-columns:210px 1fr;height:100vh}"
        "aside{background:var(--panel);border-right:1px solid var(--border);padding:18px 10px}"
        ".brand{font-size:18px;font-weight:700;padding:0 10px 18px}.place{display:flex;gap:11px;"
        "align-items:center;padding:9px 11px;border-radius:8px;color:inherit;text-decoration:none}"
        ".place:hover,.place.active{background:color-mix(in srgb,var(--accent) 18%,transparent)}"
        ".place-icon{width:20px;height:20px;object-fit:contain;filter:var(--symbolic-filter)}"
        "main{min-width:0;display:flex;flex-direction:column;position:relative}.grid{padding:20px 22px;"
        "display:grid;grid-template-columns:repeat(auto-fill,minmax(118px,1fr));gap:13px;"
        "align-content:start;overflow:auto;flex:1}.item{min-height:125px;background:var(--card);"
        "border:1px solid transparent;border-radius:12px;padding:15px 9px 10px;text-align:center;"
        "color:inherit;text-decoration:none;user-select:none;-webkit-user-select:none;"
        "-webkit-user-drag:none}.item:hover{border-color:var(--border);"
        "box-shadow:0 3px 12px #0001}.item.selected{border-color:var(--accent);"
        "background:color-mix(in srgb,var(--accent) 12%,var(--card))}.icon{font-size:43px;"
        "height:57px}.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.type{"
        "font-size:11px;color:var(--muted);margin-top:5px;overflow:hidden;text-overflow:ellipsis;"
        "white-space:nowrap}.icon{display:flex;align-items:center;justify-content:center}"
        ".icon img{width:52px;height:52px;object-fit:contain;-webkit-user-drag:none;pointer-events:none}"
        ".empty{color:var(--muted);padding:30px}.selection-box{display:none;position:fixed;z-index:8;"
        "pointer-events:none;border:1px solid var(--accent);background:color-mix(in srgb,var(--accent)"
        " 18%,transparent);border-radius:3px}"
        ".menu{display:none;position:fixed;"
        "z-index:10;min-width:170px;background:var(--card);border:1px solid var(--border);"
        "border-radius:10px;padding:6px;box-shadow:0 10px 35px #0005}.menu button{display:block;"
        "width:100%;border:0;background:none;color:inherit;text-align:left;padding:8px 12px;"
        "border-radius:6px}.menu button:hover{background:var(--panel)}.menu hr{border:0;"
        "border-top:1px solid var(--border)}.menu button:disabled{opacity:.45}.menu .danger{"
        "color:#c01c28}dialog{border:1px solid var(--border);border-radius:14px;"
        "background:var(--card);color:inherit;min-width:390px;padding:22px}dialog::backdrop{"
        "background:#0007}.props{line-height:1.8;word-break:break-all}.close{float:right}"
        "</style></head><body><div class='layout'><aside><div class='brand'>Astra Files</div>");
    file_page_theme_clear(&theme);

    append_place(html, "user-home", "Home", g_get_home_dir(), uri);
    append_place(html, "user-desktop", "Desktop",
                 g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP), uri);
    append_place(html, "folder-documents", "Documents",
                 g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS), uri);
    append_place(html, "folder-download", "Downloads",
                 g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD), uri);
    append_place(html, "drive-harddisk", "File System", "/", uri);
    gchar *safe_directory_uri = g_markup_escape_text(uri, -1);
    gchar *directory_name = g_file_get_basename(directory);
    gchar *safe_directory_name = g_markup_escape_text(
        directory_name && *directory_name ? directory_name : "File System", -1);
    g_string_append_printf(html,
        "</aside><main data-uri='%s' data-name='%s'><div class='grid'>",
        safe_directory_uri, safe_directory_name);
    g_free(safe_directory_name);
    g_free(directory_name);
    g_free(safe_directory_uri);

    if (!items->len)
        g_string_append(html, "<div class='empty'>This folder is empty.</div>");
    for (guint i = 0; i < items->len; i++) {
        FileItem *item = g_ptr_array_index(items, i);
        gchar *safe_name = g_markup_escape_text(item->name, -1);
        gchar *safe_uri = g_markup_escape_text(item->uri, -1);
        gchar *safe_type = g_markup_escape_text(
            item->directory ? "Folder" :
            (item->content_type ? item->content_type : "Unknown"), -1);
        gchar *encoded = g_uri_escape_string(item->uri, NULL, TRUE);
        gchar *href = g_strdup_printf(
            item->directory ? "service://dev.avyos.astra/navigate?uri=%s"
                            : "service://dev.avyos.astra/open?uri=%s",
            encoded);
        gchar *safe_href = g_markup_escape_text(href, -1);
        gchar *size = item->directory ? g_strdup("Folder") : g_format_size(item->size);
        gchar *safe_size = g_markup_escape_text(size, -1);
        gchar *icon_uri = item_icon_uri(item);
        gchar *safe_icon_uri = g_markup_escape_text(icon_uri, -1);
        g_string_append_printf(html,
            "<a class='item' href='%s' data-uri='%s' data-name='%s' data-type='%s' "
            "data-size='%s' data-dir='%d' data-exec='%d'><div class='icon'>"
            "<img src='%s' alt=''></div>"
            "<div class='name' title='%s'>%s</div><div class='type'>%s</div></a>",
            safe_href, safe_uri, safe_name, safe_type, safe_size,
            item->directory, item->executable, safe_icon_uri, safe_name,
            safe_name, safe_type);
        g_free(safe_icon_uri);
        g_free(icon_uri);
        g_free(safe_size);
        g_free(size);
        g_free(safe_href);
        g_free(href);
        g_free(encoded);
        g_free(safe_type);
        g_free(safe_uri);
        g_free(safe_name);
    }
    g_string_append_printf(html,
        "</div></main></div><div id='selectionBox' class='selection-box'></div>"
        "<div id='itemMenu' class='menu'>"
        "<button onclick=\"itemAction('open')\">Open</button>"
        "<button id='openWith' onclick=\"itemAction('open-with')\">Open With Application…</button>"
        "<button id='run' onclick=\"itemAction('run')\">Run</button><hr>"
        "<button onclick=\"itemAction('cut')\">Cut</button>"
        "<button onclick=\"itemAction('copy')\">Copy</button><hr>"
        "<button class='danger' onclick=\"itemAction('trash')\">Move to Trash</button>"
        "<button onclick=\"showProperties(current)\">Properties</button></div>"
        "<div id='backgroundMenu' class='menu'>"
        "<button onclick=\"createEntry('new-folder')\">New Folder</button>"
        "<button onclick=\"createEntry('new-file')\">New File</button><hr>"
        "<button onclick=\"directoryAction('refresh')\">Refresh</button>"
        "<button%s onclick=\"directoryAction('paste')\">Paste</button><hr>"
        "<button onclick=\"showProperties(directory)\">Properties</button></div>"
        "<dialog id='properties'><button class='close' onclick=\"document.getElementById('properties').close()\">✕</button>"
        "<h2 id='propName'></h2><div id='propBody' class='props'></div></dialog><script>",
        clipboard_uri ? "" : " disabled");
    g_string_append(html,
        "let current=null,itemMenu=document.getElementById('itemMenu'),"
        "backgroundMenu=document.getElementById('backgroundMenu'),root=document.querySelector('main'),"
        "selectionBox=document.getElementById('selectionBox'),dragging=false,dragMoved=false,"
        "suppressClick=false,startX=0,startY=0,baseSelection=new Set(),"
        "directory={dataset:{uri:root.dataset.uri,name:root.dataset.name,type:'Folder',size:'—'}};"
        "function hideMenus(){itemMenu.style.display='none';backgroundMenu.style.display='none'}"
        "function cards(){return [...document.querySelectorAll('.item')]}"
        "function clearSelection(){cards().forEach(x=>x.classList.remove('selected'));current=null}"
        "document.addEventListener('click',e=>{hideMenus();if(suppressClick){suppressClick=false;"
        "return}let item=e.target.closest('.item');if(item){e.preventDefault();if(e.ctrlKey||"
        "e.metaKey){item.classList.toggle('selected')}else{clearSelection();item.classList.add("
        "'selected')}current=item.classList.contains('selected')?item:null}else if(e.target.closest("
        "'main'))clearSelection()});document.addEventListener('dblclick',e=>{let item=e.target.closest("
        "'.item');if(item){e.preventDefault();location.href=item.href}});"
        "root.addEventListener('mousedown',e=>{if(e.button!==0||e.target.closest('.item')||"
        "e.target.closest('.menu'))return;e.preventDefault();hideMenus();dragging=true;dragMoved=false;"
        "startX=e.clientX;startY=e.clientY;baseSelection=new Set((e.ctrlKey||e.metaKey)?cards().filter("
        "x=>x.classList.contains('selected')):[]);if(!e.ctrlKey&&!e.metaKey)clearSelection();"
        "selectionBox.style.cssText='display:block;left:'+startX+'px;top:'+startY+'px;width:0;height:0'});"
    );
    g_string_append(html,
        "document.addEventListener('mousemove',e=>{if(!dragging)return;e.preventDefault();let left="
        "Math.min(startX,e.clientX),top=Math.min(startY,e.clientY),right=Math.max(startX,e.clientX),"
        "bottom=Math.max(startY,e.clientY);if(Math.abs(e.clientX-startX)>3||Math.abs(e.clientY-startY)"
        ">3)dragMoved=true;selectionBox.style.left=left+'px';selectionBox.style.top=top+'px';"
        "selectionBox.style.width=(right-left)+'px';selectionBox.style.height=(bottom-top)+'px';"
        "cards().forEach(item=>{let r=item.getBoundingClientRect(),hit=!(r.right<left||r.left>right||"
        "r.bottom<top||r.top>bottom);item.classList.toggle('selected',hit||baseSelection.has(item))})});"
        "document.addEventListener('mouseup',e=>{if(!dragging)return;dragging=false;selectionBox.style."
        "display='none';suppressClick=dragMoved;let selected=cards().filter(x=>x.classList.contains("
        "'selected'));current=selected.length?selected[selected.length-1]:null});"
        "document.addEventListener('dragstart',e=>{if(e.target.closest('main'))e.preventDefault()});"
        "document.addEventListener('contextmenu',e=>{let item=e.target.closest('.item');"
        "if(!e.target.closest('main'))return;e.preventDefault();hideMenus();let menu=backgroundMenu;"
        "if(item){current=item;if(!item.classList.contains('selected')){clearSelection();item.classList."
        "add('selected');current=item}document.getElementById('run').style.display=item.dataset.exec==='1'"
        "&&item.dataset.dir==='0'?'block':'none';document.getElementById('openWith').style.display="
        "item.dataset.dir==='0'?'block':'none';menu=itemMenu}else{current=null}menu.style.display='block';"
        "menu.style.left=Math.min(e.clientX,innerWidth-215)+'px';menu.style.top=Math.min(e.clientY,"
        "innerHeight-270)+'px'});function send(action,target,name){let url='service://dev.avyos.astra/'+action+"
        "'?uri='+encodeURIComponent(target);if(name)url+='&name='+encodeURIComponent(name);"
        "location.href=url}function itemAction(action){if(!current)return;if(action==='open'&&"
        "current.dataset.dir==='1')location.href=current.href;else send(action,current.dataset.uri)}"
        "function directoryAction(action){if(action==='refresh')location.href='service://dev.avyos.astra/navigate?uri='+"
        "encodeURIComponent(directory.dataset.uri);else send(action,directory.dataset.uri)}"
        "function createEntry(action){let kind=action==='new-folder'?'folder':'file';let name=prompt("
        "'Name for the new '+kind+':');if(name)send(action,directory.dataset.uri,name)}"
        "function showProperties(target){if(!target)return;document.getElementById('propName')"
        ".textContent=target.dataset.name;document.getElementById('propBody').textContent='Type: '+"
        "target.dataset.type+'\\nSize: '+target.dataset.size+'\\nLocation: '+target.dataset.uri;"
        "document.getElementById('propBody').style.whiteSpace='pre-wrap';"
        "document.getElementById('properties').showModal()}"
        "</script></body></html>");

    g_ptr_array_unref(items);
    return g_string_free(html, FALSE);
}

FilePageResult file_page_load_uri(WebKitWebView *view, const gchar *uri,
                                  GtkWindow *parent) {
    if (!uri || !g_str_has_prefix(uri, "file://"))
        return FILE_PAGE_NOT_HANDLED;
    GError *error = NULL;
    GFile *file = g_file_new_for_uri(uri);
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!info) {
        show_error(parent, "Could not open location", error->message);
        g_clear_error(&error);
        g_object_unref(file);
        return FILE_PAGE_ERROR;
    }
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY) {
        gboolean launched = launch_open(uri, &error);
        if (!launched) {
            show_error(parent, "Could not open file", error->message);
            g_clear_error(&error);
        }
        g_object_unref(info);
        g_object_unref(file);
        return launched ? FILE_PAGE_EXTERNAL : FILE_PAGE_ERROR;
    }
    g_object_unref(info);

    gchar *html = build_directory_html(file, uri, parent, &error);
    g_object_unref(file);
    if (!html) {
        show_error(parent, "Could not read folder", error->message);
        g_clear_error(&error);
        return FILE_PAGE_ERROR;
    }
    /*
     * Do not use the directory URI as the HTML base. WebKit emits a new
     * navigation-policy decision for that base, which our file:// interceptor
     * would handle again and recursively regenerate the page. Every link in
     * the generated document is absolute, so about:blank is the correct,
     * non-intercepted base here.
     */
    webkit_web_view_load_html(view, html, "about:blank");
    g_free(html);
    return FILE_PAGE_DIRECTORY;
}

gboolean file_page_handle_action(const gchar *uri, WebKitWebView *view,
                                 GtkWindow *parent) {
    const gchar *prefix = "service://dev.avyos.astra/";
    if (!uri || !g_str_has_prefix(uri, prefix))
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, &error);
    const gchar *path = parsed ? g_uri_get_path(parsed) : NULL;
    const gchar *action = path && path[0] == '/' ? path + 1 : NULL;
    const gchar *query = parsed ? g_uri_get_query(parsed) : NULL;
    GHashTable *params = query
        ? g_uri_parse_params(query, -1, "&", G_URI_PARAMS_NONE, &error)
        : NULL;
    const gchar *target = params ? g_hash_table_lookup(params, "uri") : NULL;
    const gchar *name = params ? g_hash_table_lookup(params, "name") : NULL;
    if (error || !action || !target || !g_str_has_prefix(target, "file://")) {
        g_clear_error(&error);
        if (params) g_hash_table_unref(params);
        if (parsed) g_uri_unref(parsed);
        return TRUE;
    }

    gboolean success = TRUE;
    gchar *refresh_uri = NULL;
    GFile *target_file = g_file_new_for_uri(target);
    if (g_str_equal(action, "open")) {
        success = launch_open(target, &error);
    } else if (g_str_equal(action, "open-with")) {
        success = open_with_dialog(target, parent, &error);
    } else if (g_str_equal(action, "run")) {
        success = launch_executable(target, &error);
    } else if (g_str_equal(action, "cut") || g_str_equal(action, "copy")) {
        g_free(clipboard_uri);
        clipboard_uri = g_strdup(target);
        clipboard_cut = g_str_equal(action, "cut");
        gchar *path = g_filename_from_uri(target, NULL, &error);
        if (path) {
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                   path, -1);
            g_free(path);
        } else {
            success = FALSE;
        }
        GFile *parent_file = g_file_get_parent(target_file);
        if (parent_file) {
            refresh_uri = g_file_get_uri(parent_file);
            g_object_unref(parent_file);
        }
    } else if (g_str_equal(action, "trash")) {
        gchar *basename = g_file_get_basename(target_file);
        GtkWidget *dialog = gtk_message_dialog_new(
            parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "Move “%s” to Trash?", basename);
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL,
                               "Move to Trash", GTK_RESPONSE_ACCEPT, NULL);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            success = g_file_trash(target_file, NULL, &error);
        gtk_widget_destroy(dialog);
        g_free(basename);
        GFile *parent_file = g_file_get_parent(target_file);
        if (parent_file) {
            refresh_uri = g_file_get_uri(parent_file);
            g_object_unref(parent_file);
        }
    } else if (g_str_equal(action, "new-folder") ||
               g_str_equal(action, "new-file")) {
        if (!valid_new_name(name)) {
            success = FALSE;
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                                "The name is empty or contains a slash.");
        } else {
            GFile *child = g_file_get_child(target_file, name);
            if (g_str_equal(action, "new-folder")) {
                success = g_file_make_directory(child, NULL, &error);
            } else {
                GFileOutputStream *stream = g_file_create(
                    child, G_FILE_CREATE_NONE, NULL, &error);
                success = stream != NULL;
                if (stream) {
                    g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, NULL);
                    g_object_unref(stream);
                }
            }
            g_object_unref(child);
        }
        refresh_uri = g_strdup(target);
    } else if (g_str_equal(action, "paste")) {
        if (!clipboard_uri) {
            success = FALSE;
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                "There is nothing to paste.");
        } else {
            GFile *source = g_file_new_for_uri(clipboard_uri);
            gchar *basename = g_file_get_basename(source);
            GFile *destination = g_file_get_child(target_file, basename);
            success = clipboard_cut
                ? g_file_move(source, destination, G_FILE_COPY_NONE, NULL,
                              NULL, NULL, &error)
                : copy_recursive(source, destination, &error);
            if (success && clipboard_cut) {
                g_clear_pointer(&clipboard_uri, g_free);
                clipboard_cut = FALSE;
            }
            g_object_unref(destination);
            g_free(basename);
            g_object_unref(source);
        }
        refresh_uri = g_strdup(target);
    }

    if (!success) {
        show_error(parent, "File action failed",
                   error ? error->message : "The operation failed.");
        g_clear_error(&error);
    } else if (refresh_uri) {
        file_page_load_uri(view, refresh_uri, parent);
    }
    g_free(refresh_uri);
    g_object_unref(target_file);
    g_hash_table_unref(params);
    g_uri_unref(parsed);
    return TRUE;
}

gchar *file_page_navigation_target(const gchar *uri) {
    if (!uri || !g_str_has_prefix(
            uri, "service://dev.avyos.astra/navigate?uri="))
        return NULL;
    const gchar *encoded = strstr(uri, "?uri=");
    if (!encoded)
        return NULL;
    gchar *target = g_uri_unescape_string(encoded + 5, NULL);
    if (!target || !g_str_has_prefix(target, "file://")) {
        g_free(target);
        return NULL;
    }
    return target;
}
