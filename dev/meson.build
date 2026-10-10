dev_conf = configuration_data()
dev_conf.set('LIBRARY_PATH', meson.global_build_root() / 'libVkLayer_framepace.so')
configure_file(
  input : '../manifest/framepace.json.in',
  output : 'framepace.x86_64.json',
  configuration : dev_conf)
