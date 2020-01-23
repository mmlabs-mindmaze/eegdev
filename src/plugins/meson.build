includes = include_directories('../..', '../core/')

plugins_instdir = get_option('libdir') / 'eegdev'

xdf_state = 'disabled'
xdffileio = cc.find_library('xdffileio', required : get_option('xdf'))
tre = dependency('tre', required : get_option('xdf'))
if xdffileio.found() and tre.found()
    xdf_state = 'enabled'
    datafile = shared_module('datafile',
        'fileout.c',
        name_prefix : '',  # do not prefix with 'lib'
        include_directories : includes,
        dependencies : [mmlib, tre, xdffileio],
        install : true,
        install_dir: plugins_instdir,
    )
endif


biosemi_state = 'disabled'
usb = dependency('libusb-1.0', required : get_option('biosemi'))
if usb.found()
    biosemi_state = 'enabled'
    shared_module('biosemi',
        'biosemi.c',
        name_prefix : '',  # do not prefix with 'lib'
        include_directories : includes,
        dependencies : [mmlib, usb],
        install : true,
        install_dir: plugins_instdir,
    )
    install_data('biosemi.conf', install_dir :
        '@0@/@1@'.format(get_option('sysconfdir'),
            meson.project_name()))
endif


gtec_state = get_option('gtec')
dlopen_gusbamp = 'false'
if get_option('gtec') != 'disabled'
    gtec_src = files('gtec.c')
    deps = [mmlib]

    gusbamp = dependency('gusbampapi',
        required : (get_option('gtec') == 'enabled'),
    )
    if get_option('gtec') != 'module' and gusbamp.found()
        gtec_state = 'enabled'
        deps = deps + [gusbamp]
    else
        gtec_state = 'module'
        config.set('DLOPEN_GUSBAMP', 1)
        dlopen_gusbamp = 'true'
        gtec_src += files('gusbamp-loader.h',
                'gusbamp-loader.c')
    endif
    threads = dependency('threads',
        required : (get_option('gtec') != 'auto'),
    )
    deps += [threads]
    if threads.found()
        shared_module('gtec',
            gtec_src,
            name_prefix : '',  # do not prefix with 'lib'
            include_directories : includes,
            dependencies : deps,
            install : true,
            install_dir: plugins_instdir,
        )
    endif
endif


neurosky_state = 'disabled'
blue = dependency('bluez', required : get_option('neurosky'))
if blue.found()
    neurosky_state = 'enabled'
    shared_module('neurosky',
        'neurosky.c',
        name_prefix : '',  # do not prefix with 'lib'
        include_directories : includes,
        dependencies : [blue],
        install : true,
        install_dir: plugins_instdir,
    )
endif


tobiia_state = 'disabled'
xml = dependency('expat', required : get_option('tobiia'))
if xml.found()
    tobiia_state = 'enabled'
    shared_module('tobiia',
        'tobiia.c',
        include_directories : includes,
        name_prefix : '',  # do not prefix with 'lib'
        dependencies : [mmlib, xml],
        install : true,
        install_dir: plugins_instdir,
    )
endif
