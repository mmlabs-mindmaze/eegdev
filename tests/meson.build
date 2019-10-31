test_env = environment()
test_env.set('EEGDEV_PLUGINS_DIR', '@0@/src/plugins'.format(meson.build_root()))
test_env.set('EEGDEV_CONF_DIR', '@0@/tests/conffiles'.format(meson.source_root()))
test_env.append('PATH', meson.current_build_dir())
test_env.set('builddir', meson.current_build_dir())
test_env.append('LD_LIBRARY_PATH', meson.current_build_dir())

verifycast = executable('verifycast',
    files('verifycast.c'),
    include_directories : includes,
    dependencies : [mmlib],
    link_with : [eegdev_static],
)
verifysplit = executable('verifysplit',
    files('verifycast.c'),
    include_directories : includes,
    dependencies : [mmlib],
    link_with : [eegdev_static],
)

test('verifycast', files('verify-cast.sh'), env : test_env)
test('verifysplit', verifysplit)


if xdf_state == 'enabled'
    syseegfile = executable('syseegfile',
        files('syseegfile.c'),
        include_directories : includes,
        dependencies : [mmlib, xdffileio],
        link_with : [eegdev],
    )
    test('syseegfile', syseegfile, env : test_env)
endif


if host_machine.system() != 'windows'
    if biosemi_state == 'enabled'
        usb_flags = usb.partial_dependency(compile_args : true,
            link_args : false,
            links : false,
            includes : true,
        )
        fakeact2 = shared_library('fakeact2',
            files('fakelibs/activetwo.c', 'fakelibs/fakeact2.h'),
            include_directories : includes,
            dependencies : [mmlib, usb_flags],
        )
        sysbiosemi = executable('sysbiosemi',
            files('sysbiosemi.c'),
            include_directories : includes,
            dependencies : [mmlib],
            link_with : [eegdev],
        )
        bio_env = test_env
        # The following is needed to overwrite the loading of usb library for
        # biosemi plugin. meson links with -Wl,--as-needed flag which is NOT
        # possible to overwrite with its counterpart (link_with :
        # '-Wl,--no-as-needed').
        bio_env.set('LD_PRELOAD', fakeact2.full_path())
        test('testbiosemi', files('testfakeact2.sh'), env : bio_env)
    endif


    if gtec_state != 'disabled'
        fakegtec = shared_library('gusbampapi',
            files('fakelibs/gusbamp.c', 'fakelibs/fakegtec.h'),
            include_directories : includes,
            dependencies : [mmlib, threads],
        )
        sysgtec = executable('sysgtec',
            files('sysgtec.c'),
            include_directories : includes,
            dependencies : [mmlib, threads],
            link_with : [eegdev],
        )
        test('testgtec', files('testfakegtec.sh'), env : test_env)
    endif


    if neurosky_state == 'enabled'
        sysneurosky = executable('neurosky',
            files('sysneurosky.c'),
            include_directories : includes,
            dependencies : [mmlib, blue],
            link_with : [eegdev],
        )
        test('sysneurosky', sysneurosky, env : test_env)
    endif


    if tobiia_state == 'enabled'
        faketia = static_library('faketia',
            files('fakelibs/tia-server.c', 'fakelibs/tia-server.h'),
            include_directories : includes,
            dependencies : [mmlib, xml],
        )
        systobiia = executable('systobiia',
            files('systobiia.c'),
            include_directories : includes,
            dependencies : [mmlib, xml],
            link_with : [eegdev, faketia],
        )
        test('testtobia', files('testfaketobiia.sh'), env : test_env)
    endif
endif
