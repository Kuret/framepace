project('framepace', 'c',
  version : '0.1.0',
  license : 'MIT',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    'prefix=/usr',
    'libdir=lib',
  ])

cc = meson.get_compiler('c')
thread_dep = dependency('threads')
if not cc.has_header('vulkan/vk_layer.h')
  error('vulkan-headers (vulkan/vk_layer.h) is required')
endif

layer_sources = files(
  'src/layer.c',
  'src/limiter.c',
  'src/state.c',
  'src/util.c',
)

layer = shared_library('VkLayer_framepace',
  layer_sources,
  c_args : ['-fvisibility=hidden', '-D_GNU_SOURCE'],
  dependencies : [thread_dep],
  name_prefix : 'lib',
  install : true,
  install_dir : get_option('prefix') / 'lib' / 'framepace')

# Installed manifest: absolute library_path, mirrors MangoHud's layout so that
# Steam's pressure-vessel imports it and remaps the host path.

lib_install_dir = get_option('prefix') / 'lib' / 'framepace'
manifest_name = 'framepace.x86_64.json'

install_conf = configuration_data()
install_conf.set('LIBRARY_PATH', lib_install_dir / 'libVkLayer_framepace.so')
configure_file(
  input : 'manifest/framepace.json.in',
  output : manifest_name,
  configuration : install_conf,
  install : true,
  install_dir : get_option('prefix') / 'share' / 'vulkan' / 'implicit_layer.d')

# Development manifest pointing at the build output, for
# VK_ADD_IMPLICIT_LAYER_PATH=<builddir>/dev before installing.
subdir('dev')
