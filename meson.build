project(
  'voxbender',
  'c',
  version: '0.8',
  license: 'GPL',
  default_options: ['c_std=gnu99']
)

cc = meson.get_compiler('c')

math = cc.find_library('m')
gl = cc.find_library('GL')
glew = cc.find_library('GLEW')
glu = cc.find_library('GLU')
sdl = cc.find_library('SDL2')

voxbender_dependencies = [math,gl,glew,sdl]

voxbender_version = '"@0@"'.format(meson.project_version())
pkg_datadir = join_paths(get_option('prefix'), get_option('datadir')) / 'voxbender'
add_project_arguments('-DPKG_DATADIR="' + pkg_datadir + '"',
		      '-DVOXBENDER_VERSION=@0@'.format(voxbender_version),
		      language: 'c')
add_project_arguments('-Wno-unused-but-set-variable', language: 'c')

if get_option('buildtype') == 'debug'
    add_project_arguments('-DDEBUG',language: 'c')
endif

voxbender_inc = include_directories(
	'/usr/include',
	'/usr/include/GL',
	'/usr/include/SDL2',
	'src/voxbender',
	'src/mt_core',
	'src/mt_math')

com_sources = ['src/voxbender/main.c',
	       'src/voxbender/ku_gl_shader.c',
	       'src/voxbender/ku_gl_floatbuffer.c',
	       'src/mt_core/mt_log.c',
	       'src/mt_core/mt_time.c',
	       'src/mt_core/mt_memory.c',
	       'src/mt_math/mt_vector_2d.c',
	       'src/mt_math/mt_vector_3d.c',
	       'src/mt_math/mt_vector_4d.c',
	       'src/mt_math/mt_math_3d.c',
	       'src/mt_math/mt_matrix_4d.c'
	       ]

executable(
  'voxbender',
  com_sources,
  include_directories: [voxbender_inc],
  dependencies: voxbender_dependencies,
  install: true,
)