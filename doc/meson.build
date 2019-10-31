subdir('examples')

if get_option('corelib-build').enabled()
    egd_config = config
    configure_file(input : 'egd_open.3.in',
        output : 'egd_open.3',
        configuration : egd_config,
        install_dir : get_option('mandir')+'/man3/',
    )

    man_pages = files(
        'egd_channel_info.3',
        'egd_get_data.3',
        'egd_sensor_type.3',
        'eegdev-open-options.5',
        'egd_close.3',
        'egd_get_numch.3',
        'egd_start.3',
        'egd_get_available.3',
        'egd_stop.3',
        'egd_acq_setup.3',
        'egd_get_cap.3',
        'egd_sensor_name.3',
    )
    install_man(man_pages)
endif

if xdf_state == 'enabled'
    install_man('eegdev-datafile.5')
endif
if neurosky_state == 'enabled'
    install_man('eegdev-neurosky.5')
endif
if biosemi_state == 'enabled'
    install_man('eegdev-biosemi.5')
endif
if gtec_state == 'enabled'
    install_man('eegdev-gtec.5')
endif
if tobiia_state == 'enabled'
    install_man('eegdev-tobiia.5')
endif
