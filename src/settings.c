#include "settings.h"

#include "app.h"
#include "browser.h"

static GtkWidget *icon_button(const gchar *icon, const gchar *tooltip) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");
    return button;
}

static GtkWidget *settings_row(const gchar *title, const gchar *description,
                               GtkWidget *control) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name = gtk_label_new(title);
    GtkWidget *desc = gtk_label_new(description);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_set_halign(desc, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(desc), "dim-label");
    gtk_box_pack_start(GTK_BOX(labels), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels), desc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), labels, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(row), control, FALSE, FALSE, 0);
    gtk_widget_set_margin_top(row, 7);
    gtk_widget_set_margin_bottom(row, 7);
    return row;
}

static GtkWidget *section_label(const gchar *text) {
    GtkWidget *label = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(label, 16);
    gtk_widget_set_margin_bottom(label, 4);
    return label;
}

static void setting_text_changed(GtkEntry *entry, gpointer data) {
    BrowserWindow *browser = data;
    const gchar *key = g_object_get_data(G_OBJECT(entry), "setting-key");
    gchar **target = NULL;
    if (g_str_equal(key, "homepage"))
        target = &browser->app->config.homepage;
    else if (g_str_equal(key, "search_url"))
        target = &browser->app->config.search_url;
    else if (g_str_equal(key, "user_agent"))
        target = &browser->app->config.user_agent;
    if (target) {
        g_free(*target);
        *target = g_strdup(gtk_entry_get_text(entry));
    }
    app_save(browser->app);
    browser_apply_settings(browser->app);
}

static gboolean setting_switch_changed(GtkSwitch *widget, gboolean state,
                                       gpointer data) {
    BrowserWindow *browser = data;
    AppConfig *c = &browser->app->config;
    const gchar *key = g_object_get_data(G_OBJECT(widget), "setting-key");
    if (g_str_equal(key, "javascript")) c->javascript = state;
    else if (g_str_equal(key, "images")) c->images = state;
    else if (g_str_equal(key, "webgl")) c->webgl = state;
    else if (g_str_equal(key, "media_stream")) c->media_stream = state;
    else if (g_str_equal(key, "autoplay")) c->autoplay = state;
    else if (g_str_equal(key, "developer_tools")) c->developer_tools = state;
    else if (g_str_equal(key, "smooth_scrolling")) c->smooth_scrolling = state;
    else if (g_str_equal(key, "zoom_text_only")) c->zoom_text_only = state;
    else if (g_str_equal(key, "dark_ui")) c->dark_ui = state;
    app_save(browser->app);
    browser_apply_settings(browser->app);
    return FALSE;
}

static void zoom_setting_changed(GtkSpinButton *spin, gpointer data) {
    BrowserWindow *browser = data;
    browser->app->config.default_zoom = gtk_spin_button_get_value(spin) / 100.0;
    app_save(browser->app);
    browser_apply_zoom(browser->app);
}

static GtkWidget *text_setting(BrowserWindow *browser, const gchar *key,
                               const gchar *value) {
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), value);
    gtk_widget_set_size_request(entry, 310, -1);
    g_object_set_data(G_OBJECT(entry), "setting-key", (gpointer)key);
    g_signal_connect(entry, "changed", G_CALLBACK(setting_text_changed), browser);
    return entry;
}

static GtkWidget *switch_setting(BrowserWindow *browser, const gchar *key,
                                 gboolean value) {
    GtkWidget *widget = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(widget), value);
    g_object_set_data(G_OBJECT(widget), "setting-key", (gpointer)key);
    g_signal_connect(widget, "state-set", G_CALLBACK(setting_switch_changed), browser);
    return widget;
}

static void clear_data_finished(GObject *source, GAsyncResult *result, gpointer data) {
    WebKitWebsiteDataManager *manager = WEBKIT_WEBSITE_DATA_MANAGER(source);
    GError *error = NULL;
    webkit_website_data_manager_clear_finish(manager, result, &error);
    GtkButton *button = data;
    gtk_button_set_label(button, error ? "Could not clear data" : "Data cleared");
    g_clear_error(&error);
    g_object_unref(button);
}

static void clear_data_clicked(GtkButton *button, gpointer data) {
    BrowserWindow *browser = data;
    gtk_list_store_clear(browser->app->completion_store);
    g_ptr_array_set_size(browser->app->history, 0);
    app_save(browser->app);
    gtk_button_set_label(button, "Clearing…");
    WebKitWebsiteDataManager *manager =
        webkit_web_context_get_website_data_manager(webkit_web_context_get_default());
    webkit_website_data_manager_clear(manager, WEBKIT_WEBSITE_DATA_ALL, 0, NULL,
                                      clear_data_finished, g_object_ref(button));
}

static GtkWidget *build_settings_page(BrowserWindow *browser) {
    AppConfig *c = &browser->app->config;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(content, 36);
    gtk_widget_set_margin_end(content, 36);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 32);
    gtk_widget_set_size_request(content, 680, -1);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span size='x-large' weight='bold'>Browser settings</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), section_label("Startup and search"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Homepage", "Opened by the Home button and in new tabs.",
        text_setting(browser, "homepage", c->homepage)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Search URL", "Use %s where the escaped search terms should go.",
        text_setting(browser, "search_url", c->search_url)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), section_label("Web content"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("JavaScript", "Allow sites to run scripts.", switch_setting(browser, "javascript", c->javascript)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Load images", "Display images embedded in pages.", switch_setting(browser, "images", c->images)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("WebGL", "Allow hardware-accelerated 3D graphics.", switch_setting(browser, "webgl", c->webgl)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Camera and microphone", "Permit sites to request media capture access.", switch_setting(browser, "media_stream", c->media_stream)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Autoplay media", "Allow audio and video without a click.", switch_setting(browser, "autoplay", c->autoplay)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Smooth scrolling", "Animate page scrolling.", switch_setting(browser, "smooth_scrolling", c->smooth_scrolling)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), section_label("Appearance and advanced"), FALSE, FALSE, 0);
    GtkWidget *zoom = gtk_spin_button_new_with_range(30, 300, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom), c->default_zoom * 100);
    g_signal_connect(zoom, "value-changed", G_CALLBACK(zoom_setting_changed), browser);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Default zoom (%)", "Applied to all open tabs.", zoom), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Zoom text only", "Keep images at their original size.", switch_setting(browser, "zoom_text_only", c->zoom_text_only)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Dark browser chrome", "Use GTK's dark theme for the browser interface.", switch_setting(browser, "dark_ui", c->dark_ui)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Developer tools", "Enable right-click Inspect Element. F12 / Ctrl+Shift+I opens the Web Inspector.", switch_setting(browser, "developer_tools", c->developer_tools)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Custom user agent", "Leave blank to use WebKitGTK's default.",
        text_setting(browser, "user_agent", c->user_agent)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), section_label("Privacy"), FALSE, FALSE, 0);
    GtkWidget *clear = gtk_button_new_with_label("Clear browsing data");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_data_clicked), browser);
    gtk_box_pack_start(GTK_BOX(content), settings_row("Cookies, cache, and history", "Deletes stored website data and address suggestions.", clear), FALSE, FALSE, 0);

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(center), content, TRUE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), center);
    g_object_set_data(G_OBJECT(scroll), "settings-page", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(scroll), "astra-page-title", (gpointer)"Settings");
    g_object_set_data(G_OBJECT(scroll), "astra-page-icon",
                      (gpointer)"preferences-system-symbolic");
    return scroll;
}

static void close_settings(GtkButton *button, gpointer data) {
    BrowserWindow *browser = data;
    GtkWidget *page_widget =
        g_object_get_data(G_OBJECT(button), "settings-page-widget");
    gint page = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), page_widget);
    if (page >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(browser->notebook), page);
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook)) == 0)
        gtk_widget_destroy(browser->window);
    else
        browser_update_tab_visibility(browser);
}

void settings_open(BrowserWindow *browser) {
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), i);
        if (g_object_get_data(G_OBJECT(page), "settings-page")) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), i);
            return;
        }
    }

    GtkWidget *page = build_settings_page(browser);
    GtkWidget *label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(label),
        gtk_image_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_MENU),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new("Settings"), FALSE, FALSE, 0);
    GtkWidget *close = icon_button("window-close-symbolic", "Close settings");
    g_object_set_data(G_OBJECT(close), "settings-page-widget", page);
    g_signal_connect(close, "clicked", G_CALLBACK(close_settings), browser);
    gtk_box_pack_start(GTK_BOX(label), close, FALSE, FALSE, 0);
    gtk_widget_show_all(label);

    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), page, label);
    gtk_widget_show_all(page);
    browser_update_tab_visibility(browser);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), index);
}
