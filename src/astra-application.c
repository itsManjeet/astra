#include "config.h"
#include "astra/astra-application.h"
#include "astra/astra-window.h"
#include "astra/astra-utils.h"

struct _AstraApplication {
  GtkApplication parent_instance;
};

G_DEFINE_TYPE(AstraApplication, astra_application, GTK_TYPE_APPLICATION)

static void astra_application_activate(GApplication *application) {
  AstraWindow *window = astra_window_new(ASTRA_APPLICATION(application));
  astra_window_show_new_tab(window);
  gtk_widget_show_all(GTK_WIDGET(window));
  gtk_window_present(GTK_WINDOW(window));
}

static void astra_application_open(GApplication *application,
                                   GFile **files,
                                   gint n_files,
                                   const gchar *hint) {
  AstraWindow *window = astra_window_new(ASTRA_APPLICATION(application));

  if (n_files > 0) {
    g_autofree char *uri = g_file_get_uri(files[0]);
    astra_window_load_uri(window, uri);
  } else {
    astra_window_show_new_tab(window);
  }

  gtk_widget_show_all(GTK_WIDGET(window));
  gtk_window_present(GTK_WINDOW(window));
}

static int astra_application_command_line(GApplication *application,
                                          GApplicationCommandLine *command_line) {
  gchar **argv = g_application_command_line_get_arguments(command_line, NULL);
  AstraWindow *window = astra_window_new(ASTRA_APPLICATION(application));

  if (argv[1] != NULL) {
    g_autofree char *uri = astra_uri_normalize(argv[1]);
    astra_window_load_uri(window, uri);
  } else {
    astra_window_show_new_tab(window);
  }

  g_strfreev(argv);
  gtk_widget_show_all(GTK_WIDGET(window));
  gtk_window_present(GTK_WINDOW(window));
  return 0;
}

static void astra_application_startup(GApplication *application) {
  G_APPLICATION_CLASS(astra_application_parent_class)->startup(application);
  astra_load_css();
}

static void astra_application_class_init(AstraApplicationClass *klass) {
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);
  app_class->startup = astra_application_startup;
  app_class->activate = astra_application_activate;
  app_class->open = astra_application_open;
  app_class->command_line = astra_application_command_line;
}

static void astra_application_init(AstraApplication *self) {}

AstraApplication *astra_application_new(void) {
  return g_object_new(
    ASTRA_TYPE_APPLICATION,
    "application-id", ASTRA_APP_ID,
    "flags", G_APPLICATION_HANDLES_OPEN | G_APPLICATION_HANDLES_COMMAND_LINE,
    NULL
  );
}
