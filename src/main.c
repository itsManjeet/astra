#include "config.h"
#include "astra/astra-application.h"

int main(int argc, char **argv) {
  g_autoptr(AstraApplication) app = astra_application_new();
  return g_application_run(G_APPLICATION(app), argc, argv);
}
