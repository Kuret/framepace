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
  dependencies : [cc.find_library('m')],
  build_by_default : false)
test('color encoding', test_color)

test_game = executable('test_game', 'tests/test_game.c', 'src/game.c', 'src/gate.c', 'src/util.c',
  c_args : ['-D_GNU_SOURCE'], dependencies : [thread_dep],
  build_by_default : false)
test('game key and settings', test_game)

test_limiter = executable('test_limiter', 'tests/test_limiter.c', 'src/limiter.c', 'src/util.c',
  c_args : ['-D_GNU_SOURCE'], dependencies : [thread_dep],
  build_by_default : false)
test('limiter pace', test_limiter)

test('adaptive policy', find_program('python3'), args : [files('tests/test_adapt.py')])

# Manual HDR swapchain harness (needs a Wayland session): `ninja -C build hdr_swapchain`.
# Not built by default, and skipped when the Wayland development files are missing.
wl_client = dependency('wayland-client', required : false)
wl_protocols = dependency('wayland-protocols', required : false)
wl_scanner = find_program('wayland-scanner', required : false)
vulkan_lib = cc.find_library('vulkan', required : false)
if wl_client.found() and wl_protocols.found() and wl_scanner.found() and vulkan_lib.found()
  xdg_xml = wl_protocols.get_variable('pkgdatadir') / 'stable' / 'xdg-shell' / 'xdg-shell.xml'
  xdg_code = custom_target('xdg-shell-protocol-c',
    input : xdg_xml, output : 'xdg-shell-protocol.c',
    command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  xdg_header = custom_target('xdg-shell-client-protocol-h',
    input : xdg_xml, output : 'xdg-shell-client-protocol.h',
    command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  executable('hdr_swapchain', 'tests/hdr_swapchain.c', xdg_code, xdg_header,
    dependencies : [wl_client, vulkan_lib],
    build_by_default : false)
endif
