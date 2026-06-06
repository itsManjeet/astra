project(
  'astra-browser',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

gnome = import('gnome')

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
  ]),
  language: 'c',
)

prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
app_id = 'dev.avyos.Astra'

config_h = configuration_data()
config_h.set_quoted('ASTRA_APP_ID', app_id)
config_h.set_quoted('ASTRA_VERSION', meson.project_version())
config_h.set_quoted('ASTRA_DATADIR', datadir)
configure_file(output: 'config.h', configuration: config_h)

subdir('data')
subdir('src')
