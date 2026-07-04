project(
  'astra',
  'c',
  version: '0.1.0',
  license: 'NOASSERTION',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

app_id = 'dev.avyos.Astra'
app_name = 'Astra'
webkit_api = get_option('webkitgtk_api')

cc = meson.get_compiler('c')

gtk_dep = dependency('gtk+-3.0', version: '>=3.24')
webkit_dep = dependency('webkit2gtk-' + webkit_api, version: '>=2.38')

common_c_args = [
  '-D_GNU_SOURCE',
  '-DASTRA_VERSION="@0@"'.format(meson.project_version()),
]

foreach arg : ['-Wformat=2', '-Wshadow', '-Wundef', '-Wstrict-prototypes', '-Wmissing-prototypes']
  if cc.has_argument(arg)
    common_c_args += arg
  endif
endforeach

sources = files(
  'src/astra.c',
  'src/app.c',
  'src/browser.c',
  'src/downloads.c',
  'src/file_page.c',
  'src/library.c',
  'src/pwa.c',
  'src/service_scheme.c',
  'src/settings.c',
)

executable(
  'astra',
  sources,
  include_directories: include_directories('src'),
  dependencies: [gtk_dep, webkit_dep],
  c_args: common_c_args,
  install: true,
)

conf = configuration_data()
conf.set('APP_ID', app_id)
conf.set('APP_NAME', app_name)
conf.set('VERSION', meson.project_version())
conf.set('BINDIR', get_option('prefix') / get_option('bindir'))

configure_file(
  input: 'data/dev.avyos.Astra.desktop.in',
  output: 'dev.avyos.Astra.desktop',
  configuration: conf,
  install: true,
  install_dir: get_option('datadir') / 'applications',
)

configure_file(
  input: 'data/dev.avyos.Astra.metainfo.xml.in',
  output: 'dev.avyos.Astra.metainfo.xml',
  configuration: conf,
  install: true,
  install_dir: get_option('datadir') / 'metainfo',
)

install_data(
  'data/icons/hicolor/scalable/apps/dev.avyos.Astra.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

meson.add_install_script('meson/postinstall.py')
