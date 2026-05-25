/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "sd-bus.h"

#include "manager.h"

struct audit_info {
        sd_bus_creds *creds;
        const char *path;
        const char *cmdline;
        const char *function;
#define AVC_TYPE_SELINUX	1
#define AVC_TYPE_USID	2
        int avc_type;
};


int mac_selinux_access_check_internal(sd_bus_message *message, const char *unit_path, const char *unit_label, const char *permission, const char *function, sd_bus_error *error);
int umac_unit_access_check(sd_bus_message *message, const char *unit_path, const char *unit_context, const char *permission, const char *function, sd_bus_error *error);
#define mac_selinux_access_check(message, permission, error) \
        mac_selinux_access_check_internal((message), NULL, NULL, (permission), __func__, (error))

#define mac_selinux_unit_access_check(unit, message, permission, error) ({ \
        int RC;		\
        do {		\
                RC = mac_selinux_access_check_internal((message), (unit)->fragment_path, (unit)->access_selinux_context, (permission), __func__, (error)); \
                if (RC < 0)	\
                        break;	\
                RC = umac_unit_access_check((message), (unit)->fragment_path, (unit)->access_selinux_context, (permission), __func__, (error)); \
        } while (0);		\
        RC;		\
})
