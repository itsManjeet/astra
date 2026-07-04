#include "browser.h"

#include "app.h"
#include "downloads.h"
#include "file_page.h"
#include "library.h"
#include "pwa.h"
#include "service_scheme.h"
#include "settings.h"

#include <string.h>

static void update_header(BrowserWindow *browser);
static void update_header_for_tab(BrowserWindow *browser, BrowserTab *tab,
                                  gboolean force_address);
static void tab_load_uri(BrowserTab *tab, const gchar *uri);
static BrowserTab *browser_tab_new_with_related_view(BrowserWindow *browser,
                                                     WebKitWebView *related_view,
                                                     gboolean switch_to);
static void request_header_color(BrowserWindow *browser, BrowserTab *tab);
static void reset_header_color(BrowserWindow *browser);
static void open_web_inspector(BrowserWindow *browser);
static const gchar *current_special_page_title(BrowserWindow *browser);
static const gchar *current_special_page_icon(BrowserWindow *browser);
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data);
static gboolean on_context_menu(WebKitWebView *view, WebKitContextMenu *menu,
                                GdkEvent *event, WebKitHitTestResult *hit_test,
                                gpointer data);
static void downloads_action(GtkButton *button, gpointer data);
static void enable_browser_drop_target(GtkWidget *widget, BrowserWindow *browser);
static void enable_tab_drop_target(GtkWidget *widget, BrowserTab *tab);


#define HEADER_COLOR_JS \
    "(function(){" \
    "function visible(c){" \
    "if(!c)return null;c=String(c).trim();" \
    "if(!c||c==='transparent')return null;" \
    "var m=c.match(/^rgba?\\(([^)]+)\\)$/i);" \
    "if(m){var p=m[1].split(/[\\s,\\/]+/).filter(Boolean);" \
    "if(p.length>=4&&parseFloat(p[3])<=0.05)return null;}" \
    "return c;}" \
    "function fromElement(el){" \
    "while(el&&el!==document){var c=visible(getComputedStyle(el).backgroundColor);" \
    "if(c)return c;el=el.parentElement;}return null;}" \
    "var meta=document.querySelector('meta[name=\\\"theme-color\\\"],meta[name=\\\"msapplication-TileColor\\\"]');" \
    "var c=visible(meta&&meta.getAttribute('content'));" \
    "if(c)return c;" \
    "var w=Math.max(1,innerWidth),h=Math.max(1,innerHeight);" \
    "var pts=[[8,8],[Math.floor(w/2),8],[8,Math.min(80,h-1)]];" \
    "for(var i=0;i<pts.length;i++){var el=document.elementFromPoint(pts[i][0],pts[i][1]);" \
    "c=fromElement(el);if(c)return c;}" \
    "var body=document.body, html=document.documentElement;" \
    "if(body){c=visible(getComputedStyle(body).backgroundColor);if(c)return c;}" \
    "if(html){c=visible(getComputedStyle(html).backgroundColor);if(c)return c;}" \
    "return null;" \
    "})()"

#define HEADER_COLOR_KEY "astra-header-color"
#define HEADER_COLOR_SERIAL_KEY "astra-header-color-serial"

static guint header_style_counter = 0;

typedef struct {
    GtkWidget *header;
    GtkCssProvider *provider;
    gchar *style_class;
    guint serial;
} HeaderColorRequest;

static gboolean parse_header_color(const gchar *css_color, GdkRGBA *rgba) {
    if (!css_color || !*css_color || !gdk_rgba_parse(rgba, css_color))
        return FALSE;
    return rgba->alpha > 0.05;
}

static gchar *rgba_to_css(const GdkRGBA *rgba) {
    return g_strdup_printf("rgba(%d,%d,%d,%.3f)",
                           (gint)CLAMP(rgba->red * 255.0, 0.0, 255.0),
                           (gint)CLAMP(rgba->green * 255.0, 0.0, 255.0),
                           (gint)CLAMP(rgba->blue * 255.0, 0.0, 255.0),
                           CLAMP(rgba->alpha, 0.0, 1.0));
}

static const gchar *contrast_text_for_color(const GdkRGBA *rgba) {
    gdouble r = rgba->red * rgba->alpha + (1.0 - rgba->alpha);
    gdouble g = rgba->green * rgba->alpha + (1.0 - rgba->alpha);
    gdouble b = rgba->blue * rgba->alpha + (1.0 - rgba->alpha);
    gdouble luminance = 0.299 * r + 0.587 * g + 0.114 * b;
    return luminance > 0.58 ? "#111111" : "#ffffff";
}

static void load_header_css(GtkCssProvider *provider, const gchar *css) {
    GError *error = NULL;
    gtk_css_provider_load_from_data(provider, css ? css : "", -1, &error);
    if (error)
        g_error_free(error);
}

static guint begin_header_color_update(BrowserWindow *browser) {
    browser->header_color_serial++;
    if (browser->header)
        g_object_set_data(G_OBJECT(browser->header), HEADER_COLOR_SERIAL_KEY,
                          GUINT_TO_POINTER(browser->header_color_serial));
    return browser->header_color_serial;
}

static void apply_header_color_css(GtkCssProvider *provider,
                                   const gchar *style_class,
                                   const gchar *css_color) {
    GdkRGBA rgba;
    if (!parse_header_color(css_color, &rgba)) {
        load_header_css(provider, "");
        return;
    }

    gchar *bg = rgba_to_css(&rgba);
    const gchar *fg = contrast_text_for_color(&rgba);

    /*
     * GtkEntry does not reliably inherit the headerbar foreground/background
     * in mixed light/dark setups. Without explicitly styling it, a dark GTK
     * theme can leave light address text on a light site-colored header, or a
     * light theme can leave dark text on a dark site-colored header. Keep the
     * address bar tied to the same contrast decision used for toolbar icons.
     */
    gboolean dark_text = g_strcmp0(fg, "#111111") == 0;
    const gchar *entry_bg = dark_text ? "rgba(0,0,0,0.08)"
                                      : "rgba(255,255,255,0.14)";
    const gchar *entry_bg_focus = dark_text ? "rgba(0,0,0,0.12)"
                                            : "rgba(255,255,255,0.18)";
    const gchar *entry_border = dark_text ? "rgba(0,0,0,0.18)"
                                          : "rgba(255,255,255,0.28)";
    const gchar *entry_focus_border = dark_text ? "rgba(0,0,0,0.32)"
                                                : "rgba(255,255,255,0.45)";
    const gchar *placeholder = dark_text ? "rgba(17,17,17,0.55)"
                                         : "rgba(255,255,255,0.62)";
    const gchar *selection_bg = dark_text ? "rgba(0,0,0,0.20)"
                                          : "rgba(255,255,255,0.30)";

    gchar *css = g_strdup_printf(
        ".%s {"
        "background-image: none;"
        "background-color: %s;"
        "color: %s;"
        "}"
        ".%s button, .%s button.flat, .%s label, .%s image {"
        "color: %s;"
        "}"
        ".%s entry {"
        "background-image: none;"
        "background-color: %s;"
        "border-color: %s;"
        "box-shadow: none;"
        "color: %s;"
        "text-shadow: none;"
        "}"
        ".%s entry:focus {"
        "background-color: %s;"
        "border-color: %s;"
        "box-shadow: 0 0 0 1px %s;"
        "}"
        ".%s entry image, .%s entry label {"
        "color: %s;"
        "}"
        ".%s entry placeholder {"
        "color: %s;"
        "}"
        ".%s entry selection {"
        "background-color: %s;"
        "color: %s;"
        "}"
        ".%s progressbar progress {"
        "background-color: %s;"
        "}",
        style_class, bg, fg,
        style_class, style_class, style_class, style_class, fg,
        style_class, entry_bg, entry_border, fg,
        style_class, entry_bg_focus, entry_focus_border, entry_focus_border,
        style_class, style_class, fg,
        style_class, placeholder,
        style_class, selection_bg, fg,
        style_class, fg);
    load_header_css(provider, css);
    g_free(css);
    g_free(bg);
}

static void reset_header_color(BrowserWindow *browser) {
    if (!browser->header_css)
        return;
    begin_header_color_update(browser);
    load_header_css(browser->header_css, "");
}

static void apply_header_color(BrowserWindow *browser, const gchar *css_color) {
    if (!browser->header_css || !browser->header_css_class)
        return;
    begin_header_color_update(browser);
    apply_header_color_css(browser->header_css, browser->header_css_class,
                           css_color);
}

static void header_color_request_free(HeaderColorRequest *request) {
    if (!request)
        return;
    g_clear_object(&request->header);
    g_clear_object(&request->provider);
    g_free(request->style_class);
    g_free(request);
}

static void header_color_javascript_finished(GObject *source,
                                             GAsyncResult *result,
                                             gpointer data) {
    HeaderColorRequest *request = data;
    WebKitWebView *view = WEBKIT_WEB_VIEW(source);
    GError *error = NULL;
    WebKitJavascriptResult *js_result =
        webkit_web_view_run_javascript_finish(view, result, &error);
    if (error) {
        g_error_free(error);
        header_color_request_free(request);
        return;
    }

    guint view_serial = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(view), HEADER_COLOR_SERIAL_KEY));
    guint header_serial = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(request->header), HEADER_COLOR_SERIAL_KEY));
    if (view_serial != request->serial || header_serial != request->serial) {
        if (js_result)
            webkit_javascript_result_unref(js_result);
        header_color_request_free(request);
        return;
    }

    gchar *color = NULL;
    if (js_result) {
        JSCValue *value = webkit_javascript_result_get_js_value(js_result);
        if (value && !jsc_value_is_null(value) && !jsc_value_is_undefined(value))
            color = jsc_value_to_string(value);
        webkit_javascript_result_unref(js_result);
    }

    GdkRGBA rgba;
    if (parse_header_color(color, &rgba)) {
        g_object_set_data_full(G_OBJECT(view), HEADER_COLOR_KEY,
                               g_strdup(color), g_free);
        apply_header_color_css(request->provider, request->style_class, color);
    } else {
        g_object_set_data(G_OBJECT(view), HEADER_COLOR_KEY, NULL);
        load_header_css(request->provider, "");
    }

    g_free(color);
    header_color_request_free(request);
}

static void request_header_color(BrowserWindow *browser, BrowserTab *tab) {
    if (!browser->header || !browser->header_css || !browser->header_css_class ||
        !tab || tab->file_uri) {
        reset_header_color(browser);
        return;
    }

    const gchar *uri = webkit_web_view_get_uri(tab->view);
    if (!uri || !(g_str_has_prefix(uri, "http://") ||
                  g_str_has_prefix(uri, "https://"))) {
        reset_header_color(browser);
        return;
    }

    const gchar *cached = g_object_get_data(G_OBJECT(tab->view), HEADER_COLOR_KEY);
    if (cached && *cached) {
        apply_header_color(browser, cached);
        return;
    }

    guint serial = begin_header_color_update(browser);
    g_object_set_data(G_OBJECT(tab->view), HEADER_COLOR_SERIAL_KEY,
                      GUINT_TO_POINTER(serial));

    HeaderColorRequest *request = g_new0(HeaderColorRequest, 1);
    request->header = g_object_ref(browser->header);
    request->provider = g_object_ref(browser->header_css);
    request->style_class = g_strdup(browser->header_css_class);
    request->serial = serial;
    webkit_web_view_run_javascript(tab->view, HEADER_COLOR_JS, NULL,
                                   header_color_javascript_finished, request);
}

static WebKitWebContext *app_get_web_context(App *app) {
    if (app->context)
        return g_object_ref(app->context);

    /*
     * Use one persistent website data manager for all normal windows and
     * installed web apps. Service workers, Cache Storage, IndexedDB,
     * localStorage, cookies and the HTTP cache all live under these base
     * directories, so PWAs that implement offline support can keep working
     * after Astra is closed and reopened.
     */
    WebKitWebsiteDataManager *manager = webkit_website_data_manager_new(
        "base-data-directory", app->data_dir,
        "base-cache-directory", app->cache_dir,
        NULL);

    WebKitCookieManager *cookies =
        webkit_website_data_manager_get_cookie_manager(manager);
    webkit_cookie_manager_set_accept_policy(cookies,
                                            WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    webkit_cookie_manager_set_persistent_storage(
        cookies, app->cookie_path, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    app->context = webkit_web_context_new_with_website_data_manager(manager);
    downloads_connect_context(app, app->context);
    webkit_web_context_set_cache_model(app->context,
                                       WEBKIT_CACHE_MODEL_WEB_BROWSER);

    gchar *favicons_dir = g_build_filename(app->cache_dir, "favicons", NULL);
    g_mkdir_with_parents(favicons_dir, 0700);
    webkit_web_context_set_favicon_database_directory(app->context, favicons_dir);
    g_free(favicons_dir);

    g_object_unref(manager);
    return g_object_ref(app->context);
}

static GtkWidget *icon_button(const gchar *icon, const gchar *tooltip) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");
    return button;
}

static void set_cairo_rgba(cairo_t *cr, const GdkRGBA *color, gdouble alpha) {
    cairo_set_source_rgba(cr, color->red, color->green, color->blue,
                          CLAMP(color->alpha * alpha, 0.0, 1.0));
}

static gboolean download_progress_draw(GtkWidget *area, cairo_t *cr,
                                       gpointer data) {
    BrowserWindow *browser = data;
    if (!browser || !browser->download_progress_active)
        return FALSE;

    gint width = gtk_widget_get_allocated_width(area);
    gint height = gtk_widget_get_allocated_height(area);
    gdouble size = MIN(width, height);
    if (size <= 4.0)
        return FALSE;

    GtkStyleContext *context = browser->header
        ? gtk_widget_get_style_context(browser->header)
        : gtk_widget_get_style_context(area);
    GdkRGBA color;
    gtk_style_context_get_color(context, gtk_widget_get_state_flags(area), &color);

    gdouble cx = width / 2.0;
    gdouble cy = height / 2.0;
    gdouble radius = size / 2.0 - 2.5;
    gdouble fraction = CLAMP(browser->download_progress_fraction, 0.0, 1.0);
    gboolean determinate = fraction > 0.005;

    cairo_save(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    set_cairo_rgba(cr, &color, 0.22);
    cairo_arc(cr, cx, cy, radius, 0, 2.0 * G_PI);
    cairo_stroke(cr);

    set_cairo_rgba(cr, &color, 0.96);
    if (determinate) {
        gdouble start = -G_PI / 2.0;
        gdouble end = start + fraction * 2.0 * G_PI;
        cairo_arc(cr, cx, cy, radius, start, end);
    } else {
        /* Unknown length: draw a stable short ring segment. No timeout is used. */
        gdouble start = -G_PI / 2.0;
        cairo_arc(cr, cx, cy, radius, start, start + 0.65 * G_PI);
    }
    cairo_stroke(cr);

    cairo_restore(cr);
    return FALSE;
}

static gboolean hide_download_popover_cb(gpointer data) {
    BrowserWindow *browser = data;
    if (!browser)
        return G_SOURCE_REMOVE;

    browser->download_popover_hide_id = 0;
    browser->download_button_hold = FALSE;

    if (browser->download_popover)
        gtk_popover_popdown(GTK_POPOVER(browser->download_popover));
    browser_update_download_indicator(browser);
    return G_SOURCE_REMOVE;
}

static void schedule_download_popover_hide(BrowserWindow *browser, guint timeout_ms) {
    if (!browser)
        return;
    if (browser->download_popover_hide_id) {
        g_source_remove(browser->download_popover_hide_id);
        browser->download_popover_hide_id = 0;
    }
    browser->download_popover_hide_id =
        g_timeout_add(timeout_ms, hide_download_popover_cb, browser);
}

static void build_download_popover(BrowserWindow *browser, GtkWidget *button) {
    browser->download_popover = gtk_popover_new(button);
    gtk_popover_set_position(GTK_POPOVER(browser->download_popover),
                             GTK_POS_BOTTOM);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_size_request(box, 260, -1);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    browser->download_popover_icon =
        gtk_image_new_from_icon_name("folder-download-symbolic",
                                     GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(row), browser->download_popover_icon,
                       FALSE, FALSE, 0);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    browser->download_popover_title = gtk_label_new("Download");
    gtk_widget_set_halign(browser->download_popover_title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(browser->download_popover_title),
                            PANGO_ELLIPSIZE_END);

    browser->download_popover_subtitle = gtk_label_new(NULL);
    gtk_widget_set_halign(browser->download_popover_subtitle, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(browser->download_popover_subtitle),
                            PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(browser->download_popover_subtitle),
        "dim-label");

    gtk_box_pack_start(GTK_BOX(text), browser->download_popover_title,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), browser->download_popover_subtitle,
                       FALSE, FALSE, 0);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    browser->download_popover_progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), browser->download_popover_progress,
                       FALSE, FALSE, 0);

    GtkWidget *open = gtk_button_new_with_label("Open downloads");
    gtk_widget_set_halign(open, GTK_ALIGN_END);
    g_signal_connect(open, "clicked", G_CALLBACK(downloads_action), browser);
    gtk_box_pack_start(GTK_BOX(box), open, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(browser->download_popover), box);
    gtk_widget_show_all(box);
}

static GtkWidget *download_indicator_button(BrowserWindow *browser) {
    GtkWidget *button = gtk_button_new();
    gtk_widget_set_tooltip_text(button, "Downloads");
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");

    GtkWidget *overlay = gtk_overlay_new();
    browser->download_icon = gtk_image_new_from_icon_name("folder-download-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(overlay), browser->download_icon);

    browser->download_progress = gtk_drawing_area_new();
    gtk_widget_set_no_show_all(browser->download_progress, TRUE);
    gtk_widget_set_size_request(browser->download_progress, 22, 22);
    gtk_widget_set_halign(browser->download_progress, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(browser->download_progress, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), browser->download_progress);
    g_signal_connect(browser->download_progress, "draw",
                     G_CALLBACK(download_progress_draw), browser);

    gtk_container_add(GTK_CONTAINER(button), overlay);
    build_download_popover(browser, button);

    /* The headerbar indicator is only for active downloads or for a short
     * app-mode notification after completion/failure. Keep it out of
     * gtk_widget_show_all() so it does not appear on a fresh browser window. */
    gtk_widget_show_all(button);
    gtk_widget_set_no_show_all(button, TRUE);
    gtk_widget_hide(button);

    return button;
}

void browser_update_download_indicator(BrowserWindow *browser) {
    if (!browser || !browser->download_button || !browser->download_progress)
        return;

    DownloadStatus status;
    downloads_get_status(browser->app, &status);

    browser->download_progress_fraction = status.progress;
    browser->download_progress_active = status.active;

    gboolean show_button = status.active || browser->download_button_hold;
    gtk_widget_set_visible(browser->download_button, show_button);
    gtk_widget_set_visible(browser->download_progress, status.active);

    if (status.active) {
        gchar *tooltip = g_strdup_printf("%u active download%s — %.0f%%",
                                         status.active_count,
                                         status.active_count == 1 ? "" : "s",
                                         status.progress * 100.0);
        gtk_image_set_from_icon_name(GTK_IMAGE(browser->download_icon),
                                     "folder-download-symbolic",
                                     GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(browser->download_button, tooltip);
        g_free(tooltip);
        gtk_widget_queue_draw(browser->download_progress);
    } else if (!browser->download_button_hold) {
        gtk_widget_set_tooltip_text(browser->download_button, "Downloads");
    }
}

void browser_show_download_notification(BrowserWindow *browser,
                                        const gchar *title,
                                        const gchar *detail,
                                        gdouble progress,
                                        gboolean failed) {
    if (!browser || !browser->app_mode || !browser->download_button ||
        !browser->download_popover)
        return;

    browser->download_button_hold = TRUE;
    gtk_widget_set_visible(browser->download_button, TRUE);

    const gchar *icon = failed ? "dialog-warning-symbolic" :
        progress >= 0.999 ? "emblem-ok-symbolic" : "folder-download-symbolic";
    gtk_image_set_from_icon_name(GTK_IMAGE(browser->download_icon), icon,
                                 GTK_ICON_SIZE_BUTTON);
    gtk_image_set_from_icon_name(GTK_IMAGE(browser->download_popover_icon), icon,
                                 GTK_ICON_SIZE_DIALOG);

    gtk_label_set_text(GTK_LABEL(browser->download_popover_title),
                       title && *title ? title : "Download");
    gtk_label_set_text(GTK_LABEL(browser->download_popover_subtitle),
                       detail && *detail ? detail : "Download item");

    gboolean active = progress > 0.0 && progress < 0.999 && !failed;
    gtk_widget_set_visible(browser->download_popover_progress, active);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(browser->download_popover_progress),
                                  CLAMP(progress, 0.0, 1.0));

    gtk_widget_set_tooltip_text(browser->download_button,
                                title && *title ? title : "Downloads");
    gtk_popover_popup(GTK_POPOVER(browser->download_popover));

    schedule_download_popover_hide(browser, active ? 2500 : 6500);
}

static BrowserTab *current_tab(BrowserWindow *browser) {
    gint index = gtk_notebook_get_current_page(GTK_NOTEBOOK(browser->notebook));
    if (index < 0)
        return NULL;
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), index);
    return g_object_get_data(G_OBJECT(page), "browser-tab");
}

static GtkWidget *current_page_widget(BrowserWindow *browser) {
    gint index = gtk_notebook_get_current_page(GTK_NOTEBOOK(browser->notebook));
    if (index < 0)
        return NULL;
    return gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), index);
}

static const gchar *current_special_page_title(BrowserWindow *browser) {
    GtkWidget *page = current_page_widget(browser);
    if (!page)
        return "Page";
    const gchar *title = g_object_get_data(G_OBJECT(page), "astra-page-title");
    if (title && *title)
        return title;
    if (g_object_get_data(G_OBJECT(page), "settings-page"))
        return "Settings";
    return "Page";
}

static const gchar *current_special_page_icon(BrowserWindow *browser) {
    GtkWidget *page = current_page_widget(browser);
    if (!page)
        return "text-html-symbolic";
    const gchar *icon = g_object_get_data(G_OBJECT(page), "astra-page-icon");
    if (icon && *icon)
        return icon;
    if (g_object_get_data(G_OBJECT(page), "settings-page"))
        return "preferences-system-symbolic";
    return "text-html-symbolic";
}

static const gchar *tab_display_uri(BrowserTab *tab) {
    return tab->file_uri ? tab->file_uri : webkit_web_view_get_uri(tab->view);
}

static void apply_settings_to_view(App *app, WebKitWebView *view) {
    AppConfig *c = &app->config;
    WebKitSettings *settings = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_javascript(settings, c->javascript);
    webkit_settings_set_auto_load_images(settings, c->images);
    webkit_settings_set_enable_webgl(settings, c->webgl);
    webkit_settings_set_enable_media_stream(settings, c->media_stream);
    webkit_settings_set_media_playback_requires_user_gesture(settings, !c->autoplay);
    webkit_settings_set_enable_developer_extras(settings, c->developer_tools);
    webkit_settings_set_enable_smooth_scrolling(settings, c->smooth_scrolling);
    webkit_settings_set_zoom_text_only(settings, c->zoom_text_only);

    /*
     * PWA/offline foundations. These do not make every website work offline by
     * themselves; the site still needs to register a service worker or use web
     * storage correctly. AppCache is obsolete, so do not call
     * webkit_settings_set_enable_offline_web_application_cache(): recent
     * WebKitGTK warns that it is deprecated and does nothing.
     */
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    webkit_settings_set_enable_html5_database(settings, TRUE);
    webkit_settings_set_enable_page_cache(settings, TRUE);

    webkit_settings_set_user_agent(settings, *c->user_agent ? c->user_agent : NULL);
}

void browser_apply_settings(App *app) {
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", app->config.dark_ui, NULL);
    for (GList *item = app->windows; item; item = item->next) {
        BrowserWindow *browser = item->data;
        gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
        for (gint i = 0; i < count; i++) {
            GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), i);
            BrowserTab *tab = g_object_get_data(G_OBJECT(page), "browser-tab");
            if (tab) {
                apply_settings_to_view(app, tab->view);
                if (tab->file_uri) {
                    file_page_load_uri(tab->view, tab->file_uri,
                                       GTK_WINDOW(browser->window));
                }
            }
        }
        update_header(browser);
    }
}

void browser_apply_zoom(App *app) {
    for (GList *item = app->windows; item; item = item->next) {
        BrowserWindow *browser = item->data;
        gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
        for (gint i = 0; i < count; i++) {
            GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), i);
            BrowserTab *tab = g_object_get_data(G_OBJECT(page), "browser-tab");
            if (tab)
                webkit_web_view_set_zoom_level(tab->view, app->config.default_zoom);
        }
    }
}

void browser_update_tab_visibility(BrowserWindow *browser) {
    gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(browser->notebook),
                               !browser->app_mode && count > 1);
}

static gchar *text_to_uri(App *app, const gchar *text) {
    gchar *trimmed = g_strstrip(g_strdup(text));
    if (!*trimmed) {
        g_free(trimmed);
        return g_strdup(app->config.homepage);
    }
    if (g_uri_parse_scheme(trimmed) || g_str_has_prefix(trimmed, "about:"))
        return trimmed;
    if (!strchr(trimmed, ' ') &&
        (strchr(trimmed, '.') || g_str_has_prefix(trimmed, "localhost"))) {
        gchar *uri = g_strdup_printf("https://%s", trimmed);
        g_free(trimmed);
        return uri;
    }

    gchar *escaped = g_uri_escape_string(trimmed, NULL, TRUE);
    const gchar *slot = strstr(app->config.search_url, "%s");
    gchar *uri;
    if (slot) {
        gchar *prefix = g_strndup(app->config.search_url,
                                  slot - app->config.search_url);
        uri = g_strconcat(prefix, escaped, slot + 2, NULL);
        g_free(prefix);
    } else {
        uri = g_strconcat(app->config.search_url, escaped, NULL);
    }
    g_free(escaped);
    g_free(trimmed);
    return uri;
}

static void load_address(BrowserWindow *browser) {
    BrowserTab *tab = current_tab(browser);
    if (!tab)
        return;
    gchar *uri = text_to_uri(browser->app,
                             gtk_entry_get_text(GTK_ENTRY(browser->address)));
    tab_load_uri(tab, uri);
    g_free(uri);
}

static void on_address_activate(GtkEntry *entry, gpointer data) {
    (void)entry;
    load_address(data);
}


static void on_go(GtkButton *button, gpointer data) {
    (void)button;
    load_address(data);
}

typedef enum {
    DROP_TARGET_URI_LIST,
    DROP_TARGET_TEXT,
    DROP_TARGET_NETSCAPE_URL,
    DROP_TARGET_MOZ_URL,
    DROP_TARGET_GNOME_COPIED_FILES,
} DropTargetType;

static const GtkTargetEntry drop_targets[] = {
    { "text/uri-list", 0, DROP_TARGET_URI_LIST },
    { "x-special/gnome-copied-files", 0, DROP_TARGET_GNOME_COPIED_FILES },
    { "_NETSCAPE_URL", 0, DROP_TARGET_NETSCAPE_URL },
    { "text/x-moz-url", 0, DROP_TARGET_MOZ_URL },
    { "text/x-moz-url-data", 0, DROP_TARGET_MOZ_URL },
    { "text/plain", 0, DROP_TARGET_TEXT },
    { "UTF8_STRING", 0, DROP_TARGET_TEXT },
    { "TEXT", 0, DROP_TARGET_TEXT },
    { "STRING", 0, DROP_TARGET_TEXT },
};

static gboolean drag_context_has_target(GdkDragContext *context,
                                        const gchar *target_name) {
    GList *targets = context ? gdk_drag_context_list_targets(context) : NULL;
    if (!targets)
        return FALSE;

    GdkAtom atom = gdk_atom_intern_static_string(target_name);
    return g_list_find(targets, atom) != NULL;
}

static GdkAtom choose_drop_target(GdkDragContext *context) {
    static const gchar *preferred_targets[] = {
        "text/uri-list",
        "x-special/gnome-copied-files",
        "_NETSCAPE_URL",
        "text/x-moz-url",
        "text/x-moz-url-data",
        "text/plain",
        "UTF8_STRING",
        "TEXT",
        "STRING",
    };

    for (guint i = 0; i < G_N_ELEMENTS(preferred_targets); i++) {
        if (drag_context_has_target(context, preferred_targets[i]))
            return gdk_atom_intern_static_string(preferred_targets[i]);
    }

    return GDK_NONE;
}

static gchar *first_non_empty_line(const gchar *text) {
    if (!text)
        return NULL;

    gchar **lines = g_strsplit(text, "\n", -1);
    gchar *result = NULL;
    for (guint i = 0; lines && lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (*line) {
            result = g_strdup(line);
            break;
        }
    }
    g_strfreev(lines);
    return result;
}

static gchar *selection_text_from_data(GtkSelectionData *selection) {
    gchar *text = (gchar *)gtk_selection_data_get_text(selection);
    if (text)
        return text;

    const guchar *raw = gtk_selection_data_get_data(selection);
    gint length = gtk_selection_data_get_length(selection);
    if (!raw || length <= 0)
        return NULL;

    return g_strndup((const gchar *)raw, length);
}

static gchar *first_uri_from_text_list(const gchar *text,
                                       gboolean skip_file_manager_action) {
    if (!text)
        return NULL;

    gchar **lines = g_strsplit(text, "\n", -1);
    gchar *result = NULL;

    for (guint i = 0; lines && lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!*line || *line == '#')
            continue;

        if (skip_file_manager_action &&
            (g_str_equal(line, "copy") || g_str_equal(line, "cut") ||
             g_str_equal(line, "link") || g_str_equal(line, "move")))
            continue;

        result = g_strdup(line);
        break;
    }

    g_strfreev(lines);
    return result;
}

static gchar *local_path_to_uri_if_exists(const gchar *text) {
    if (!text || !*text)
        return NULL;
    if (!(g_path_is_absolute(text) || g_str_has_prefix(text, "~/") ||
          g_str_has_prefix(text, "./") || g_str_has_prefix(text, "../")))
        return NULL;

    GFile *file = g_file_new_for_commandline_arg(text);
    if (!g_file_query_exists(file, NULL)) {
        g_object_unref(file);
        return NULL;
    }

    gchar *uri = g_file_get_uri(file);
    g_object_unref(file);
    return uri;
}

static gchar *drop_data_to_uri(App *app, GtkSelectionData *selection,
                               guint info) {
    gchar *uri = NULL;

    if (info == DROP_TARGET_URI_LIST) {
        gchar **uris = gtk_selection_data_get_uris(selection);
        for (guint i = 0; uris && uris[i]; i++) {
            gchar *candidate = g_strstrip(g_strdup(uris[i]));
            if (*candidate && *candidate != '#') {
                uri = candidate;
                break;
            }
            g_free(candidate);
        }
        g_strfreev(uris);
        if (uri)
            return uri;
    }

    gchar *text = selection_text_from_data(selection);
    gchar *line = NULL;

    if (info == DROP_TARGET_GNOME_COPIED_FILES) {
        /* Nautilus/Nemo/Thunar commonly offer this target as:
         *   copy\nfile:///path/to/file
         * The previous implementation interpreted the first line, "copy",
         * as search text, so file-manager drops appeared to do nothing useful.
         */
        line = first_uri_from_text_list(text, TRUE);
    } else {
        /* _NETSCAPE_URL and text/x-moz-url store "URL\nTitle". For plain
         * text, using the first non-empty line also makes drops from
         * terminals/editors predictable. */
        line = first_non_empty_line(text);
    }

    g_free(text);
    if (!line)
        return NULL;

    uri = local_path_to_uri_if_exists(line);
    if (!uri)
        uri = text_to_uri(app, line);
    g_free(line);
    return uri;
}

static gboolean load_dropped_uri_in_tab(BrowserTab *tab, const gchar *uri) {
    if (!tab || !uri || !*uri)
        return FALSE;

    tab_load_uri(tab, uri);
    return TRUE;
}

static void finish_drop(GdkDragContext *context, guint time,
                        gboolean success) {
    gtk_drag_finish(context, success, FALSE, time);
}

static gboolean on_drop_motion(GtkWidget *widget, GdkDragContext *context,
                               gint x, gint y, guint time, gpointer data) {
    (void)widget;
    (void)x;
    (void)y;
    (void)data;

    GdkAtom target = choose_drop_target(context);
    if (target == GDK_NONE)
        return FALSE;

    gdk_drag_status(context, GDK_ACTION_COPY, time);
    return TRUE;
}

static gboolean on_drop_request_data(GtkWidget *widget, GdkDragContext *context,
                                     gint x, gint y, guint time,
                                     gpointer data) {
    (void)x;
    (void)y;
    (void)data;

    GdkAtom target = choose_drop_target(context);
    if (target == GDK_NONE) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return FALSE;
    }

    gtk_drag_get_data(widget, context, target, time);
    return TRUE;
}

static void on_browser_drag_data_received(GtkWidget *widget,
                                          GdkDragContext *context,
                                          gint x,
                                          gint y,
                                          GtkSelectionData *selection,
                                          guint info,
                                          guint time,
                                          gpointer data) {
    (void)widget;
    (void)x;
    (void)y;
    BrowserWindow *browser = data;
    BrowserTab *tab = browser ? current_tab(browser) : NULL;
    gchar *uri = browser ? drop_data_to_uri(browser->app, selection, info) : NULL;
    gboolean success = load_dropped_uri_in_tab(tab, uri);
    g_free(uri);
    finish_drop(context, time, success);
}

static void on_tab_drag_data_received(GtkWidget *widget,
                                      GdkDragContext *context,
                                      gint x,
                                      gint y,
                                      GtkSelectionData *selection,
                                      guint info,
                                      guint time,
                                      gpointer data) {
    (void)widget;
    (void)x;
    (void)y;
    BrowserTab *tab = data;
    gchar *uri = tab && tab->browser
        ? drop_data_to_uri(tab->browser->app, selection, info)
        : NULL;
    gboolean success = load_dropped_uri_in_tab(tab, uri);
    if (success)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tab->browser->notebook),
                                      gtk_notebook_page_num(
                                          GTK_NOTEBOOK(tab->browser->notebook),
                                          tab->page));
    g_free(uri);
    finish_drop(context, time, success);
}

static void enable_browser_drop_target(GtkWidget *widget, BrowserWindow *browser) {
    if (!widget)
        return;

    gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_HIGHLIGHT,
                      drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY);
    gtk_drag_dest_set_track_motion(widget, TRUE);
    g_signal_connect(widget, "drag-motion",
                     G_CALLBACK(on_drop_motion), browser);
    g_signal_connect(widget, "drag-drop",
                     G_CALLBACK(on_drop_request_data), browser);
    g_signal_connect(widget, "drag-data-received",
                     G_CALLBACK(on_browser_drag_data_received), browser);
}

static void enable_tab_drop_target(GtkWidget *widget, BrowserTab *tab) {
    if (!widget)
        return;

    gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_HIGHLIGHT,
                      drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY);
    gtk_drag_dest_set_track_motion(widget, TRUE);
    g_signal_connect(widget, "drag-motion",
                     G_CALLBACK(on_drop_motion), tab);
    g_signal_connect(widget, "drag-drop",
                     G_CALLBACK(on_drop_request_data), tab);
    g_signal_connect(widget, "drag-data-received",
                     G_CALLBACK(on_tab_drag_data_received), tab);
}

static void on_back(GtkButton *button, gpointer data) {
    (void)button;
    BrowserTab *tab = current_tab(data);
    if (!tab)
        return;
    if (tab->file_uri && tab->file_history_index > 0) {
        tab->file_history_index--;
        tab->file_history_navigation = TRUE;
        tab_load_uri(tab, g_ptr_array_index(tab->file_history,
                                            tab->file_history_index));
    } else if (!tab->file_uri) {
        webkit_web_view_go_back(tab->view);
    }
}

static void on_forward(GtkButton *button, gpointer data) {
    (void)button;
    BrowserTab *tab = current_tab(data);
    if (!tab)
        return;
    if (tab->file_uri && tab->file_history_index + 1 <
                             (gint)tab->file_history->len) {
        tab->file_history_index++;
        tab->file_history_navigation = TRUE;
        tab_load_uri(tab, g_ptr_array_index(tab->file_history,
                                            tab->file_history_index));
    } else if (!tab->file_uri) {
        webkit_web_view_go_forward(tab->view);
    }
}

static void on_reload(GtkButton *button, gpointer data) {
    (void)button;
    BrowserTab *tab = current_tab(data);
    if (!tab)
        return;
    if (tab->file_uri)
        tab_load_uri(tab, tab->file_uri);
    else if (webkit_web_view_is_loading(tab->view))
        webkit_web_view_stop_loading(tab->view);
    else
        webkit_web_view_reload(tab->view);
}

static void on_home(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *browser = data;
    BrowserTab *tab = current_tab(browser);
    if (tab)
        tab_load_uri(tab, browser->app->config.homepage);
}

static void history_add(BrowserWindow *browser, const gchar *uri) {
    App *app = browser->app;
    if (browser->is_private || !uri || !g_str_has_prefix(uri, "http") ||
        app_array_contains(app->history, uri))
        return;
    g_ptr_array_add(app->history, g_strdup(uri));
    GtkTreeIter iter;
    gtk_list_store_append(app->completion_store, &iter);
    gtk_list_store_set(app->completion_store, &iter, 0, uri, -1);
    while (app->history->len > 250)
        g_ptr_array_remove_index(app->history, 0);
    app_save(app);
}

static void set_favicon(GtkImage *image, cairo_surface_t *surface, gint size) {
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ||
        cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
        gtk_image_set_from_icon_name(image, "text-html-symbolic", GTK_ICON_SIZE_MENU);
        return;
    }

    cairo_surface_flush(surface);
    gint width = cairo_image_surface_get_width(surface);
    gint height = cairo_image_surface_get_height(surface);
    if (width <= 0 || height <= 0) {
        gtk_image_set_from_icon_name(image, "text-html-symbolic", GTK_ICON_SIZE_MENU);
        return;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
    if (!pixbuf) {
        gtk_image_set_from_icon_name(image, "text-html-symbolic", GTK_ICON_SIZE_MENU);
        return;
    }

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, size, size,
                                                GDK_INTERP_BILINEAR);
    if (scaled) {
        gtk_image_set_from_pixbuf(image, scaled);
        g_object_unref(scaled);
    } else {
        gtk_image_set_from_icon_name(image, "text-html-symbolic", GTK_ICON_SIZE_MENU);
    }
    g_object_unref(pixbuf);
}

static void update_favicon(BrowserTab *tab) {
    cairo_surface_t *surface = webkit_web_view_get_favicon(tab->view);
    set_favicon(GTK_IMAGE(tab->tab_icon), surface, 16);
    if (current_tab(tab->browser) == tab && tab->browser->favicon)
        set_favicon(GTK_IMAGE(tab->browser->favicon), surface, 18);
}

static void on_favicon_changed(WebKitWebView *view, GParamSpec *pspec,
                               gpointer data) {
    (void)view;
    (void)pspec;
    BrowserTab *tab = data;
    if (!tab->file_uri)
        update_favicon(tab);
}

static void update_bookmark_icon_for_tab(BrowserWindow *browser,
                                         BrowserTab *tab) {
    const gchar *uri = tab ? tab_display_uri(tab) : NULL;
    const gchar *icon = uri && app_array_contains(browser->app->bookmarks, uri)
                            ? "starred-symbolic"
                            : "non-starred-symbolic";
    gtk_button_set_image(GTK_BUTTON(browser->bookmark),
        gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
}

static void update_bookmark_icon(BrowserWindow *browser) {
    update_bookmark_icon_for_tab(browser, current_tab(browser));
}

static void on_bookmark(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *browser = data;
    BrowserTab *tab = current_tab(browser);
    const gchar *uri = tab ? tab_display_uri(tab) : NULL;
    if (!uri || !*uri)
        return;
    for (guint i = 0; i < browser->app->bookmarks->len; i++) {
        if (g_strcmp0(g_ptr_array_index(browser->app->bookmarks, i), uri) == 0) {
            g_ptr_array_remove_index(browser->app->bookmarks, i);
            app_save(browser->app);
            update_bookmark_icon(browser);
            return;
        }
    }
    g_ptr_array_add(browser->app->bookmarks, g_strdup(uri));
    if (!app_array_contains(browser->app->history, uri))
        history_add(browser, uri);
    app_save(browser->app);
    update_bookmark_icon(browser);
}

static void on_load_changed(WebKitWebView *view, WebKitLoadEvent event,
                            gpointer data) {
    BrowserTab *tab = data;
    if (event == WEBKIT_LOAD_COMMITTED) {
        const gchar *uri = webkit_web_view_get_uri(view);
        g_object_set_data(G_OBJECT(view), HEADER_COLOR_KEY, NULL);
        if (uri && g_str_has_prefix(uri, "file://")) {
            g_free(tab->file_uri);
            tab->file_uri = g_strdup(uri);
        } else if (uri && !g_str_has_prefix(uri, "about:")) {
            g_clear_pointer(&tab->file_uri, g_free);
        }
        if (current_tab(tab->browser) == tab)
            reset_header_color(tab->browser);
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        history_add(tab->browser, webkit_web_view_get_uri(view));
        if (current_tab(tab->browser) == tab)
            request_header_color(tab->browser, tab);
    }
    if ((event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED) &&
        current_tab(tab->browser) == tab)
        update_header(tab->browser);
}

static void on_view_notify(WebKitWebView *view, GParamSpec *pspec, gpointer data) {
    BrowserTab *tab = data;
    if (g_str_equal(g_param_spec_get_name(pspec), "title") && !tab->file_uri) {
        const gchar *title = webkit_web_view_get_title(view);
        gtk_label_set_text(GTK_LABEL(tab->tab_title),
                           title && *title ? title : "New tab");
        gtk_widget_set_tooltip_text(tab->tab_title, title);
    }
    if (current_tab(tab->browser) == tab)
        update_header(tab->browser);
}

static gboolean on_decide_policy(WebKitWebView *view,
                                 WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType type,
                                 gpointer data) {
    (void)view;
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *response_decision =
            WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (!webkit_response_policy_decision_is_mime_type_supported(response_decision)) {
            webkit_policy_decision_download(decision);
            return TRUE;
        }
        return FALSE;
    }
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;
    WebKitNavigationAction *action =
        webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const gchar *uri = webkit_uri_request_get_uri(
        webkit_navigation_action_get_request(action));
    BrowserTab *tab = data;
    gchar *file_target = file_page_navigation_target(uri);
    if (file_target) {
        tab_load_uri(tab, file_target);
        g_free(file_target);
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    if (file_page_handle_action(uri, tab->view,
                                GTK_WINDOW(tab->browser->window))) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    if (uri && g_str_has_prefix(uri, "file://")) {
        tab_load_uri(tab, uri);
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    return FALSE;
}

static void on_popup_close(WebKitWebView *view, gpointer data) {
    (void)view;
    BrowserWindow *popup = data;
    if (popup && popup->window)
        gtk_widget_destroy(popup->window);
}

static void on_popup_ready_to_show(WebKitWebView *view, gpointer data) {
    (void)view;
    BrowserWindow *popup = data;
    if (!popup || !popup->window)
        return;

    gtk_widget_show_all(popup->window);
    browser_update_tab_visibility(popup);
    gtk_window_present(GTK_WINDOW(popup->window));
}

static WebKitWebView *on_create_web_view(WebKitWebView *view,
                                         WebKitNavigationAction *action,
                                         gpointer data) {
    (void)action;
    BrowserWindow *browser = data;

    /*
     * JavaScript window.open() / OAuth login popups must return a WebView
     * related to the opener. A plain webkit_web_view_new_with_context() view
     * can trigger WebKitGTK popup/window-feature assertions on modern builds,
     * especially visible with Google login popups.
     */
    BrowserWindow *popup = browser_window_new(browser->app, browser->is_private,
                                             FALSE, NULL, NULL);
    gtk_window_set_transient_for(GTK_WINDOW(popup->window),
                                 GTK_WINDOW(browser->window));

    /*
     * Keep later tabs/actions in this popup on the same context as the opener.
     * This matters for private windows and for OAuth state/cookies.
     */
    g_clear_object(&popup->context);
    popup->context = g_object_ref(webkit_web_view_get_context(view));
    downloads_connect_context(browser->app, popup->context);

    BrowserTab *tab = browser_tab_new_with_related_view(popup, view, TRUE);
    g_signal_connect(tab->view, "ready-to-show",
                     G_CALLBACK(on_popup_ready_to_show), popup);
    g_signal_connect(tab->view, "close", G_CALLBACK(on_popup_close), popup);
    return tab->view;
}

static gboolean on_permission_request(WebKitWebView *view,
                                      WebKitPermissionRequest *request,
                                      gpointer data) {
    (void)view;
    BrowserWindow *browser = data;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(browser->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE, "Allow this site to use the requested permission?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Deny", GTK_RESPONSE_REJECT,
                           "Allow", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        webkit_permission_request_allow(request);
    else
        webkit_permission_request_deny(request);
    gtk_widget_destroy(dialog);
    return TRUE;
}

static void close_tab(GtkButton *button, gpointer data) {
    (void)button;
    BrowserTab *tab = data;
    BrowserWindow *browser = tab->browser;
    gint index = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), tab->page);
    if (index >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(browser->notebook), index);
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook)) == 0)
        gtk_widget_destroy(browser->window);
    else
        browser_update_tab_visibility(browser);
}

static GtkWidget *make_tab_label(BrowserTab *tab) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    tab->tab_icon = gtk_image_new_from_icon_name("text-html-symbolic",
                                                 GTK_ICON_SIZE_MENU);
    tab->tab_title = gtk_label_new("New tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab->tab_title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(tab->tab_title, 120, -1);
    GtkWidget *close = icon_button("window-close-symbolic", "Close tab");
    gtk_widget_set_size_request(close, 24, 24);
    gtk_box_pack_start(GTK_BOX(box), tab->tab_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), tab->tab_title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    g_signal_connect(close, "clicked", G_CALLBACK(close_tab), tab);
    enable_tab_drop_target(box, tab);
    enable_tab_drop_target(tab->tab_title, tab);
    gtk_widget_show_all(box);
    return box;
}

static void set_file_tab_title(BrowserTab *tab, const gchar *uri) {
    GFile *file = g_file_new_for_uri(uri);
    gchar *name = g_file_get_basename(file);
    gtk_label_set_text(GTK_LABEL(tab->tab_title),
                       name && *name ? name : "File System");
    gtk_image_set_from_icon_name(GTK_IMAGE(tab->tab_icon), "folder-symbolic",
                                 GTK_ICON_SIZE_MENU);
    g_free(name);
    g_object_unref(file);
}

static void tab_load_uri(BrowserTab *tab, const gchar *uri) {
    FilePageResult result = file_page_load_uri(
        tab->view, uri, GTK_WINDOW(tab->browser->window));
    if (result == FILE_PAGE_DIRECTORY) {
        if (!tab->file_history_navigation) {
            while ((gint)tab->file_history->len >
                   tab->file_history_index + 1)
                g_ptr_array_remove_index(tab->file_history,
                                         tab->file_history->len - 1);
            const gchar *current = tab->file_history_index >= 0
                ? g_ptr_array_index(tab->file_history,
                                    tab->file_history_index)
                : NULL;
            if (g_strcmp0(current, uri) != 0) {
                g_ptr_array_add(tab->file_history, g_strdup(uri));
                tab->file_history_index = (gint)tab->file_history->len - 1;
            }
        }
        tab->file_history_navigation = FALSE;
        g_free(tab->file_uri);
        tab->file_uri = g_strdup(uri);
        set_file_tab_title(tab, uri);
        if (current_tab(tab->browser) == tab)
            update_header(tab->browser);
        return;
    }
    tab->file_history_navigation = FALSE;
    if (result != FILE_PAGE_NOT_HANDLED)
        return;
    if (tab->file_uri) {
        g_ptr_array_set_size(tab->file_history, 0);
        tab->file_history_index = -1;
    }
    g_clear_pointer(&tab->file_uri, g_free);
    webkit_web_view_load_uri(tab->view, uri);
}

static void browser_tab_free(gpointer data) {
    BrowserTab *tab = data;
    g_free(tab->file_uri);
    g_ptr_array_unref(tab->file_history);
    g_free(tab);
}

static BrowserTab *browser_tab_new_internal(BrowserWindow *browser,
                                           const gchar *uri,
                                           gboolean switch_to,
                                           WebKitWebView *related_view) {
    BrowserTab *tab = g_new0(BrowserTab, 1);
    tab->browser = browser;
    tab->file_history = g_ptr_array_new_with_free_func(g_free);
    tab->file_history_index = -1;
    tab->page = gtk_scrolled_window_new(NULL, NULL);
    if (related_view)
        tab->view = WEBKIT_WEB_VIEW(
            webkit_web_view_new_with_related_view(related_view));
    else
        tab->view = browser->context
            ? WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(browser->context))
            : WEBKIT_WEB_VIEW(webkit_web_view_new());
    apply_settings_to_view(browser->app, tab->view);
    webkit_web_view_set_zoom_level(tab->view, browser->app->config.default_zoom);
    gtk_container_add(GTK_CONTAINER(tab->page), GTK_WIDGET(tab->view));
    g_object_set_data_full(G_OBJECT(tab->page), "browser-tab", tab,
                           browser_tab_free);

    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook),
                                          tab->page, make_tab_label(tab));
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(browser->notebook),
                                     tab->page, TRUE);
    g_signal_connect(tab->view, "load-changed", G_CALLBACK(on_load_changed), tab);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(on_view_notify), tab);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(on_view_notify), tab);
    g_signal_connect(tab->view, "notify::estimated-load-progress",
                     G_CALLBACK(on_view_notify), tab);
    g_signal_connect(tab->view, "notify::can-go-back",
                     G_CALLBACK(on_view_notify), tab);
    g_signal_connect(tab->view, "notify::can-go-forward",
                     G_CALLBACK(on_view_notify), tab);
    g_signal_connect(tab->view, "notify::favicon",
                     G_CALLBACK(on_favicon_changed), tab);
    g_signal_connect(tab->view, "create", G_CALLBACK(on_create_web_view), browser);
    g_signal_connect(tab->view, "decide-policy",
                     G_CALLBACK(on_decide_policy), tab);
    g_signal_connect(tab->view, "permission-request",
                     G_CALLBACK(on_permission_request), browser);
    g_signal_connect(tab->view, "context-menu",
                     G_CALLBACK(on_context_menu), tab);
    g_signal_connect(tab->view, "key-press-event",
                     G_CALLBACK(on_window_key_press), browser);
    gtk_widget_show_all(tab->page);
    browser_update_tab_visibility(browser);
    if (switch_to)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), index);
    if (uri)
        tab_load_uri(tab, uri);
    return tab;
}

BrowserTab *browser_tab_new(BrowserWindow *browser, const gchar *uri,
                            gboolean switch_to) {
    return browser_tab_new_internal(browser, uri, switch_to, NULL);
}

static BrowserTab *browser_tab_new_with_related_view(BrowserWindow *browser,
                                                     WebKitWebView *related_view,
                                                     gboolean switch_to) {
    return browser_tab_new_internal(browser, NULL, switch_to, related_view);
}

static void update_header(BrowserWindow *browser) {
    update_header_for_tab(browser, current_tab(browser), FALSE);
}

static void update_header_for_tab(BrowserWindow *browser, BrowserTab *tab,
                                  gboolean force_address) {
    gboolean enabled = tab != NULL;
    gboolean can_go_back = enabled &&
        (tab->file_uri ? tab->file_history_index > 0
                       : webkit_web_view_can_go_back(tab->view));
    gboolean can_go_forward = enabled &&
        (tab->file_uri
             ? tab->file_history_index + 1 < (gint)tab->file_history->len
             : webkit_web_view_can_go_forward(tab->view));

    gtk_widget_set_sensitive(browser->address, enabled);
    gtk_widget_set_sensitive(browser->go, enabled);
    gtk_widget_set_sensitive(browser->bookmark, enabled);
    gtk_widget_set_sensitive(browser->reload, enabled);
    gtk_widget_set_sensitive(browser->home, enabled);
    gtk_widget_set_sensitive(browser->back, can_go_back);
    gtk_widget_set_sensitive(browser->forward, can_go_forward);

    if (browser->app_mode) {
        gtk_widget_set_visible(browser->back, can_go_back);
        gtk_widget_set_visible(browser->forward, can_go_forward);
    }

    if (!tab) {
        reset_header_color(browser);
        gtk_entry_set_text(GTK_ENTRY(browser->address),
                           current_special_page_title(browser));
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(browser->progress), 0);
        if (browser->favicon)
            gtk_image_set_from_icon_name(GTK_IMAGE(browser->favicon),
                                         current_special_page_icon(browser),
                                         GTK_ICON_SIZE_MENU);
        if (browser->app_mode && browser->app_title)
            gtk_label_set_text(GTK_LABEL(browser->app_title),
                               browser->app_name ? browser->app_name : "Web app");
        return;
    }
    if (browser->app_mode && browser->app_title) {
        const gchar *title = webkit_web_view_get_title(tab->view);
        const gchar *label = title && *title ? title :
            browser->app_name && *browser->app_name ? browser->app_name : "Web app";
        gtk_label_set_text(GTK_LABEL(browser->app_title), label);
        gtk_window_set_title(GTK_WINDOW(browser->window), label);
    }
    const gchar *uri = tab_display_uri(tab);
    if (force_address || !gtk_widget_has_focus(browser->address))
        gtk_entry_set_text(GTK_ENTRY(browser->address), uri ? uri : "");
    gdouble progress = webkit_web_view_get_estimated_load_progress(tab->view);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(browser->progress),
                                  progress < 1.0 ? progress : 0.0);
    const gchar *icon = webkit_web_view_is_loading(tab->view)
                            ? "process-stop-symbolic"
                            : "view-refresh-symbolic";
    gtk_button_set_image(GTK_BUTTON(browser->reload),
        gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
    if (tab->file_uri) {
        reset_header_color(browser);
        if (browser->favicon)
            gtk_image_set_from_icon_name(GTK_IMAGE(browser->favicon),
                                         "folder-symbolic", GTK_ICON_SIZE_MENU);
    } else {
        cairo_surface_t *surface = webkit_web_view_get_favicon(tab->view);
        set_favicon(GTK_IMAGE(tab->tab_icon), surface, 16);
        if (browser->favicon)
            set_favicon(GTK_IMAGE(browser->favicon), surface, 18);
    }
    update_bookmark_icon_for_tab(browser, tab);
}

static void on_switch_page(GtkNotebook *notebook, GtkWidget *page, guint index,
                           gpointer data) {
    (void)notebook;
    (void)index;
    BrowserWindow *browser = data;
    BrowserTab *tab = g_object_get_data(G_OBJECT(page), "browser-tab");
    update_header_for_tab(browser, tab, TRUE);
    request_header_color(browser, tab);
}

static void new_tab_action(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *browser = data;
    browser_tab_new(browser, browser->app->config.homepage, TRUE);
}

static void new_window_action(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *source = data;
    BrowserWindow *browser = browser_window_new(source->app, FALSE, FALSE, NULL, NULL);
    browser_tab_new(browser, source->app->config.homepage, TRUE);
    gtk_widget_show_all(browser->window);
    browser_update_tab_visibility(browser);
}

static void new_private_action(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *source = data;
    BrowserWindow *browser = browser_window_new(source->app, TRUE, FALSE, NULL, NULL);
    browser_tab_new(browser, source->app->config.homepage, TRUE);
    gtk_widget_show_all(browser->window);
    browser_update_tab_visibility(browser);
}

static void ensure_developer_tools_enabled(BrowserWindow *browser) {
    if (!browser->app->config.developer_tools) {
        browser->app->config.developer_tools = TRUE;
        app_save(browser->app);
    }
    browser_apply_settings(browser->app);
}

static void open_web_inspector(BrowserWindow *browser) {
    BrowserTab *tab = current_tab(browser);
    if (!tab || tab->file_uri)
        return;

    ensure_developer_tools_enabled(browser);
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(tab->view);
    if (inspector)
        webkit_web_inspector_show(inspector);
}

static void open_web_inspector_action(GtkButton *button, gpointer data) {
    (void)button;
    open_web_inspector(data);
}

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data) {
    (void)widget;
    guint key = gdk_keyval_to_lower(event->keyval);
    GdkModifierType state = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean ctrl_shift = (state & GDK_CONTROL_MASK) &&
                          (state & GDK_SHIFT_MASK);

    BrowserWindow *browser = data;
    if (event->keyval == GDK_KEY_F12 ||
        (ctrl_shift && (key == GDK_KEY_i || key == GDK_KEY_j))) {
        open_web_inspector(browser);
        return TRUE;
    }
    if ((state & GDK_CONTROL_MASK) && key == GDK_KEY_j && !ctrl_shift) {
        downloads_open(browser);
        return TRUE;
    }
    if ((state & GDK_CONTROL_MASK) && key == GDK_KEY_h && !ctrl_shift) {
        library_open_history(browser);
        return TRUE;
    }
    if (ctrl_shift && key == GDK_KEY_o) {
        library_open_bookmarks(browser);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_context_menu(WebKitWebView *view, WebKitContextMenu *menu,
                                GdkEvent *event, WebKitHitTestResult *hit_test,
                                gpointer data) {
    (void)view;
    (void)event;
    (void)hit_test;
    BrowserTab *tab = data;

    if (!tab || tab->file_uri || !tab->browser->app->config.developer_tools)
        return FALSE;

    webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
    webkit_context_menu_append(menu,
        webkit_context_menu_item_new_from_stock_action(
            WEBKIT_CONTEXT_MENU_ACTION_INSPECT_ELEMENT));
    return FALSE;
}

static void zoom_by(BrowserWindow *browser, gdouble amount) {
    BrowserTab *tab = current_tab(browser);
    if (tab) {
        gdouble zoom = webkit_web_view_get_zoom_level(tab->view) + amount;
        webkit_web_view_set_zoom_level(tab->view, CLAMP(zoom, 0.3, 3.0));
    }
}

static void zoom_in_action(GtkButton *button, gpointer data) {
    (void)button;
    zoom_by(data, 0.1);
}

static void zoom_out_action(GtkButton *button, gpointer data) {
    (void)button;
    zoom_by(data, -0.1);
}

static void zoom_reset_action(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *browser = data;
    BrowserTab *tab = current_tab(browser);
    if (tab)
        webkit_web_view_set_zoom_level(tab->view,
                                       browser->app->config.default_zoom);
}

static GtkWidget *menu_row(const gchar *icon, const gchar *text,
                           GCallback callback, gpointer data) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(box),
        gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    g_signal_connect(button, "clicked", callback, data);
    return button;
}

static void settings_action(GtkButton *button, gpointer data) {
    (void)button;
    settings_open(data);
}

static void install_pwa_action(GtkButton *button, gpointer data) {
    (void)button;
    BrowserWindow *browser = data;
    BrowserTab *tab = current_tab(browser);
    if (!tab)
        return;
    pwa_install_site(browser->app, GTK_WINDOW(browser->window),
                     webkit_web_view_get_uri(tab->view),
                     webkit_web_view_get_title(tab->view),
                     webkit_web_view_get_favicon(tab->view));
}

static void downloads_action(GtkButton *button, gpointer data) {
    (void)button;
    downloads_open(data);
}

static void history_action(GtkButton *button, gpointer data) {
    (void)button;
    library_open_history(data);
}

static void bookmarks_action(GtkButton *button, gpointer data) {
    (void)button;
    library_open_bookmarks(data);
}

static GtkWidget *build_menu(BrowserWindow *browser) {
    GtkWidget *popover = gtk_popover_new(NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_pack_start(GTK_BOX(box), menu_row("tab-new-symbolic", "New tab",
        G_CALLBACK(new_tab_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("window-new-symbolic", "New window",
        G_CALLBACK(new_window_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("changes-prevent-symbolic",
        "New private window", G_CALLBACK(new_private_action), browser), FALSE, FALSE, 0);
    if (!browser->is_private && !browser->app_mode)
        gtk_box_pack_start(GTK_BOX(box), menu_row("emblem-web-symbolic",
            "Install site as app", G_CALLBACK(install_pwa_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("utilities-terminal-symbolic",
        "Developer tools", G_CALLBACK(open_web_inspector_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("folder-download-symbolic",
        "Downloads", G_CALLBACK(downloads_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("document-open-recent-symbolic",
        "History", G_CALLBACK(history_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), menu_row("user-bookmarks-symbolic",
        "Bookmarks", G_CALLBACK(bookmarks_action), browser), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 5);

    GtkWidget *zoom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *out = icon_button("zoom-out-symbolic", "Zoom out");
    GtkWidget *reset = gtk_button_new_with_label("100%");
    GtkWidget *in = icon_button("zoom-in-symbolic", "Zoom in");
    g_signal_connect(out, "clicked", G_CALLBACK(zoom_out_action), browser);
    g_signal_connect(reset, "clicked", G_CALLBACK(zoom_reset_action), browser);
    g_signal_connect(in, "clicked", G_CALLBACK(zoom_in_action), browser);
    gtk_box_pack_start(GTK_BOX(zoom), gtk_label_new("Zoom"), TRUE, TRUE, 8);
    gtk_box_pack_start(GTK_BOX(zoom), out, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(zoom), reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(zoom), in, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), zoom, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(box), menu_row("preferences-system-symbolic",
        "Settings", G_CALLBACK(settings_action), browser), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);
    gtk_widget_show_all(box);
    return popover;
}

static void window_destroyed(GtkWidget *widget, gpointer data) {
    (void)widget;
    BrowserWindow *browser = data;
    browser->app->windows = g_list_remove(browser->app->windows, browser);
    if (browser->download_popover_hide_id) {
        g_source_remove(browser->download_popover_hide_id);
        browser->download_popover_hide_id = 0;
    }
    g_clear_object(&browser->context);
    if (browser->header_css) {
        GdkScreen *screen = gtk_widget_get_screen(browser->window);
        gtk_style_context_remove_provider_for_screen(screen,
            GTK_STYLE_PROVIDER(browser->header_css));
    }
    g_clear_object(&browser->header_css);
    g_free(browser->header_css_class);
    g_free(browser->app_name);
    g_free(browser->app_id);
    g_free(browser);
}

BrowserWindow *browser_window_new(App *app, gboolean is_private,
                                  gboolean app_mode,
                                  const gchar *app_name,
                                  const gchar *app_id) {
    BrowserWindow *browser = g_new0(BrowserWindow, 1);
    browser->app = app;
    browser->is_private = is_private;
    browser->app_mode = app_mode;
    browser->app_name = g_strdup(app_name);
    browser->app_id = g_strdup(app_id);
    if (is_private)
        browser->context = webkit_web_context_new_ephemeral();
    else
        browser->context = app_get_web_context(app);
    downloads_connect_context(app, browser->context);
    service_scheme_register(browser->context ? browser->context
                                             : webkit_web_context_get_default());
    browser->window = gtk_application_window_new(app->application);
    if (app_mode && browser->app_id) {
        gtk_window_set_role(GTK_WINDOW(browser->window), browser->app_id);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_window_set_wmclass(GTK_WINDOW(browser->window),
                               browser->app_id, browser->app_id);
        G_GNUC_END_IGNORE_DEPRECATIONS
        pwa_apply_window_icon(GTK_WINDOW(browser->window), browser->app_id);
    }
    gtk_window_set_default_size(GTK_WINDOW(browser->window), 1180, 760);
    gtk_window_set_title(GTK_WINDOW(browser->window),
                         app_mode && browser->app_name ? browser->app_name :
                         is_private ? "Astra — Private" : "Astra");
    g_signal_connect(browser->window, "destroy",
                     G_CALLBACK(window_destroyed), browser);
    g_signal_connect(browser->window, "key-press-event",
                     G_CALLBACK(on_window_key_press), browser);

    browser->header = gtk_header_bar_new();
    GtkWidget *header = browser->header;
    browser->header_css = gtk_css_provider_new();
    browser->header_css_class = g_strdup_printf("astra-header-%u",
                                                ++header_style_counter);
    GtkStyleContext *header_style = gtk_widget_get_style_context(header);
    gtk_style_context_add_class(header_style, browser->header_css_class);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(header),
        GTK_STYLE_PROVIDER(browser->header_css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header),
                             app_mode && browser->app_name ? browser->app_name :
                             is_private ? "Private" : NULL);
    enable_browser_drop_target(header, browser);
    browser->back = icon_button("go-previous-symbolic", "Back");
    browser->forward = icon_button("go-next-symbolic", "Forward");
    browser->reload = icon_button("view-refresh-symbolic", "Reload / Stop");
    browser->home = icon_button("go-home-symbolic", "Home");
    browser->new_tab = icon_button("tab-new-symbolic", "New tab");
    /*
     * Keep one visible favicon in app mode.
     *
     * PWA windows already use the installed site icon as the window icon via
     * pwa_apply_window_icon(). Some GTK themes show that icon at the very
     * beginning of the header bar. Putting the same favicon in the custom
     * title created a duplicate, so app mode uses the window/app icon only.
     *
     * Normal browser windows do not get a per-page window icon, so put the
     * current page favicon at the beginning of the toolbar instead of inside
     * the address box.
     */
    if (!app_mode) {
        browser->favicon = gtk_image_new_from_icon_name("text-html-symbolic",
                                                        GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(browser->favicon, "Page icon");
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->favicon);
    }
    if (app_mode) {
        /*
         * Keep PWA/app windows minimal, but still expose useful history
         * navigation. These buttons are packed in the headerbar and are
         * hidden with no-show-all until the current view can actually go
         * backward or forward. Normal browser windows keep the traditional
         * always-visible disabled/enabled toolbar buttons.
         */
        gtk_widget_set_no_show_all(browser->back, TRUE);
        gtk_widget_set_no_show_all(browser->forward, TRUE);
        gtk_widget_hide(browser->back);
        gtk_widget_hide(browser->forward);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->back);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->forward);
    } else {
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->back);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->forward);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->reload);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->home);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->new_tab);
    }

    GtkWidget *location = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *overlay = gtk_overlay_new();
    browser->address = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->address),
                                   "Search or enter address");
    gtk_widget_set_size_request(browser->address, 500, -1);
    GtkEntryCompletion *completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion,
                                   GTK_TREE_MODEL(app->completion_store));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_completion_set_inline_completion(completion, TRUE);
    gtk_entry_set_completion(GTK_ENTRY(browser->address), completion);
    g_object_unref(completion);
    browser->progress = gtk_progress_bar_new();
    gtk_widget_set_valign(browser->progress, GTK_ALIGN_END);
    gtk_widget_set_halign(browser->progress, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), browser->progress);
    gtk_container_add(GTK_CONTAINER(overlay), browser->address);
    enable_browser_drop_target(browser->address, browser);
    enable_browser_drop_target(overlay, browser);
    browser->bookmark = icon_button("non-starred-symbolic", "Bookmark this page");
    browser->go = icon_button("go-jump-symbolic", "Go");
    browser->download_button = download_indicator_button(browser);
    g_signal_connect(browser->download_button, "clicked",
                     G_CALLBACK(downloads_action), browser);
    if (app_mode) {
        GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        browser->app_title = gtk_label_new(browser->app_name && *browser->app_name
                                           ? browser->app_name : "Web app");
        gtk_label_set_ellipsize(GTK_LABEL(browser->app_title), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(title_box), browser->app_title, TRUE, TRUE, 0);
        gtk_widget_set_hexpand(title_box, TRUE);
        enable_browser_drop_target(title_box, browser);
        enable_browser_drop_target(browser->app_title, browser);
        gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), title_box);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), browser->download_button);
    } else {
        gtk_box_pack_start(GTK_BOX(location), overlay, TRUE, TRUE, 0);
        enable_browser_drop_target(location, browser);
        gtk_box_pack_start(GTK_BOX(location), browser->bookmark, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(location), browser->go, FALSE, FALSE, 0);
        gtk_widget_set_hexpand(location, TRUE);
        gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), location);

        GtkWidget *menu = gtk_menu_button_new();
        gtk_button_set_image(GTK_BUTTON(menu),
            gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), build_menu(browser));
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), browser->download_button);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu);
    }
    browser_update_download_indicator(browser);
    gtk_window_set_titlebar(GTK_WINDOW(browser->window), header);

    browser->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(browser->notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(browser->notebook), FALSE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(browser->notebook), FALSE);
    gtk_container_add(GTK_CONTAINER(browser->window), browser->notebook);
    g_signal_connect(browser->notebook, "switch-page",
                     G_CALLBACK(on_switch_page), browser);
    g_signal_connect(browser->back, "clicked", G_CALLBACK(on_back), browser);
    g_signal_connect(browser->forward, "clicked", G_CALLBACK(on_forward), browser);
    g_signal_connect(browser->reload, "clicked", G_CALLBACK(on_reload), browser);
    g_signal_connect(browser->home, "clicked", G_CALLBACK(on_home), browser);
    g_signal_connect(browser->new_tab, "clicked",
                     G_CALLBACK(new_tab_action), browser);
    g_signal_connect(browser->address, "activate",
                     G_CALLBACK(on_address_activate), browser);
    g_signal_connect(browser->go, "clicked", G_CALLBACK(on_go), browser);
    g_signal_connect(browser->bookmark, "clicked",
                     G_CALLBACK(on_bookmark), browser);
    app->windows = g_list_prepend(app->windows, browser);
    return browser;
}
