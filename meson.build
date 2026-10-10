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
)

layer = shared_library('VkLayer_framepace',
  layer_sources,
  c_args : ['-fvisibility=hidden', '-D_GNU_SOURCE'],
  dependencies : [thread_dep],
  name_prefix : 'lib',
  install : true,
  install_dir : get_option('prefix') / 'lib' / 'framepace')
