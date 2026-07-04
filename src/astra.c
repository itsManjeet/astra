#include "app.h"
#include "browser.h"
#include "pwa.h"

#include <string.h>

static void install_css(void) {
    const gchar *css =
        "progressbar { min-height: 2px; }"
        "progressbar trough { min-height: 2px; background: transparent; border: 0; }"
        "progressbar progress { min-height: 2px; }"
        ".dim-label { opacity: 0.65; font-size: 0.9em; }";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

typedef struct {
    gboolean app_mode;
    gboolean show_help;
    gboolean show_version;
    gchar *uri;
    gchar *name;
    gchar *app_id;
} LaunchOptions;

static void ensure_ui_initialized(App *app) {
    static gboolean ui_initialized = FALSE;
    if (ui_initialized)
        return;

    g_set_application_name(APP_NAME);
    gtk_window_set_default_icon_name(APP_ID);

    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme",
                 app->config.dark_ui, NULL);
    install_css();
    ui_initialized = TRUE;
}

static void launch_options_clear(LaunchOptions *options) {
    g_free(options->uri);
    g_free(options->name);
    g_free(options->app_id);
    memset(options, 0, sizeof(*options));
}

static void parse_launch_options(LaunchOptions *options, int argc, char **argv) {
    memset(options, 0, sizeof(*options));

    for (int i = 1; i < argc; i++) {
        const gchar *arg = argv[i];
        if (g_str_has_prefix(arg, "--app=")) {
            g_free(options->uri);
            options->uri = g_strdup(arg + strlen("--app="));
            options->app_mode = TRUE;
        } else if (g_strcmp0(arg, "--app") == 0) {
            options->app_mode = TRUE;
            if (i + 1 < argc) {
                g_free(options->uri);
                options->uri = g_strdup(argv[++i]);
            }
        } else if (g_str_has_prefix(arg, "--name=")) {
            g_free(options->name);
            options->name = g_strdup(arg + strlen("--name="));
        } else if (g_strcmp0(arg, "--name") == 0) {
            if (i + 1 < argc) {
                g_free(options->name);
                options->name = g_strdup(argv[++i]);
            }
        } else if (g_str_has_prefix(arg, "--app-id=")) {
            g_free(options->app_id);
            options->app_id = g_strdup(arg + strlen("--app-id="));
        } else if (g_strcmp0(arg, "--app-id") == 0) {
            if (i + 1 < argc) {
                g_free(options->app_id);
                options->app_id = g_strdup(argv[++i]);
            }
        } else if (g_strcmp0(arg, "--help") == 0 || g_strcmp0(arg, "-h") == 0) {
            options->show_help = TRUE;
        } else if (g_strcmp0(arg, "--version") == 0 || g_strcmp0(arg, "-v") == 0) {
            options->show_version = TRUE;
        } else if (!g_str_has_prefix(arg, "-") && !options->uri) {
            options->uri = g_strdup(arg);
        }
    }
}


static gchar *startup_application_id(int argc, char **argv) {
    LaunchOptions options;
    parse_launch_options(&options, argc, argv);

    gchar *id = NULL;
    if (options.app_mode) {
        gchar *name = options.name && *options.name
            ? g_strdup(options.name)
            : pwa_default_name_for_uri(options.uri, NULL);
        id = pwa_runtime_app_id(options.uri, name, options.app_id);
        g_free(name);
    }

    if (!id || !g_application_id_is_valid(id)) {
        g_free(id);
        id = g_strdup(APP_ID);
    }

    launch_options_clear(&options);
    return id;
}

static void open_window_for_launch(App *app, const LaunchOptions *options) {
    ensure_ui_initialized(app);

    gboolean app_mode = options->app_mode;
    gchar *uri = options->uri && *options->uri
        ? g_strdup(options->uri)
        : g_strdup(app->config.homepage);
    gchar *name = NULL;

    if (app_mode)
        name = options->name && *options->name
            ? g_strdup(options->name)
            : pwa_default_name_for_uri(uri, NULL);

    gchar *effective_app_id = app_mode
        ? pwa_runtime_app_id(uri, name, options->app_id)
        : NULL;

    BrowserWindow *browser = browser_window_new(app, FALSE, app_mode,
                                                name, effective_app_id);
    browser_tab_new(browser, uri, TRUE);
    gtk_widget_show_all(browser->window);
    browser_update_tab_visibility(browser);
    gtk_window_present(GTK_WINDOW(browser->window));

    g_free(effective_app_id);
    g_free(name);
    g_free(uri);
}

static void print_help(GApplicationCommandLine *command_line) {
    g_application_command_line_print(command_line,
        "%s %s\n\n"
        "Usage:\n"
        "  astra [URI]\n"
        "  astra --app URI [--name NAME] [--app-id ID]\n"
        "  astra --version\n\n"
        "Options:\n"
        "  --app URI       Open URI in standalone web-app mode\n"
        "  --name NAME     Window/app name for --app\n"
        "  --app-id ID     Stable installed web-app identifier\n"
        "  --version       Print version and exit\n"
        "  --help          Show this help and exit\n",
        APP_NAME, ASTRA_VERSION);
}

static int command_line(GApplication *application,
                        GApplicationCommandLine *command_line,
                        gpointer data) {
    (void)application;
    App *app = data;
    int argc = 0;
    gchar **argv = g_application_command_line_get_arguments(command_line, &argc);

    LaunchOptions options;
    parse_launch_options(&options, argc, argv);

    if (options.show_help) {
        print_help(command_line);
    } else if (options.show_version) {
        g_application_command_line_print(command_line, "%s %s\n", APP_NAME, ASTRA_VERSION);
    } else {
        open_window_for_launch(app, &options);
    }

    launch_options_clear(&options);
    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv) {
    gchar *runtime_app_id = startup_application_id(argc, argv);

    g_set_prgname(runtime_app_id);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_set_program_class(runtime_app_id);
    G_GNUC_END_IGNORE_DEPRECATIONS

    App *app = g_new0(App, 1);
    app_load(app);

    app->application = gtk_application_new(runtime_app_id,
                                           G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app->application, "command-line",
                     G_CALLBACK(command_line), app);

    int status = g_application_run(G_APPLICATION(app->application), argc, argv);
    app_save(app);
    g_object_unref(app->application);
    app_clear(app);
    g_free(app);
    g_free(runtime_app_id);
    return status;
}
