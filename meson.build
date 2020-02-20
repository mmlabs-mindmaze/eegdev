project('eegdev', 'c',
    version : '1.0',
    license : 'LGPL-3.0',
    meson_version: '>= 0.49'
)

cc = meson.get_compiler('c')

config = configuration_data()

# MMLibrary is needed in core and modules
mmlib = cc.find_library('mmlib', required : true)

# fix pthread-win32 header mess
config.set('CONFIG_H', true)

includes = include_directories('src/core', './')

# define HAVE_CONFIG_H with compiler command line to include the generated
# config.h file (same as autotools)
add_project_arguments('-DHAVE_CONFIG_H', language : 'c')

subdir('config/api-exports')
subdir('config/autotools-compat')
subdir('src/plugins')
subdir('src/core')
subdir('doc')
if get_option('tests')
    subdir('tests')
endif
subdir('devtools')

config.set_quoted('PKGSYSCONFDIR', get_option('prefix') / get_option('sysconfdir') / meson.project_name())
config.set_quoted('PKGLIBDIR', get_option('prefix') / get_option('libdir') / meson.project_name())

message('Configuration summary')
message('=====================')
message('    Core library build : @0@'.format(corelib_state))
message('    --------------------------')
message('    EEG file support : @0@'.format(xdf_state))
message('    Biosemi support  : @0@'.format(biosemi_state))
message('    gTec support     : @0@ (loaded at runtime: @1@)'.format(gtec_state, dlopen_gusbamp))
message('    Neurosky support : @0@'.format(neurosky_state))
message('    TobiIA support   : @0@'.format(tobiia_state))

configure_file(output : 'config.h', configuration : config)
