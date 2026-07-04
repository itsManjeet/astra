#include "library.h"

#include "app.h"
#include "browser.h"

#include <string.h>

typedef struct {
    BrowserWindow *browser;
    gboolean bookmarks;
    GtkWidget *page;
    GtkWidget *list;
} LibraryPage;

typedef struct {
    BrowserWindow *browser;
    GtkWidget *page;
    gboolean bookmarks;
    gchar *uri;
} LibraryAction;

static void library_refresh(LibraryPage *state);

static void library_action_free(gpointer data) {
    LibraryAction *action = data;
    if (!action)
        return;
    g_free(action->uri);
    g_free(action);
}

static GtkWidget *icon_button(const gchar *icon, const gchar *tooltip) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "flat");
    return button;
}

static GPtrArray *library_array(App *app, gboolean bookmarks) {
    return bookmarks ? app->bookmarks : app->history;
}

static const gchar *library_title(gboolean bookmarks) {
    return bookmarks ? "Bookmarks" : "History";
}

static const gchar *library_icon(gboolean bookmarks) {
    return bookmarks ? "user-bookmarks-symbolic" : "document-open-recent-symbolic";
}

static void open_uri_action(GtkButton *button, gpointer data) {
    (void)button;
    LibraryAction *action = data;
    if (!action->uri || !*action->uri)
        return;
    browser_tab_new(action->browser, action->uri, TRUE);
    browser_update_tab_visibility(action->browser);
}

static void remove_uri_from_array(GPtrArray *array, const gchar *uri) {
    for (guint i = 0; i < array->len; i++) {
        if (g_strcmp0(g_ptr_array_index(array, i), uri) == 0) {
            g_ptr_array_remove_index(array, i);
            return;
        }
    }
}

static void remove_uri_action(GtkButton *button, gpointer data) {
    (void)button;
    LibraryAction *action = data;
    App *app = action->browser->app;
    remove_uri_from_array(library_array(app, action->bookmarks), action->uri);
    if (!action->bookmarks)
        app_rebuild_completion(app);
    app_save(app);

    LibraryPage *state = g_object_get_data(G_OBJECT(action->page),
                                           "astra-library-state");
    if (state)
        library_refresh(state);
}

static void clear_all_action(GtkButton *button, gpointer data) {
    (void)button;
    LibraryPage *state = data;
    App *app = state->browser->app;
    g_ptr_array_set_size(library_array(app, state->bookmarks), 0);
    if (!state->bookmarks)
        app_rebuild_completion(app);
    app_save(app);
    library_refresh(state);
}

static void copy_uri_action(GtkButton *button, gpointer data) {
    LibraryAction *action = data;
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, action->uri, -1);
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Copied");
}

static LibraryAction *make_action(LibraryPage *state, const gchar *uri) {
    LibraryAction *action = g_new0(LibraryAction, 1);
    action->browser = state->browser;
    action->page = state->page;
    action->bookmarks = state->bookmarks;
    action->uri = g_strdup(uri);
    return action;
}

static GtkWidget *action_button(const gchar *icon, const gchar *tooltip,
                                GCallback callback, LibraryAction *action) {
    GtkWidget *button = icon_button(icon, tooltip);
    g_object_set_data_full(G_OBJECT(button), "astra-library-action", action,
                           library_action_free);
    g_signal_connect(button, "clicked", callback, action);
    return button;
}

static GtkWidget *library_row(LibraryPage *state, const gchar *uri) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 7);
    gtk_widget_set_margin_bottom(box, 7);

    gtk_box_pack_start(GTK_BOX(box),
        gtk_image_new_from_icon_name(library_icon(state->bookmarks),
                                     GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new(uri);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box), action_button("document-open-symbolic",
        "Open", G_CALLBACK(open_uri_action), make_action(state, uri)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), action_button("edit-copy-symbolic",
        "Copy URL", G_CALLBACK(copy_uri_action), make_action(state, uri)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), action_button("edit-delete-symbolic",
        "Remove", G_CALLBACK(remove_uri_action), make_action(state, uri)),
        FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(row), box);
    return row;
}

static void list_remove_all(GtkWidget *container) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(children);
}

static void library_refresh(LibraryPage *state) {
    list_remove_all(state->list);
    GPtrArray *array = library_array(state->browser->app, state->bookmarks);

    if (!array || array->len == 0) {
        gchar *text = g_strdup_printf("No %s yet",
                                      state->bookmarks ? "bookmarks" : "history");
        GtkWidget *empty = gtk_label_new(text);
        gtk_widget_set_margin_top(empty, 36);
        gtk_widget_set_margin_bottom(empty, 36);
        gtk_style_context_add_class(gtk_widget_get_style_context(empty),
                                    "dim-label");
        gtk_container_add(GTK_CONTAINER(state->list), empty);
        g_free(text);
        gtk_widget_show_all(state->list);
        return;
    }

    for (gint i = (gint)array->len - 1; i >= 0; i--)
        gtk_container_add(GTK_CONTAINER(state->list),
                          library_row(state, g_ptr_array_index(array, i)));
    gtk_widget_show_all(state->list);
}

static void library_page_free(gpointer data) {
    g_free(data);
}

static GtkWidget *build_library_page(BrowserWindow *browser, gboolean bookmarks) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(root, 24);
    gtk_widget_set_margin_end(root, 24);
    gtk_widget_set_margin_top(root, 18);
    gtk_widget_set_margin_bottom(root, 18);

    LibraryPage *state = g_new0(LibraryPage, 1);
    state->browser = browser;
    state->bookmarks = bookmarks;
    state->page = root;

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>",
                                    library_title(bookmarks));
    gtk_label_set_markup(GTK_LABEL(title), markup);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
    GtkWidget *clear = gtk_button_new_with_label(bookmarks ? "Clear bookmarks" : "Clear history");
    gtk_style_context_add_class(gtk_widget_get_style_context(clear),
                                "destructive-action");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_all_action), state);
    gtk_box_pack_start(GTK_BOX(header), clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 8);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    state->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), state->list);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

    g_object_set_data_full(G_OBJECT(root), "astra-library-state", state,
                           library_page_free);
    g_object_set_data(G_OBJECT(root), "astra-library-page", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(root), "astra-library-bookmarks",
                      GINT_TO_POINTER(bookmarks));
    g_object_set_data(G_OBJECT(root), "astra-page-title",
                      (gpointer)library_title(bookmarks));
    g_object_set_data(G_OBJECT(root), "astra-page-icon",
                      (gpointer)library_icon(bookmarks));

    library_refresh(state);
    g_free(markup);
    return root;
}

static void close_library(GtkButton *button, gpointer data) {
    BrowserWindow *browser = data;
    GtkWidget *page_widget = g_object_get_data(G_OBJECT(button),
                                               "astra-page-widget");
    gint page = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), page_widget);
    if (page >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(browser->notebook), page);
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook)) == 0)
        gtk_widget_destroy(browser->window);
    else
        browser_update_tab_visibility(browser);
}

static void library_open(BrowserWindow *browser, gboolean bookmarks) {
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(browser->notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(browser->notebook), i);
        if (g_object_get_data(G_OBJECT(page), "astra-library-page") &&
            GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page),
                                             "astra-library-bookmarks")) == bookmarks) {
            LibraryPage *state = g_object_get_data(G_OBJECT(page),
                                                   "astra-library-state");
            if (state)
                library_refresh(state);
            gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), i);
            return;
        }
    }

    GtkWidget *page = build_library_page(browser, bookmarks);
    GtkWidget *label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(label),
        gtk_image_new_from_icon_name(library_icon(bookmarks), GTK_ICON_SIZE_MENU),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new(library_title(bookmarks)),
                       FALSE, FALSE, 0);
    GtkWidget *close = icon_button("window-close-symbolic", "Close");
    g_object_set_data(G_OBJECT(close), "astra-page-widget", page);
    g_signal_connect(close, "clicked", G_CALLBACK(close_library), browser);
    gtk_box_pack_start(GTK_BOX(label), close, FALSE, FALSE, 0);
    gtk_widget_show_all(label);

    gint index = gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook),
                                          page, label);
    gtk_widget_show_all(page);
    browser_update_tab_visibility(browser);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), index);
}

void library_open_bookmarks(BrowserWindow *browser) {
    library_open(browser, TRUE);
}

void library_open_history(BrowserWindow *browser) {
    library_open(browser, FALSE);
}
