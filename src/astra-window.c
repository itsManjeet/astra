#include "astra/astra-window.h"
#include "astra/astra-browser-view.h"
#include "astra/astra-downloads.h"
#include "astra/astra-permissions.h"
#include "astra/astra-utils.h"
#include "astra/astra-settings.h"
#include "astra/astra-webapp.h"

#include <gdk/gdkkeysyms.h>

#include <string.h>

typedef struct {
  guint id;
  AstraBrowserView *browser;
  GtkWidget *tab_box;
  GtkWidget *tab_button;
  GtkWidget *tab_icon;
  GtkWidget *tab_spinner;
  GtkWidget *tab_label;
  GtkWidget *close_button;
  gchar *stack_name;
  gchar *title;
} AstraTab;

struct _AstraWindow {
  GtkApplicationWindow parent_instance;

  GtkWidget *main_box;
  GtkWidget *titlebar;
  GtkWidget *tab_scroller;
  GtkWidget *tab_strip;
  GtkWidget *new_tab_button;

  GtkWidget *nav_bar;
  GtkWidget *progress_bar;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *reload_button;
  GtkWidget *url_entry;
  GtkWidget *shield_button;
  GtkWidget *menu_button;

  GtkWidget *content_box;
  GtkWidget *browser_stack;

  GList *tabs;
  AstraTab *active_tab;
  guint next_tab_id;
  gboolean private_mode;
};

G_DEFINE_TYPE(AstraWindow, astra_window, GTK_TYPE_APPLICATION_WINDOW)

static void astra_window_add_tab(AstraWindow *self, const char *uri, gboolean switch_to_tab);
static void astra_window_select_tab(AstraWindow *self, AstraTab *tab);
static void astra_window_close_tab(AstraWindow *self, AstraTab *tab);
static void astra_window_show_settings(AstraWindow *self);
static void astra_window_apply_settings_to_all_tabs(AstraWindow *self);

static AstraTab *find_tab_by_view(AstraWindow *self, AstraBrowserView *view) {
  for (GList *l = self->tabs; l != NULL; l = l->next) {
    AstraTab *tab = l->data;
    if (tab->browser == view) return tab;
  }
  return NULL;
}

static WebKitWebView *active_web_view(AstraWindow *self) {
  if (self->active_tab == NULL) return NULL;
  return astra_browser_view_get_web_view(self->active_tab->browser);
}

static void update_nav_buttons(AstraWindow *self) {
  gboolean has_view = self->active_tab != NULL;
  gtk_widget_set_sensitive(self->back_button, has_view && astra_browser_view_can_go_back(self->active_tab->browser));
  gtk_widget_set_sensitive(self->forward_button, has_view && astra_browser_view_can_go_forward(self->active_tab->browser));
  gtk_widget_set_sensitive(self->reload_button, has_view);
}

static void update_tab_visuals(AstraWindow *self) {
  for (GList *l = self->tabs; l != NULL; l = l->next) {
    AstraTab *tab = l->data;
    gboolean active = tab == self->active_tab;
    GtkStyleContext *ctx = gtk_widget_get_style_context(tab->tab_box);
    if (active) {
      gtk_style_context_add_class(ctx, "astra-tab-active");
    } else {
      gtk_style_context_remove_class(ctx, "astra-tab-active");
    }
  }
}

static void update_window_for_active_tab(AstraWindow *self) {
  if (self->active_tab == NULL) return;

  WebKitWebView *web_view = active_web_view(self);
  const char *uri = astra_browser_view_get_uri(self->active_tab->browser);
  const char *title = webkit_web_view_get_title(web_view);

  if (uri != NULL && !g_str_has_prefix(uri, "resource://") && !g_str_has_prefix(uri, "astra://new-tab")) {
    gtk_entry_set_text(GTK_ENTRY(self->url_entry), uri);
  } else {
    gtk_entry_set_text(GTK_ENTRY(self->url_entry), "");
  }

  /* Keep the native window/task-switcher title updated, but GtkHeaderBar does
   * not render it because the titlebar uses a zero-width custom title.
   */
  gtk_window_set_title(GTK_WINDOW(self), title && *title ? title : "Astra");
  update_nav_buttons(self);
  update_tab_visuals(self);
}

static void go_back(GtkButton *button, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab != NULL) astra_browser_view_go_back(self->active_tab->browser);
}

static void go_forward(GtkButton *button, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab != NULL) astra_browser_view_go_forward(self->active_tab->browser);
}

static void reload_page(GtkButton *button, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab != NULL) astra_browser_view_reload(self->active_tab->browser);
}

static void url_entry_activate(GtkEntry *entry, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  const char *text = gtk_entry_get_text(entry);
  astra_window_load_uri(self, text);
}

static void tab_button_clicked(GtkButton *button, gpointer user_data) {
  AstraTab *tab = user_data;
  AstraWindow *self = ASTRA_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
  astra_window_select_tab(self, tab);
}

static gboolean tab_box_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
  (void)widget;
  AstraTab *tab = user_data;

  if (event == NULL || tab == NULL) return FALSE;

  AstraWindow *self = ASTRA_WINDOW(gtk_widget_get_toplevel(tab->tab_box));

  if (event->button == GDK_BUTTON_PRIMARY) {
    astra_window_select_tab(self, tab);
    return TRUE;
  }

  if (event->button == GDK_BUTTON_MIDDLE) {
    astra_window_close_tab(self, tab);
    return TRUE;
  }

  return FALSE;
}

static void tab_close_clicked(GtkButton *button, gpointer user_data) {
  AstraTab *tab = user_data;
  AstraWindow *self = ASTRA_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
  astra_window_close_tab(self, tab);
}

static void add_tab_clicked(GtkButton *button, gpointer user_data) {
  astra_window_add_tab(ASTRA_WINDOW(user_data), NULL, TRUE);
}

static void on_uri_changed(AstraBrowserView *view, const char *uri, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab != NULL && self->active_tab->browser == view) {
    if (uri != NULL && !g_str_has_prefix(uri, "resource://") && !g_str_has_prefix(uri, "astra://new-tab")) {
      gtk_entry_set_text(GTK_ENTRY(self->url_entry), uri);
    } else if (uri != NULL && g_str_has_prefix(uri, "astra://new-tab")) {
      gtk_entry_set_text(GTK_ENTRY(self->url_entry), "");
    }
    update_nav_buttons(self);
  }
}

static void on_title_changed(AstraBrowserView *view, const char *title, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  AstraTab *tab = find_tab_by_view(self, view);
  if (tab == NULL) return;

  g_free(tab->title);
  tab->title = g_strdup(title && *title ? title : "New Tab");
  gtk_label_set_text(GTK_LABEL(tab->tab_label), tab->title);
  gtk_widget_set_tooltip_text(tab->tab_box, tab->title);

  if (tab == self->active_tab) {
    /* Window title is only for the compositor/task switcher; the headerbar
     * itself stays title-free so tabs get the horizontal room.
     */
    gtk_window_set_title(GTK_WINDOW(self), tab->title);
  }
}

static void on_favicon_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  AstraTab *tab = NULL;
  for (GList *l = self->tabs; l != NULL; l = l->next) {
    AstraTab *candidate = l->data;
    if (astra_browser_view_get_web_view(candidate->browser) == web_view) { tab = candidate; break; }
  }
  if (tab == NULL || tab->tab_icon == NULL) return;

  cairo_surface_t *surface = webkit_web_view_get_favicon(web_view);
  if (surface != NULL) {
    gtk_image_set_from_surface(GTK_IMAGE(tab->tab_icon), surface);
    gtk_widget_show(tab->tab_icon);
  } else {
    gtk_image_set_from_icon_name(GTK_IMAGE(tab->tab_icon), "text-html-symbolic", GTK_ICON_SIZE_MENU);
  }
}

static void on_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab == NULL || astra_browser_view_get_web_view(self->active_tab->browser) != web_view) return;
  double progress = webkit_web_view_get_estimated_load_progress(web_view);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), progress);
  gtk_widget_set_visible(self->progress_bar, progress > 0.0 && progress < 1.0);
}

static void on_load_changed(AstraBrowserView *view, int event, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  AstraTab *tab = find_tab_by_view(self, view);
  if (tab != NULL) {
    if (event == WEBKIT_LOAD_STARTED) {
      gtk_widget_hide(tab->tab_icon);
      gtk_widget_show(tab->tab_spinner);
      gtk_spinner_start(GTK_SPINNER(tab->tab_spinner));
    } else if (event == WEBKIT_LOAD_FINISHED) {
      gtk_spinner_stop(GTK_SPINNER(tab->tab_spinner));
      gtk_widget_hide(tab->tab_spinner);
      gtk_widget_show(tab->tab_icon);
    }
  }
  if (self->active_tab != NULL && self->active_tab->browser == view) {
    update_nav_buttons(self);
    if (event == WEBKIT_LOAD_STARTED) {
      gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), 0.05);
      gtk_widget_show(self->progress_bar);
    } else if (event == WEBKIT_LOAD_FINISHED) {
      gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), 1.0);
      gtk_widget_hide(self->progress_bar);
    }
  }
}

static gboolean permission_request(WebKitWebView *web_view,
                                   WebKitPermissionRequest *request,
                                   gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  g_autofree char *origin = astra_uri_origin(webkit_web_view_get_uri(web_view));
  return astra_permissions_handle_request(GTK_WINDOW(self), request, origin);
}

static void show_shield_panel(GtkButton *button, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  WebKitWebView *web_view = active_web_view(self);
  const char *uri = web_view ? webkit_web_view_get_uri(web_view) : NULL;
  g_autofree char *origin = astra_uri_origin(uri);

  GtkWidget *popover = gtk_popover_new(GTK_WIDGET(button));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width(GTK_CONTAINER(box), 14);
  gtk_container_add(GTK_CONTAINER(popover), box);

  GtkWidget *title = gtk_label_new(origin ? origin : "Astra");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_style_context_add_class(gtk_widget_get_style_context(title), "astra-permission-title");
  gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

  const char *items[] = {
    "✓ Secure connection",
    "Cookies: limited",
    "Tracking protection: enabled",
    "Storage: isolated per site",
    "System access: ask every time",
    NULL
  };

  for (int i = 0; items[i] != NULL; i++) {
    GtkWidget *label = gtk_label_new(items[i]);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
  }

  gtk_widget_show_all(popover);
}

static void open_devtools(GtkMenuItem *item, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  WebKitWebView *web_view = active_web_view(self);
  if (web_view != NULL) webkit_web_inspector_show(webkit_web_view_get_inspector(web_view));
}

static void app_mode_action(GtkMenuItem *item, gpointer user_data) {
  AstraWindow *self = ASTRA_WINDOW(user_data);
  gboolean visible = gtk_widget_get_visible(self->nav_bar);
  gtk_widget_set_visible(self->nav_bar, !visible);
}


static void open_settings_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  astra_window_show_settings(ASTRA_WINDOW(user_data));
}

static void open_apps_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  astra_window_load_uri(ASTRA_WINDOW(user_data), "astra://apps");
}

static void open_bookmarks_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab == NULL) astra_window_add_tab(self, NULL, TRUE);
  astra_browser_view_show_bookmarks_dialog(self->active_tab->browser, GTK_WINDOW(self));
  update_window_for_active_tab(self);
}

static void bookmark_current_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab == NULL) return;
  astra_browser_view_bookmark_current_page(self->active_tab->browser);
  astra_browser_view_show_bookmarks_dialog(self->active_tab->browser, GTK_WINDOW(self));
  update_window_for_active_tab(self);
}

static void open_history_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  if (self->active_tab == NULL) astra_window_add_tab(self, NULL, TRUE);
  astra_browser_view_show_history_dialog(self->active_tab->browser, GTK_WINDOW(self));
  update_window_for_active_tab(self);
}

static void open_downloads_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  astra_downloads_show_dialog(GTK_WINDOW(self));
  update_window_for_active_tab(self);
}

static void new_private_window_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  GtkApplication *app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(self)));
  AstraWindow *private_window = astra_window_new_private(ASTRA_APPLICATION(app));
  astra_window_show_new_tab(private_window);
  gtk_widget_show_all(GTK_WIDGET(private_window));
  gtk_window_present(GTK_WINDOW(private_window));
}

static void install_webapp_action(GtkMenuItem *item, gpointer user_data) {
  (void)item;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  WebKitWebView *web_view = active_web_view(self);
  if (web_view == NULL) return;

  const char *uri = webkit_web_view_get_uri(web_view);
  const char *title = webkit_web_view_get_title(web_view);
  g_autofree char *app_uri = NULL;
  g_autoptr(GError) error = NULL;

  if (!astra_webapp_install_dialog(GTK_WINDOW(self), uri, title, &app_uri, &error)) {
    if (error != NULL) astra_show_error(GTK_WINDOW(self), "Could not install web app", error->message);
    return;
  }

  if (app_uri != NULL) astra_window_load_uri(self, app_uri);
}


static void settings_dialog_changed(gpointer user_data) {
  astra_window_apply_settings_to_all_tabs(ASTRA_WINDOW(user_data));
}

static void astra_window_show_settings(AstraWindow *self) {
  if (self->active_tab == NULL) astra_window_add_tab(self, NULL, TRUE);
  WebKitWebView *web_view = active_web_view(self);
  if (web_view == NULL) return;
  astra_settings_show_dialog(GTK_WINDOW(self), web_view, settings_dialog_changed, self);
  update_window_for_active_tab(self);
}

static void astra_window_apply_settings_to_all_tabs(AstraWindow *self) {
  astra_settings_apply_to_window(GTK_WIDGET(self));
  for (GList *l = self->tabs; l != NULL; l = l->next) {
    AstraTab *tab = l->data;
    WebKitWebView *web_view = astra_browser_view_get_web_view(tab->browser);
    astra_settings_apply_to_web_view(web_view);
  }
}

static void on_settings_changed(AstraBrowserView *view, gpointer user_data) {
  (void)view;
  astra_window_apply_settings_to_all_tabs(ASTRA_WINDOW(user_data));
}

static void on_open_uri_new_tab(AstraBrowserView *view, const char *uri, gpointer user_data) {
  (void)view;
  if (uri == NULL) return;
  astra_window_add_tab(ASTRA_WINDOW(user_data), uri, TRUE);
}

static void zoom_current_page(AstraWindow *self, double delta) {
  WebKitWebView *web_view = active_web_view(self);
  if (web_view == NULL) return;
  double level = webkit_web_view_get_zoom_level(web_view) + delta;
  if (level < 0.3) level = 0.3;
  if (level > 3.0) level = 3.0;
  webkit_web_view_set_zoom_level(web_view, level);
}

static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
  (void)widget;
  AstraWindow *self = ASTRA_WINDOW(user_data);
  WebKitWebView *web_view = active_web_view(self);
  guint state = event->state & gtk_accelerator_get_default_mod_mask();
  gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
  gboolean alt = (state & GDK_MOD1_MASK) != 0;

  if (ctrl && event->keyval == GDK_KEY_l) {
    gtk_widget_grab_focus(self->url_entry);
    gtk_editable_select_region(GTK_EDITABLE(self->url_entry), 0, -1);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_t) {
    astra_window_add_tab(self, NULL, TRUE);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_w) {
    if (self->active_tab != NULL) astra_window_close_tab(self, self->active_tab);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_comma) {
    astra_window_show_settings(self);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_d) {
    if (self->active_tab != NULL) astra_browser_view_bookmark_current_page(self->active_tab->browser);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_h) {
    open_history_action(NULL, self);
    return TRUE;
  }
  if (ctrl && event->keyval == GDK_KEY_j) {
    open_downloads_action(NULL, self);
    return TRUE;
  }
  if (ctrl && (state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_P) {
    new_private_window_action(NULL, self);
    return TRUE;
  }
  if ((ctrl && event->keyval == GDK_KEY_r) || event->keyval == GDK_KEY_F5) {
    if (self->active_tab != NULL) astra_browser_view_reload(self->active_tab->browser);
    return TRUE;
  }
  if (alt && event->keyval == GDK_KEY_Left) {
    if (self->active_tab != NULL && astra_browser_view_can_go_back(self->active_tab->browser)) {
      astra_browser_view_go_back(self->active_tab->browser);
    }
    return TRUE;
  }
  if (alt && event->keyval == GDK_KEY_Right) {
    if (self->active_tab != NULL && astra_browser_view_can_go_forward(self->active_tab->browser)) {
      astra_browser_view_go_forward(self->active_tab->browser);
    }
    return TRUE;
  }
  if (ctrl && (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal || event->keyval == GDK_KEY_KP_Add)) {
    zoom_current_page(self, 0.1);
    return TRUE;
  }
  if (ctrl && (event->keyval == GDK_KEY_minus || event->keyval == GDK_KEY_KP_Subtract)) {
    zoom_current_page(self, -0.1);
    return TRUE;
  }
  if (ctrl && (event->keyval == GDK_KEY_0 || event->keyval == GDK_KEY_KP_0)) {
    if (web_view != NULL) webkit_web_view_set_zoom_level(web_view, 1.0);
    return TRUE;
  }
  return FALSE;
}

static GtkWidget *make_icon_button(const char *label, const char *tooltip) {
  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_set_size_request(button, 36, 36);
  gtk_style_context_add_class(gtk_widget_get_style_context(button), "astra-flat-button");
  gtk_style_context_add_class(gtk_widget_get_style_context(button), "astra-icon-button");
  return button;
}

static GtkWidget *make_button(const char *label, const char *tooltip) {
  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_style_context_add_class(gtk_widget_get_style_context(button), "astra-flat-button");
  return button;
}

static GtkWidget *make_titlebar(AstraWindow *self) {
  GtkWidget *bar = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);
  gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(bar), FALSE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(bar), "");
  gtk_header_bar_set_subtitle(GTK_HEADER_BAR(bar), "");

  gtk_style_context_add_class(gtk_widget_get_style_context(bar), "astra-titlebar");

  /*
   * Firefox-style layout:
   * Use GtkHeaderBar's custom title area for the tab strip instead of packing
   * the strip as a start-side child. Start/end children in GtkHeaderBar keep
   * their natural width, so a packed-start tab strip only receives the first
   * chunk of the titlebar. The custom title area is the part GTK lets expand
   * between the window edges and native window controls.
   *
   * The native GtkWindow title is still updated elsewhere for the compositor,
   * task switcher, and accessibility, but no separate visual title is shown.
   */
  GtkWidget *title_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(title_content, TRUE);
  gtk_widget_set_halign(title_content, GTK_ALIGN_FILL);
  gtk_style_context_add_class(gtk_widget_get_style_context(title_content), "astra-titlebar-content");

  /*
   * Keep tabs in one horizontally scrollable strip.
   * The + button is packed into the strip itself, after the last tab, so it
   * scrolls with the tabs instead of drifting to the far edge of the titlebar.
   */
  self->tab_scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->tab_scroller),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_NEVER);
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(self->tab_scroller), TRUE);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(self->tab_scroller), 1);
  gtk_widget_set_size_request(self->tab_scroller, 1, 40);
  gtk_widget_set_hexpand(self->tab_scroller, TRUE);
  gtk_widget_set_halign(self->tab_scroller, GTK_ALIGN_FILL);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->tab_scroller), "astra-tab-scroller");

  self->tab_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_hexpand(self->tab_strip, FALSE);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->tab_strip), "astra-tab-strip");

  self->new_tab_button = gtk_button_new_with_label("+");
  gtk_button_set_relief(GTK_BUTTON(self->new_tab_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text(self->new_tab_button, "New tab");
  gtk_widget_set_size_request(self->new_tab_button, 34, 34);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->new_tab_button), "astra-new-tab-button");
  gtk_style_context_add_class(gtk_widget_get_style_context(self->new_tab_button), "astra-icon-button");
  g_signal_connect(self->new_tab_button, "clicked", G_CALLBACK(add_tab_clicked), self);

  gtk_box_pack_start(GTK_BOX(self->tab_strip), self->new_tab_button, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(self->tab_scroller), self->tab_strip);
  gtk_box_pack_start(GTK_BOX(title_content), self->tab_scroller, TRUE, TRUE, 0);

  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(bar), title_content);

  return bar;
}

static void astra_window_select_tab(AstraWindow *self, AstraTab *tab) {
  if (tab == NULL) return;
  self->active_tab = tab;
  gtk_stack_set_visible_child_name(GTK_STACK(self->browser_stack), tab->stack_name);
  update_window_for_active_tab(self);
}

static void astra_tab_free(AstraTab *tab) {
  if (tab == NULL) return;
  g_clear_pointer(&tab->stack_name, g_free);
  g_clear_pointer(&tab->title, g_free);
  g_free(tab);
}

static void astra_window_close_tab(AstraWindow *self, AstraTab *tab) {
  if (tab == NULL) return;

  if (g_list_length(self->tabs) == 1) {
    astra_browser_view_load_new_tab(tab->browser);
    gtk_label_set_text(GTK_LABEL(tab->tab_label), "New Tab");
    return;
  }

  GList *link = g_list_find(self->tabs, tab);
  AstraTab *next_tab = NULL;
  if (tab == self->active_tab) {
    if (link != NULL && link->next != NULL) next_tab = link->next->data;
    else if (link != NULL && link->prev != NULL) next_tab = link->prev->data;
  }

  self->tabs = g_list_remove(self->tabs, tab);
  gtk_container_remove(GTK_CONTAINER(self->tab_strip), tab->tab_box);
  gtk_container_remove(GTK_CONTAINER(self->browser_stack), GTK_WIDGET(tab->browser));

  if (tab == self->active_tab) self->active_tab = NULL;
  astra_tab_free(tab);

  if (next_tab != NULL) astra_window_select_tab(self, next_tab);
}

static void astra_window_add_tab(AstraWindow *self, const char *uri, gboolean switch_to_tab) {
  AstraTab *tab = g_new0(AstraTab, 1);
  tab->id = ++self->next_tab_id;
  tab->title = g_strdup("New Tab");
  tab->stack_name = g_strdup_printf("tab-%u", tab->id);
  tab->browser = astra_browser_view_new_private(self->private_mode);

  /*
   * Important: the whole tab must be one real clickable surface.
   *
   * Older revisions used a GtkToggleButton inside a GtkEventBox. In a
   * GtkHeaderBar custom-title area that made blank/internal tabs unreliable:
   * clicks on some parts of the tab were eaten by the child button/headerbar
   * interaction and selection could fail even though the tab was visible.
   *
   * Use a single GtkEventBox as the tab surface instead. The label is passive,
   * and only the close button is an active child. That makes New Tab,
   * Settings, file-manager, app://, and normal web tabs all select through the
   * same path, independent of WebKit's current URI or load state.
   */
  tab->tab_box = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(tab->tab_box), TRUE);
  gtk_widget_set_size_request(tab->tab_box, 220, 34);
  gtk_widget_set_hexpand(tab->tab_box, FALSE);
  gtk_widget_add_events(tab->tab_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  gtk_widget_set_tooltip_text(tab->tab_box, tab->title);
  gtk_style_context_add_class(gtk_widget_get_style_context(tab->tab_box), "astra-tab");
  g_signal_connect(tab->tab_box, "button-press-event", G_CALLBACK(tab_box_button_press), tab);

  GtkWidget *tab_inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_size_request(tab_inner, 220, 34);
  gtk_widget_set_hexpand(tab_inner, TRUE);
  gtk_container_add(GTK_CONTAINER(tab->tab_box), tab_inner);

  tab->tab_icon = gtk_image_new_from_icon_name("text-html-symbolic", GTK_ICON_SIZE_MENU);
  gtk_widget_set_size_request(tab->tab_icon, 16, 16);
  gtk_style_context_add_class(gtk_widget_get_style_context(tab->tab_icon), "astra-tab-favicon");

  tab->tab_spinner = gtk_spinner_new();
  gtk_widget_set_size_request(tab->tab_spinner, 16, 16);
  gtk_widget_hide(tab->tab_spinner);

  tab->tab_label = gtk_label_new(tab->title);
  gtk_label_set_xalign(GTK_LABEL(tab->tab_label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(tab->tab_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(tab->tab_label, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(tab->tab_label), "astra-tab-label");

  tab->close_button = gtk_button_new_with_label("×");
  gtk_button_set_relief(GTK_BUTTON(tab->close_button), GTK_RELIEF_NONE);
  gtk_widget_set_size_request(tab->close_button, 28, 28);
  gtk_widget_set_tooltip_text(tab->close_button, "Close tab");
  gtk_style_context_add_class(gtk_widget_get_style_context(tab->close_button), "astra-tab-close");
  gtk_style_context_add_class(gtk_widget_get_style_context(tab->close_button), "astra-icon-button");
  g_signal_connect(tab->close_button, "clicked", G_CALLBACK(tab_close_clicked), tab);

  gtk_box_pack_start(GTK_BOX(tab_inner), tab->tab_icon, FALSE, FALSE, 10);
  gtk_box_pack_start(GTK_BOX(tab_inner), tab->tab_spinner, FALSE, FALSE, 10);
  gtk_box_pack_start(GTK_BOX(tab_inner), tab->tab_label, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(tab_inner), tab->close_button, FALSE, FALSE, 4);

  /* Keep the + button directly after the last tab. */
  gtk_box_pack_start(GTK_BOX(self->tab_strip), tab->tab_box, FALSE, FALSE, 0);
  gtk_box_reorder_child(GTK_BOX(self->tab_strip), self->new_tab_button, -1);

  gtk_stack_add_named(GTK_STACK(self->browser_stack), GTK_WIDGET(tab->browser), tab->stack_name);

  WebKitWebView *web_view = astra_browser_view_get_web_view(tab->browser);
  g_signal_connect(web_view, "permission-request", G_CALLBACK(permission_request), self);
  g_signal_connect(web_view, "notify::favicon", G_CALLBACK(on_favicon_changed), self);
  g_signal_connect(web_view, "notify::estimated-load-progress", G_CALLBACK(on_progress_changed), self);
  g_signal_connect(tab->browser, "astra-uri-changed", G_CALLBACK(on_uri_changed), self);
  g_signal_connect(tab->browser, "astra-title-changed", G_CALLBACK(on_title_changed), self);
  g_signal_connect(tab->browser, "astra-load-changed", G_CALLBACK(on_load_changed), self);
  g_signal_connect(tab->browser, "astra-settings-changed", G_CALLBACK(on_settings_changed), self);
  g_signal_connect(tab->browser, "astra-open-uri-new-tab", G_CALLBACK(on_open_uri_new_tab), self);

  self->tabs = g_list_append(self->tabs, tab);
  gtk_widget_show_all(tab->tab_box);
  gtk_widget_show_all(GTK_WIDGET(tab->browser));
  gtk_widget_show_all(self->browser_stack);

  /*
   * Select first, then load. Internal pages like astra://new-tab emit their
   * virtual URI/title synchronously from load_internal_html(). If the old tab
   * is still active during that emit, the new tab becomes visible but the
   * chrome/active state belongs to the previous tab, which is the bug that made
   * blank New Tab pages look unselectable.
   */
  if (switch_to_tab || self->active_tab == NULL) astra_window_select_tab(self, tab);

  if (uri != NULL && *uri != '\0') astra_browser_view_load_uri(tab->browser, uri);
  else astra_browser_view_load_new_tab(tab->browser);

  if (tab == self->active_tab) update_window_for_active_tab(self);
}

static void astra_window_init(AstraWindow *self) {
  gtk_window_set_default_size(GTK_WINDOW(self), 1180, 760);
  g_signal_connect(self, "key-press-event", G_CALLBACK(key_press_event), self);
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "astra-window");
  astra_settings_apply_to_window(GTK_WIDGET(self));

  self->titlebar = make_titlebar(self);
  gtk_window_set_titlebar(GTK_WINDOW(self), self->titlebar);

  self->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(self), self->main_box);

  self->nav_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->nav_bar), "astra-header");
  gtk_box_pack_start(GTK_BOX(self->main_box), self->nav_bar, FALSE, FALSE, 0);

  self->progress_bar = gtk_progress_bar_new();
  gtk_widget_set_no_show_all(self->progress_bar, TRUE);
  gtk_widget_hide(self->progress_bar);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->progress_bar), "astra-load-progress");
  gtk_box_pack_start(GTK_BOX(self->main_box), self->progress_bar, FALSE, FALSE, 0);

  self->back_button = make_icon_button("‹", "Back");
  self->forward_button = make_icon_button("›", "Forward");
  self->reload_button = make_icon_button("⟳", "Reload");
  g_signal_connect(self->back_button, "clicked", G_CALLBACK(go_back), self);
  g_signal_connect(self->forward_button, "clicked", G_CALLBACK(go_forward), self);
  g_signal_connect(self->reload_button, "clicked", G_CALLBACK(reload_page), self);

  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->back_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->forward_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->reload_button, FALSE, FALSE, 0);

  self->url_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(self->url_entry), "Search or enter address");
  gtk_style_context_add_class(gtk_widget_get_style_context(self->url_entry), "astra-url-entry");
  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->url_entry, TRUE, TRUE, 8);
  g_signal_connect(self->url_entry, "activate", G_CALLBACK(url_entry_activate), self);

  self->shield_button = make_icon_button("🛡", "Site privacy and permissions");
  g_signal_connect(self->shield_button, "clicked", G_CALLBACK(show_shield_panel), self);
  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->shield_button, FALSE, FALSE, 0);

  self->menu_button = gtk_menu_button_new();
  gtk_button_set_label(GTK_BUTTON(self->menu_button), "☰");
  gtk_button_set_relief(GTK_BUTTON(self->menu_button), GTK_RELIEF_NONE);
  gtk_widget_set_tooltip_text(self->menu_button, "Astra menu");
  gtk_widget_set_size_request(self->menu_button, 36, 36);
  gtk_style_context_add_class(gtk_widget_get_style_context(self->menu_button), "astra-flat-button");
  gtk_style_context_add_class(gtk_widget_get_style_context(self->menu_button), "astra-icon-button");
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *new_tab = gtk_menu_item_new_with_label("New Tab");
  GtkWidget *private_window = gtk_menu_item_new_with_label("New Private Window");
  GtkWidget *bookmark_page = gtk_menu_item_new_with_label("Bookmark This Page");
  GtkWidget *bookmarks = gtk_menu_item_new_with_label("Bookmarks");
  GtkWidget *history = gtk_menu_item_new_with_label("History");
  GtkWidget *downloads = gtk_menu_item_new_with_label("Downloads");
  GtkWidget *settings = gtk_menu_item_new_with_label("Settings");
  GtkWidget *apps = gtk_menu_item_new_with_label("Installed Web Apps");
  GtkWidget *install_app = gtk_menu_item_new_with_label("Install Site as Web App…");
  GtkWidget *app_mode = gtk_menu_item_new_with_label("Toggle App Mode");
  GtkWidget *inspector = gtk_menu_item_new_with_label("Astra Inspector");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_tab);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), private_window);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), bookmark_page);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), bookmarks);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), history);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), downloads);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), apps);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), install_app);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), app_mode);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), inspector);
  gtk_widget_show_all(menu);
  gtk_menu_button_set_popup(GTK_MENU_BUTTON(self->menu_button), menu);
  g_signal_connect(new_tab, "activate", G_CALLBACK(add_tab_clicked), self);
  g_signal_connect(private_window, "activate", G_CALLBACK(new_private_window_action), self);
  g_signal_connect(bookmark_page, "activate", G_CALLBACK(bookmark_current_action), self);
  g_signal_connect(bookmarks, "activate", G_CALLBACK(open_bookmarks_action), self);
  g_signal_connect(history, "activate", G_CALLBACK(open_history_action), self);
  g_signal_connect(downloads, "activate", G_CALLBACK(open_downloads_action), self);
  g_signal_connect(settings, "activate", G_CALLBACK(open_settings_action), self);
  g_signal_connect(apps, "activate", G_CALLBACK(open_apps_action), self);
  g_signal_connect(install_app, "activate", G_CALLBACK(install_webapp_action), self);
  g_signal_connect(app_mode, "activate", G_CALLBACK(app_mode_action), self);
  g_signal_connect(inspector, "activate", G_CALLBACK(open_devtools), self);
  gtk_box_pack_start(GTK_BOX(self->nav_bar), self->menu_button, FALSE, FALSE, 0);

  self->content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->main_box), self->content_box, TRUE, TRUE, 0);

  self->browser_stack = gtk_stack_new();
  gtk_widget_set_hexpand(self->browser_stack, TRUE);
  gtk_widget_set_vexpand(self->browser_stack, TRUE);
  gtk_box_pack_start(GTK_BOX(self->content_box), self->browser_stack, TRUE, TRUE, 0);

  WebKitWebContext *context = webkit_web_context_get_default();
  astra_webapp_init(context);
  astra_downloads_attach(context, GTK_WINDOW(self));

  update_nav_buttons(self);
}

static void astra_window_class_init(AstraWindowClass *klass) {}

AstraWindow *astra_window_new(AstraApplication *app) {
  return g_object_new(ASTRA_TYPE_WINDOW, "application", app, NULL);
}

AstraWindow *astra_window_new_private(AstraApplication *app) {
  AstraWindow *window = g_object_new(ASTRA_TYPE_WINDOW, "application", app, NULL);
  window->private_mode = TRUE;
  gtk_window_set_title(GTK_WINDOW(window), "Astra Private Window");
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(window)), "astra-private-window");
  return window;
}

void astra_window_load_uri(AstraWindow *window, const char *uri) {
  if (uri != NULL && g_str_has_prefix(uri, "astra://settings")) {
    astra_window_show_settings(window);
    return;
  }
  if (uri != NULL && g_str_has_prefix(uri, "astra://bookmarks")) {
    open_bookmarks_action(NULL, window);
    return;
  }
  if (uri != NULL && g_str_has_prefix(uri, "astra://history")) {
    open_history_action(NULL, window);
    return;
  }
  if (uri != NULL && g_str_has_prefix(uri, "astra://downloads")) {
    open_downloads_action(NULL, window);
    return;
  }
  if (window->active_tab == NULL) astra_window_add_tab(window, uri, TRUE);
  else astra_browser_view_load_uri(window->active_tab->browser, uri);
}

void astra_window_show_new_tab(AstraWindow *window) {
  astra_window_add_tab(window, NULL, TRUE);
}
