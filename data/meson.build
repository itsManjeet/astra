astra_resources = gnome.compile_resources(
  'astra-resources',
  'astra.gresource.xml',
  source_dir: meson.current_source_dir(),
  c_name: 'astra',
)

install_data(
  'dev.avyos.Astra.desktop',
  install_dir: join_paths(datadir, 'applications'),
)

install_data(
  'dev.avyos.Astra.svg',
  install_dir: join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps'),
)
