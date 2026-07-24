# SPDX-License-Identifier: LGPL-2.1-or-later

units = [
        'app.slice',
        'background.slice',
        'basic.target',
        'bluetooth.target',
        'default.target',
        'exit.target',
        'graphical-session-pre.target',
        'graphical-session.target',
        'paths.target',
        'printer.target',
        'session.slice',
        'shutdown.target',
        'smartcard.target',
        'sockets.target',
        'sound.target',
        'systemd-exit.service',
        'systemd-tmpfiles-clean.service',
        'systemd-tmpfiles-clean.timer',
        'systemd-tmpfiles-setup.service',
        'systemd-storage-fs.socket',
        'timers.target',
]

if conf.get('ENABLE_XDG_AUTOSTART') == 1
        units += 'xdg-desktop-autostart.target'
endif

foreach file : units
        install_data(file,
                     install_dir : userunitdir)
endforeach

custom_target(
        'user-systemd-storage-fs@.service',
        input : 'systemd-storage-fs@.service.in',
        output : 'systemd-storage-fs@.service',
        command : [jinja2_cmdline, '@INPUT@', '@OUTPUT@'],
        install : true,
        install_dir : userunitdir)

install_emptydir(userunitdir / 'sockets.target.wants')
meson.add_install_script(
        sh,
        '-c',
        ln_s.format(
                userunitdir / 'systemd-storage-fs.socket',
                userunitdir / 'sockets.target.wants/systemd-storage-fs.socket'))
