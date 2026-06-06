gtk_dep = dependency('gtk+-3.0', version: '>=3.24')
webkit_dep = dependency('webkit2gtk-4.1', version: '>=2.40', required: false)
if not webkit_dep.found()
  webkit_dep = dependency('webkit2gtk-4.0', version: '>=2.36')
endif

srcs = [
  'main.c',
  'astra-application.c',
  'astra-window.c',
  'astra-browser-view.c',
  'astra-permissions.c',
  'astra-downloads.c',
  'astra-settings.c',
  'astra-file-manager.c',
  'astra-webapp.c',
  'astra-utils.c',
]

inc = include_directories('../include', '..')

executable(
  'astra',
  srcs + astra_resources,
  include_directories: inc,
  dependencies: [gtk_dep, webkit_dep],
  install: true,
)
