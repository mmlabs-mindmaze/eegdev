install_subdir('./', install_dir : get_option('datadir')+'/doc/eegdev/examples')

if get_option('corelib-build').enabled()
    saw = shared_module('saw',
        files('plugin/saw.c'),
        include_directories : includes,
        name_prefix : '',
        dependencies : [mmlib],
    )
    eegdev_acq = executable('eegdev_acq',
        files('library-usage/eegdev_acq.c'),
        include_directories : includes,
        dependencies : [mmlib],
        link_with : [eegdev],
    )
    test_env = environment()
    test_env.set('EEGDEV_PLUGINS_DIR', '@0@/doc/examples'.format(meson.build_root()))
    test_env.set('PATH', '@0@:$PATH'.format(meson.current_build_dir()))
    test_env.set('CORELIB_PATH', '@0@/src/core'.format(meson.build_root()))
    test('testexamples', files('testexamples.sh'), env : test_env)

    if get_option('xdf').enabled()
        # we check if the example compiles
        recinxdf = executable('recinxdf',
            files('library-usage/recinxdf.c',
                'library-usage/acquisition.c',
                'library-usage/acquisition.h'),
            include_directories : includes,
            dependencies : [mmlib, xdffileio],
            link_with : [eegdev],
        )
    endif
endif