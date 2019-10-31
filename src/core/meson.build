eegdev_sources = files(
    'configuration.h',
    'confparser.h',
    'core.c',
    'coreinternals.h',
    'device-helper.c',
    'eegdev-pluginapi.h',
    'eegdev.h',
    'opendev.c',
    'sensortypes.c',
    'typecast.c',
    )

corelib_state = 'disabled'
if not get_option('corelib-build').disabled()
    flex = find_program('flex',
        required : get_option('corelib-build').enabled(),
    )
    bison = find_program('bison',
        required : get_option('corelib-build').enabled(),
    )

    if flex.found() and bison.found()
        corelib_state = 'enabled'
        run_command(bison, files('configuration.y'))
        run_command(flex, files('configuration.l'))
        eegdev_sources += files(
            'configuration.lex.c',
            'configuration.lex.h',
            'configuration.tab.c',
            'configuration.tab.h',
        )

        eegdev = shared_library('eegdev',
            eegdev_sources,
            include_directories : includes,
            dependencies : [mmlib],
            install : true,
        )
        if get_option('tests')
            eegdev_static = static_library('eegdev_static',
                eegdev_sources,
                include_directories : includes,
                dependencies : [mmlib],
                install : false,
            )
        endif

        import('pkgconfig').generate(libraries : eegdev,
            version : meson.project_version(),
            name : meson.project_name(),
            description : '''eegdev is a library that provides
                          an interface for accessing various
                          biosignal acquisition systems.''',
        )

        headers = files(
            'eegdev.h',
            'eegdev-pluginapi.h',
        )
        install_headers(headers)
    endif
endif
if corelib_state == 'disabled'
    eegdev = cc.find_library('eegdev', required : true)
endif
