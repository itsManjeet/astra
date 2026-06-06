#include "astra/astra-settings.h"

#include <string.h>

static AstraSettingsPrefs prefs = {
  ASTRA_THEME_SYSTEM,
  TRUE,
  TRUE,
  TRUE,
  TRUE,
  TRUE,
  TRUE,
  NULL,
  NULL,
  NULL,
  16,
  13,
  0
};

static const char *theme_to_string(AstraThemeMode mode) {
  switch (mode) {
    case ASTRA_THEME_LIGHT: return "light";
    case ASTRA_THEME_DARK: return "dark";
    case ASTRA_THEME_SYSTEM:
    default: return "system";
  }
}

const AstraSettingsPrefs *astra_settings_get_prefs(void) { return &prefs; }

void astra_settings_set_theme(AstraThemeMode mode) { prefs.theme_mode = mode; }

void astra_settings_set_boolean(const char *key, gboolean value) {
  if (g_strcmp0(key, "enable-javascript") == 0 || g_strcmp0(key, "javascript") == 0) prefs.enable_javascript = value;
  else if (g_strcmp0(key, "auto-load-images") == 0 || g_strcmp0(key, "images") == 0) prefs.auto_load_images = value;
  else if (g_strcmp0(key, "enable-smooth-scrolling") == 0 || g_strcmp0(key, "smooth-scrolling") == 0) prefs.enable_smooth_scrolling = value;
  else if (g_strcmp0(key, "enable-page-cache") == 0 || g_strcmp0(key, "page-cache") == 0) prefs.enable_page_cache = value;
  else if (g_strcmp0(key, "enable-html5-local-storage") == 0 || g_strcmp0(key, "local-storage") == 0) prefs.enable_local_storage = value;
  else if (g_strcmp0(key, "enable-developer-extras") == 0 || g_strcmp0(key, "developer-extras") == 0) prefs.enable_developer_extras = value;
}

void astra_settings_set_font_family(const char *key, const char *value) {
  if (value == NULL) value = "";
  if (g_strcmp0(key, "serif-font-family") == 0) {
    g_free(prefs.serif_font_family); prefs.serif_font_family = g_strdup(value);
  } else if (g_strcmp0(key, "sans-serif-font-family") == 0) {
    g_free(prefs.sans_serif_font_family); prefs.sans_serif_font_family = g_strdup(value);
  } else if (g_strcmp0(key, "monospace-font-family") == 0) {
    g_free(prefs.monospace_font_family); prefs.monospace_font_family = g_strdup(value);
  }
}

void astra_settings_set_font_size(const char *key, gint value) {
  if (value < 0) value = 0;
  if (g_strcmp0(key, "default-font-size") == 0) prefs.default_font_size = value;
  else if (g_strcmp0(key, "default-monospace-font-size") == 0) prefs.default_monospace_font_size = value;
  else if (g_strcmp0(key, "minimum-font-size") == 0) prefs.minimum_font_size = value;
}

static gboolean message_value_is_true(const char *message) {
  return g_str_has_suffix(message, ":true") || g_str_has_suffix(message, ":1") || g_str_has_suffix(message, ":on");
}

void astra_settings_set_from_message(const char *message) {
  if (message == NULL) return;
  if (g_str_has_prefix(message, "theme:")) {
    const char *value = message + strlen("theme:");
    if (g_strcmp0(value, "dark") == 0) prefs.theme_mode = ASTRA_THEME_DARK;
    else if (g_strcmp0(value, "light") == 0) prefs.theme_mode = ASTRA_THEME_LIGHT;
    else prefs.theme_mode = ASTRA_THEME_SYSTEM;
    return;
  }
  if (g_str_has_prefix(message, "javascript:")) astra_settings_set_boolean("javascript", message_value_is_true(message));
  else if (g_str_has_prefix(message, "images:")) astra_settings_set_boolean("images", message_value_is_true(message));
  else if (g_str_has_prefix(message, "smooth-scrolling:")) astra_settings_set_boolean("smooth-scrolling", message_value_is_true(message));
  else if (g_str_has_prefix(message, "page-cache:")) astra_settings_set_boolean("page-cache", message_value_is_true(message));
  else if (g_str_has_prefix(message, "local-storage:")) astra_settings_set_boolean("local-storage", message_value_is_true(message));
  else if (g_str_has_prefix(message, "developer-extras:")) astra_settings_set_boolean("developer-extras", message_value_is_true(message));
}

static void safe_set_bool(WebKitSettings *settings, const char *name, gboolean value) {
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), name) != NULL) g_object_set(settings, name, value, NULL);
}
static void safe_set_int(WebKitSettings *settings, const char *name, gint value) {
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), name) != NULL) g_object_set(settings, name, value, NULL);
}
static void safe_set_string(WebKitSettings *settings, const char *name, const char *value) {
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), name) != NULL) g_object_set(settings, name, value, NULL);
}

void astra_settings_apply_to_context(WebKitWebContext *context) {
  webkit_web_context_set_process_model(context, WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
  webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_WEB_BROWSER);
}

void astra_settings_apply_to_web_view(WebKitWebView *web_view) {
  const AstraSettingsPrefs *p = astra_settings_get_prefs();
  WebKitSettings *settings = webkit_web_view_get_settings(web_view);
  safe_set_bool(settings, "enable-developer-extras", p->enable_developer_extras);
  safe_set_bool(settings, "enable-html5-local-storage", p->enable_local_storage);
  safe_set_bool(settings, "enable-page-cache", p->enable_page_cache);
  safe_set_bool(settings, "enable-smooth-scrolling", p->enable_smooth_scrolling);
  safe_set_bool(settings, "enable-javascript", p->enable_javascript);
  safe_set_bool(settings, "auto-load-images", p->auto_load_images);
  safe_set_bool(settings, "allow-file-access-from-file-urls", FALSE);
  safe_set_bool(settings, "allow-universal-access-from-file-urls", FALSE);
  safe_set_string(settings, "serif-font-family", p->serif_font_family != NULL ? p->serif_font_family : "serif");
  safe_set_string(settings, "sans-serif-font-family", p->sans_serif_font_family != NULL ? p->sans_serif_font_family : "system-ui");
  safe_set_string(settings, "monospace-font-family", p->monospace_font_family != NULL ? p->monospace_font_family : "monospace");
  safe_set_int(settings, "default-font-size", p->default_font_size);
  safe_set_int(settings, "default-monospace-font-size", p->default_monospace_font_size);
  safe_set_int(settings, "minimum-font-size", p->minimum_font_size);
}

void astra_settings_apply_to_window(GtkWidget *window) {
  GtkSettings *gtk_settings = gtk_settings_get_default();
  GtkStyleContext *ctx = gtk_widget_get_style_context(window);
  gtk_style_context_remove_class(ctx, "astra-light");
  gtk_style_context_remove_class(ctx, "astra-dark");
  gtk_style_context_remove_class(ctx, "astra-system");
  if (prefs.theme_mode == ASTRA_THEME_DARK) {
    gtk_style_context_add_class(ctx, "astra-dark");
    if (gtk_settings != NULL) g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
  } else if (prefs.theme_mode == ASTRA_THEME_LIGHT) {
    gtk_style_context_add_class(ctx, "astra-light");
    if (gtk_settings != NULL) g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
  } else {
    gtk_style_context_add_class(ctx, "astra-system");
  }
}

static GtkWidget *label_left(const char *text, const char *klass) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  if (klass != NULL) gtk_style_context_add_class(gtk_widget_get_style_context(label), klass);
  return label;
}

static GtkWidget *section_card(GtkWidget *parent, const char *title, const char *subtitle) {
  GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_style_context_add_class(gtk_widget_get_style_context(card), "astra-settings-card");
  gtk_box_pack_start(GTK_BOX(parent), card, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(card), label_left(title, "astra-settings-card-title"), FALSE, FALSE, 0);
  if (subtitle != NULL) gtk_box_pack_start(GTK_BOX(card), label_left(subtitle, "astra-settings-card-subtitle"), FALSE, FALSE, 0);
  return card;
}

static void emit_changed(GtkWidget *widget) {
  AstraSettingsChangedFunc cb = g_object_get_data(G_OBJECT(widget), "astra-changed-cb");
  gpointer data = g_object_get_data(G_OBJECT(widget), "astra-changed-data");
  if (cb != NULL) cb(data);
}

static void theme_changed(GtkComboBoxText *combo, gpointer user_data) {
  (void)user_data;
  const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
  if (g_strcmp0(id, "dark") == 0) astra_settings_set_theme(ASTRA_THEME_DARK);
  else if (g_strcmp0(id, "light") == 0) astra_settings_set_theme(ASTRA_THEME_LIGHT);
  else astra_settings_set_theme(ASTRA_THEME_SYSTEM);
  emit_changed(GTK_WIDGET(combo));
}

static GtkWidget *theme_combo(AstraSettingsChangedFunc cb, gpointer data) {
  GtkWidget *combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "system", "System");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "light", "Light");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "dark", "Dark");
  gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), theme_to_string(prefs.theme_mode));
  g_object_set_data(G_OBJECT(combo), "astra-changed-cb", cb);
  g_object_set_data(G_OBJECT(combo), "astra-changed-data", data);
  g_signal_connect(combo, "changed", G_CALLBACK(theme_changed), NULL);
  return combo;
}

static GtkWidget *row(GtkWidget *card, const char *name, const char *hint, GtkWidget *control) {
  GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_style_context_add_class(gtk_widget_get_style_context(r), "astra-settings-row");
  GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start(GTK_BOX(texts), label_left(name, "astra-settings-row-title"), FALSE, FALSE, 0);
  if (hint != NULL) gtk_box_pack_start(GTK_BOX(texts), label_left(hint, "astra-settings-row-hint"), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(r), texts, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(r), control, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(card), r, FALSE, FALSE, 0);
  return r;
}

static void bool_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  const char *prop = g_object_get_data(G_OBJECT(sw), "webkit-prop");
  WebKitSettings *settings = user_data;
  gboolean value = gtk_switch_get_active(sw);
  if (prop != NULL) {
    g_object_set(settings, prop, value, NULL);
    astra_settings_set_boolean(prop, value);
  }
  emit_changed(GTK_WIDGET(sw));
}

static GtkWidget *switch_for(WebKitSettings *settings, const char *prop, AstraSettingsChangedFunc cb, gpointer data) {
  gboolean value = FALSE;
  g_object_get(settings, prop, &value, NULL);
  GtkWidget *sw = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(sw), value);
  g_object_set_data_full(G_OBJECT(sw), "webkit-prop", g_strdup(prop), g_free);
  g_object_set_data(G_OBJECT(sw), "astra-changed-cb", cb);
  g_object_set_data(G_OBJECT(sw), "astra-changed-data", data);
  g_signal_connect(sw, "notify::active", G_CALLBACK(bool_toggled), settings);
  return sw;
}

static void int_changed(GtkSpinButton *spin, gpointer user_data) {
  WebKitSettings *settings = user_data;
  const char *prop = g_object_get_data(G_OBJECT(spin), "webkit-prop");
  gint value = gtk_spin_button_get_value_as_int(spin);
  if (prop != NULL) {
    g_object_set(settings, prop, value, NULL);
    astra_settings_set_font_size(prop, value);
  }
  emit_changed(GTK_WIDGET(spin));
}

static GtkWidget *spin_for(WebKitSettings *settings, const char *prop, gint min, gint max, AstraSettingsChangedFunc cb, gpointer data) {
  gint value = 0;
  g_object_get(settings, prop, &value, NULL);
  GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
  g_object_set_data_full(G_OBJECT(spin), "webkit-prop", g_strdup(prop), g_free);
  g_object_set_data(G_OBJECT(spin), "astra-changed-cb", cb);
  g_object_set_data(G_OBJECT(spin), "astra-changed-data", data);
  g_signal_connect(spin, "value-changed", G_CALLBACK(int_changed), settings);
  return spin;
}

static void string_changed(GtkEntry *entry, gpointer user_data) {
  WebKitSettings *settings = user_data;
  const char *prop = g_object_get_data(G_OBJECT(entry), "webkit-prop");
  const char *value = gtk_entry_get_text(entry);
  if (prop != NULL) {
    g_object_set(settings, prop, value, NULL);
    astra_settings_set_font_family(prop, value);
  }
  emit_changed(GTK_WIDGET(entry));
}

static gboolean string_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
  (void)event;
  string_changed(GTK_ENTRY(entry), user_data);
  return FALSE;
}

static GtkWidget *entry_for(WebKitSettings *settings, const char *prop, AstraSettingsChangedFunc cb, gpointer data) {
  g_autofree char *value = NULL;
  g_object_get(settings, prop, &value, NULL);
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entry), value != NULL ? value : "");
  gtk_widget_set_size_request(entry, 220, -1);
  g_object_set_data_full(G_OBJECT(entry), "webkit-prop", g_strdup(prop), g_free);
  g_object_set_data(G_OBJECT(entry), "astra-changed-cb", cb);
  g_object_set_data(G_OBJECT(entry), "astra-changed-data", data);
  g_signal_connect(entry, "activate", G_CALLBACK(string_changed), settings);
  g_signal_connect(entry, "focus-out-event", G_CALLBACK(string_focus_out), settings);
  return entry;
}

static char *pretty_prop_name(const char *name) {
  char *copy = g_strdup(name);
  for (char *p = copy; p && *p; p++) if (*p == '-') *p = ' ';
  if (copy && *copy) copy[0] = g_ascii_toupper(copy[0]);
  return copy;
}

static gboolean property_is_readwrite(GObjectClass *klass, const char *name) {
  GParamSpec *pspec = g_object_class_find_property(klass, name);
  return pspec != NULL &&
         (pspec->flags & G_PARAM_READABLE) != 0 &&
         (pspec->flags & G_PARAM_WRITABLE) != 0;
}

static gboolean pspec_is_readwrite(GParamSpec *pspec) {
  return pspec != NULL &&
         (pspec->flags & G_PARAM_READABLE) != 0 &&
         (pspec->flags & G_PARAM_WRITABLE) != 0;
}

static GtkWidget *settings_content_page(const char *title, const char *subtitle, GtkWidget **out_content) {
  GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_style_context_add_class(gtk_widget_get_style_context(scroller), "astra-settings-content-scroller");

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_hexpand(box, TRUE);
  gtk_container_set_border_width(GTK_CONTAINER(box), 32);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroller), box);

  gtk_box_pack_start(GTK_BOX(box), label_left(title, "astra-settings-title"), FALSE, FALSE, 0);
  if (subtitle != NULL && *subtitle != '\0') {
    gtk_box_pack_start(GTK_BOX(box), label_left(subtitle, "astra-settings-subtitle"), FALSE, FALSE, 0);
  }

  if (out_content != NULL) *out_content = box;
  return scroller;
}

static GtkWidget *settings_nav_row(const char *title, const char *icon, const char *page_name) {
  GtkWidget *roww = gtk_list_box_row_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(roww), "astra-settings-nav-row");
  g_object_set_data_full(G_OBJECT(roww), "astra-page-name", g_strdup(page_name), g_free);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_container_set_border_width(GTK_CONTAINER(box), 10);
  gtk_container_add(GTK_CONTAINER(roww), box);

  GtkWidget *icon_label = gtk_label_new(icon != NULL ? icon : "•");
  gtk_style_context_add_class(gtk_widget_get_style_context(icon_label), "astra-settings-nav-icon");
  gtk_box_pack_start(GTK_BOX(box), icon_label, FALSE, FALSE, 0);

  GtkWidget *label = label_left(title, "astra-settings-nav-label");
  gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
  return roww;
}

static void settings_nav_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  (void)box;
  if (row == NULL) return;
  const char *page_name = g_object_get_data(G_OBJECT(row), "astra-page-name");
  if (page_name != NULL) {
    gtk_stack_set_visible_child_name(GTK_STACK(user_data), page_name);
  }
}

static void add_stack_page(GtkWidget *stack, GtkWidget *page, const char *name, const char *title) {
  gtk_stack_add_titled(GTK_STACK(stack), page, name, title);
}

GtkWidget *astra_settings_build_gtk_page(WebKitWebView *web_view,
                                         AstraSettingsChangedFunc changed_cb,
                                         gpointer user_data) {
  WebKitSettings *settings = webkit_web_view_get_settings(web_view);
  astra_settings_apply_to_web_view(web_view);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(root, TRUE);
  gtk_widget_set_vexpand(root, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(root), "astra-settings-page");

  GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_size_request(sidebar, 250, -1);
  gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "astra-settings-sidebar");
  gtk_box_pack_start(GTK_BOX(root), sidebar, FALSE, FALSE, 0);

  GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start(GTK_BOX(sidebar), brand, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(brand), label_left("Astra", "astra-settings-brand"), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(brand), label_left("Preferences", "astra-settings-brand-subtitle"), FALSE, FALSE, 0);

  GtkWidget *nav = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
  gtk_style_context_add_class(gtk_widget_get_style_context(nav), "astra-settings-nav-list");
  gtk_box_pack_start(GTK_BOX(sidebar), nav, FALSE, FALSE, 0);

  GtkWidget *stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_transition_duration(GTK_STACK(stack), 160);
  gtk_widget_set_hexpand(stack, TRUE);
  gtk_widget_set_vexpand(stack, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(stack), "astra-settings-stack");
  gtk_box_pack_start(GTK_BOX(root), stack, TRUE, TRUE, 0);

  struct NavItem { const char *title; const char *icon; const char *name; } nav_items[] = {
    { "General", "⌂", "general" },
    { "Appearance", "◐", "appearance" },
    { "Fonts", "Aa", "fonts" },
    { "WebKit", "⚙", "webkit" },
    { "Advanced", "⋯", "advanced" },
    { "Shortcuts", "⌘", "shortcuts" },
    { NULL, NULL, NULL }
  };
  for (int i = 0; nav_items[i].title != NULL; i++) {
    gtk_list_box_insert(GTK_LIST_BOX(nav), settings_nav_row(nav_items[i].title, nav_items[i].icon, nav_items[i].name), -1);
  }
  g_signal_connect(nav, "row-selected", G_CALLBACK(settings_nav_selected), stack);

  GtkWidget *content = NULL;

  GtkWidget *general_page = settings_content_page("General", "Basic browser behavior and engine defaults.", &content);
  GtkWidget *general = section_card(content, "Browsing", "Settings here affect the active profile and are applied to all open tabs.");
  row(general, "JavaScript", "Allow websites to run JavaScript.", switch_for(settings, "enable-javascript", changed_cb, user_data));
  row(general, "Images", "Load webpage images automatically.", switch_for(settings, "auto-load-images", changed_cb, user_data));
  row(general, "Local storage", "Allow websites to store local browser data.", switch_for(settings, "enable-html5-local-storage", changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "enable-page-cache")) {
    row(general, "Page cache", "Keep recently visited pages warm for faster back/forward navigation.", switch_for(settings, "enable-page-cache", changed_cb, user_data));
  }
  add_stack_page(stack, general_page, "general", "General");

  GtkWidget *appearance_page = settings_content_page("Appearance", "Control how Astra follows the desktop and how pages feel.", &content);
  GtkWidget *shell = section_card(content, "Browser chrome", "These options affect Astra windows and dialogs.");
  row(shell, "Theme", "Use the system theme or force light/dark mode.", theme_combo(changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "enable-smooth-scrolling")) {
    row(shell, "Smooth scrolling", "Use animated page scrolling where WebKit supports it.", switch_for(settings, "enable-smooth-scrolling", changed_cb, user_data));
  }
  GtkWidget *developer = section_card(content, "Developer", "Useful while building Avyos web apps and the browser runtime.");
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "enable-developer-extras")) {
    row(developer, "Inspector", "Enable WebKit developer tools.", switch_for(settings, "enable-developer-extras", changed_cb, user_data));
  }
  add_stack_page(stack, appearance_page, "appearance", "Appearance");

  GtkWidget *fonts_page = settings_content_page("Fonts", "Configure the fonts WebKit uses when webpages do not choose their own.", &content);
  GtkWidget *families = section_card(content, "Font families", "Leave blank to let WebKit or the system choose defaults.");
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "sans-serif-font-family")) row(families, "Sans serif", "Used for most webpage text.", entry_for(settings, "sans-serif-font-family", changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "serif-font-family")) row(families, "Serif", "Used when pages request serif text.", entry_for(settings, "serif-font-family", changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "monospace-font-family")) row(families, "Monospace", "Used for code and fixed-width text.", entry_for(settings, "monospace-font-family", changed_cb, user_data));
  GtkWidget *sizes = section_card(content, "Font sizes", "Pixel sizes passed directly to WebKitSettings.");
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "default-font-size")) row(sizes, "Default size", "Default proportional font size.", spin_for(settings, "default-font-size", 8, 40, changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "default-monospace-font-size")) row(sizes, "Monospace size", "Default fixed-width font size.", spin_for(settings, "default-monospace-font-size", 8, 40, changed_cb, user_data));
  if (property_is_readwrite(G_OBJECT_GET_CLASS(settings), "minimum-font-size")) row(sizes, "Minimum size", "Prevents unreadably tiny text.", spin_for(settings, "minimum-font-size", 0, 32, changed_cb, user_data));
  add_stack_page(stack, fonts_page, "fonts", "Fonts");

  GtkWidget *webkit_page = settings_content_page("WebKit", "Common WebKitGTK feature toggles exposed by this installed build.", &content);
  GtkWidget *engine = section_card(content, "Engine features", "Only settings supported by your WebKitGTK version are shown.");
  const char *common[] = { "enable-javascript", "auto-load-images", "enable-smooth-scrolling", "enable-page-cache", "enable-html5-local-storage", "enable-developer-extras", "enable-webgl", "enable-media-stream", "enable-mediasource", "enable-site-specific-quirks", "enable-dns-prefetching", "enable-webaudio", "enable-accelerated-2d-canvas", NULL };
  for (int i = 0; common[i] != NULL; i++) {
    if (!property_is_readwrite(G_OBJECT_GET_CLASS(settings), common[i])) continue;
    g_autofree char *pretty = pretty_prop_name(common[i]);
    row(engine, pretty, common[i], switch_for(settings, common[i], changed_cb, user_data));
  }
  add_stack_page(stack, webkit_page, "webkit", "WebKit");

  GtkWidget *advanced_page = settings_content_page("Advanced", "All remaining writable WebKitSettings properties discovered at runtime.", &content);
  GtkWidget *advanced_bool = section_card(content, "Boolean settings", "Advanced flags from WebKitSettings.");
  GtkWidget *advanced_values = section_card(content, "String and integer settings", "Advanced values from WebKitSettings.");
  const char *known_props[] = { "enable-javascript", "auto-load-images", "enable-smooth-scrolling", "enable-page-cache", "enable-html5-local-storage", "enable-developer-extras", "enable-webgl", "enable-media-stream", "enable-mediasource", "enable-site-specific-quirks", "enable-dns-prefetching", "enable-webaudio", "enable-accelerated-2d-canvas", "sans-serif-font-family", "serif-font-family", "monospace-font-family", "default-font-size", "default-monospace-font-size", "minimum-font-size", NULL };
  guint n_props = 0;
  GParamSpec **props = g_object_class_list_properties(G_OBJECT_GET_CLASS(settings), &n_props);
  guint bool_count = 0;
  guint value_count = 0;
  for (guint i = 0; i < n_props; i++) {
    if (!pspec_is_readwrite(props[i])) continue;
    const char *name = props[i]->name;
    gboolean skip = FALSE;
    for (int j = 0; known_props[j] != NULL; j++) if (g_strcmp0(name, known_props[j]) == 0) skip = TRUE;
    if (skip) continue;
    g_autofree char *pretty = pretty_prop_name(name);
    if (props[i]->value_type == G_TYPE_BOOLEAN) {
      row(advanced_bool, pretty, name, switch_for(settings, name, changed_cb, user_data));
      bool_count++;
    } else if (props[i]->value_type == G_TYPE_INT) {
      GParamSpecInt *ip = G_PARAM_SPEC_INT(props[i]);
      row(advanced_values, pretty, name, spin_for(settings, name, MAX(ip->minimum, -10000), MIN(ip->maximum, 10000), changed_cb, user_data));
      value_count++;
    } else if (props[i]->value_type == G_TYPE_STRING) {
      row(advanced_values, pretty, name, entry_for(settings, name, changed_cb, user_data));
      value_count++;
    }
  }
  g_free(props);
  if (bool_count == 0) row(advanced_bool, "No extra boolean settings", "Your WebKitGTK build exposes no additional writable boolean properties.", label_left("—", "astra-shortcut-label"));
  if (value_count == 0) row(advanced_values, "No extra value settings", "Your WebKitGTK build exposes no additional writable string/integer properties.", label_left("—", "astra-shortcut-label"));
  add_stack_page(stack, advanced_page, "advanced", "Advanced");

  GtkWidget *shortcuts_page = settings_content_page("Shortcuts", "Keyboard shortcuts built into the GTK window.", &content);
  GtkWidget *navigation = section_card(content, "Navigation", NULL);
  row(navigation, "Back / Forward", "Move through tab history.", label_left("Alt ←   Alt →", "astra-shortcut-label"));
  row(navigation, "Reload", "Reload current page.", label_left("Ctrl R   F5", "astra-shortcut-label"));
  row(navigation, "Address bar", "Focus the URL/search entry.", label_left("Ctrl L", "astra-shortcut-label"));
  GtkWidget *tabs = section_card(content, "Tabs and browser tools", NULL);
  row(tabs, "New / close tab", "Create or close the active tab.", label_left("Ctrl T   Ctrl W", "astra-shortcut-label"));
  row(tabs, "Zoom", "Zoom the current webpage.", label_left("Ctrl +   Ctrl -   Ctrl 0", "astra-shortcut-label"));
  row(tabs, "Settings / History / Downloads", "Open native browser dialogs.", label_left("Ctrl ,   Ctrl H   Ctrl J", "astra-shortcut-label"));
  add_stack_page(stack, shortcuts_page, "shortcuts", "Shortcuts");

  GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav), 0);
  if (first != NULL) gtk_list_box_select_row(GTK_LIST_BOX(nav), first);
  gtk_stack_set_visible_child_name(GTK_STACK(stack), "general");

  return root;
}

char *astra_settings_build_page_html(void) {
  return g_strdup("<!doctype html><meta charset='utf-8'><title>Settings</title><body>Settings are rendered by native GTK in this build.</body>");
}

void astra_settings_show_dialog(GtkWindow *parent,
                                WebKitWebView *web_view,
                                AstraSettingsChangedFunc changed_cb,
                                gpointer user_data) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings",
                                                  parent,
                                                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  "Close", GTK_RESPONSE_CLOSE,
                                                  NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 920, 680);

  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *page = astra_settings_build_gtk_page(web_view, changed_cb, user_data);
  gtk_widget_set_hexpand(page, TRUE);
  gtk_widget_set_vexpand(page, TRUE);
  gtk_container_add(GTK_CONTAINER(area), page);

  gtk_widget_show_all(dialog);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}
