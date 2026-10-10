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

glslang = find_program('glslangValidator')
shader_headers = []
foreach s : [['overlay.vert', 'fp_vert_spv'], ['overlay.frag', 'fp_frag_spv']]
  shader_headers += custom_target(s[1],
    input : 'shaders' / s[0],
    output : s[1] + '.h',
    command : [glslang, '-V', '--vn', s[1], '-o', '@OUTPUT@', '@INPUT@'])
endforeach

layer_sources = files(
  'src/color.c',
  'src/game.c',
  'src/gate.c',
  'src/layer.c',
  'src/limiter.c',
  'src/overlay.c',
  'src/panel.c',
  'src/relay.c',
  'src/state.c',
  'src/util.c',
)

layer = shared_library('VkLayer_framepace',
  layer_sources, shader_headers,
  include_directories : include_directories('.'),
  c_args : ['-fvisibility=hidden', '-D_GNU_SOURCE'],
  dependencies : [thread_dep, cc.find_library('m')],
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

# Unit tests: `meson test -C build`
test_color = executable('test_color', 'tests/test_color.c', 'src/color.c',
  dependencies : [cc.find_library('m')])
test('color encoding', test_color)
