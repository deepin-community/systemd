# SPDX-License-Identifier: LGPL-2.1-or-later

executables += [
        libexec_template + {
                'name' : 'systemd-storage-block',
                'sources' : files(
                        'storage-block.c',
                        'storage-util.c',
                ),
        },
        libexec_template + {
                'name' : 'systemd-storage-fs',
                'sources' : files(
                        'storage-fs.c',
                        'storage-util.c',
                ),
        },
        executable_template + {
                'name' : 'storagectl',
                'public' : true,
                'sources' : files(
                        'storagectl.c',
                        'storage-util.c',
                ),
        },
]

meson.add_install_script(sh, '-c', ln_s.format(bindir / 'storagectl',
                                               sbindir / 'mount.storage'))

install_data('io.systemd.storage.policy',
             install_dir : polkitpolicydir)
