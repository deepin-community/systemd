/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <sys/mount.h>
#include <sys/statvfs.h>

#include "sd-device.h"

#include "fd-util.h"
#include "fs-util.h"
#include "json.h"
#include "parse-util.h"
#include "path-util.h"
#include "user-util.h"
#include "varlink.h"

typedef JsonDispatch sd_json_dispatch_field;
typedef JsonVariant sd_json_variant;
typedef Varlink sd_varlink;
typedef VarlinkMethodFlags sd_varlink_method_flags_t;
typedef VarlinkServer sd_varlink_server;

#define SD_JSON_VARIANT_STRING JSON_VARIANT_STRING
#define SD_JSON_VARIANT_BOOLEAN JSON_VARIANT_BOOLEAN
#define _SD_JSON_VARIANT_TYPE_INVALID _JSON_VARIANT_TYPE_INVALID
#define SD_JSON_MANDATORY JSON_MANDATORY

#define SD_JSON_BUILD_BOOLEAN JSON_BUILD_BOOLEAN
#define SD_JSON_BUILD_INTEGER JSON_BUILD_INTEGER
#define SD_JSON_BUILD_UNSIGNED JSON_BUILD_UNSIGNED
#define SD_JSON_BUILD_PAIR_BOOLEAN JSON_BUILD_PAIR_BOOLEAN
#define SD_JSON_BUILD_PAIR_CONDITION JSON_BUILD_PAIR_CONDITION
#define SD_JSON_BUILD_PAIR_INTEGER JSON_BUILD_PAIR_INTEGER
#define SD_JSON_BUILD_PAIR_STRING JSON_BUILD_PAIR_STRING

#define JSON_BUILD_PAIR_UNSIGNED_NOT_EQUAL(name, value, other) \
        JSON_BUILD_PAIR_CONDITION((value) != (other), (name), JSON_BUILD_UNSIGNED(value))

#define SD_VARLINK_METHOD_MORE VARLINK_METHOD_MORE
#define sd_json_dispatch_const_string json_dispatch_const_string
#define sd_json_dispatch_tristate json_dispatch_tristate
#define sd_json_dispatch_uint64 json_dispatch_uint64
#define sd_varlink_dispatch varlink_dispatch
#define sd_varlink_error varlink_error
#define sd_varlink_push_fd varlink_push_fd
#define sd_varlink_replybo(link, ...) varlink_replyb((link), JSON_BUILD_OBJECT(__VA_ARGS__))
#define sd_varlink_server_add_interface varlink_server_add_interface
#define sd_varlink_server_bind_method_many varlink_server_bind_method_many
#define sd_varlink_server_loop_auto varlink_server_loop_auto
#define sd_varlink_server_unrefp varlink_server_unrefp

#define voffsetof(var, member) offsetof(typeof(var), member)

#define ERRNO_IS_IOCTL_NOT_SUPPORTED(r) ERRNO_IS_NEG_NOT_SUPPORTED(r)
#define O_ACCMODE_STRICT O_ACCMODE

#define FOREIGN_UID_BASE ((uid_t) 0x7FFE0000U)
#define FOREIGN_UID_MIN FOREIGN_UID_BASE
#define FOREIGN_UID_MAX (FOREIGN_UID_BASE + (uid_t) 0xFFFFU)

static inline bool uid_is_foreign(uid_t uid) {
        return uid >= FOREIGN_UID_MIN && uid <= FOREIGN_UID_MAX;
}

static inline bool gid_is_foreign(gid_t gid) {
        return gid >= FOREIGN_UID_MIN && gid <= FOREIGN_UID_MAX;
}

static inline bool ERRNO_IS_NEG_FS_WRITE_REFUSED(intmax_t r) {
        return IN_SET(r, -EROFS, -EACCES, -EPERM);
}

static inline bool device_in_subsystem(sd_device *device, const char *subsystem) {
        const char *value;

        assert(device);
        assert(subsystem);

        return sd_device_get_subsystem(device, &value) >= 0 && streq(value, subsystem);
}

static inline int storage_device_get_sysattr_u64(sd_device *device, const char *sysattr, uint64_t *ret) {
        const char *value;
        int r;

        r = sd_device_get_sysattr_value(device, sysattr, &value);
        if (r < 0)
                return r;

        return safe_atou64(value, ret);
}

static inline int storage_fd_is_read_only_fs(int fd) {
        struct statvfs st;

        if (fstatvfs(fd, &st) < 0)
                return -errno;

        if (st.f_flag & ST_RDONLY)
                return true;

        return access_fd(fd, W_OK) == -EROFS;
}

static inline int storage_stat_verify_block(const struct stat *st) {
        assert(st);

        return S_ISBLK(st->st_mode) ? 0 : -ENOTBLK;
}

static inline int storage_state_directory(RuntimeScope scope, char **ret) {
        assert(ret);

        if (scope == RUNTIME_SCOPE_SYSTEM) {
                char *p = strdup("/var/lib");
                if (!p)
                        return -ENOMEM;

                *ret = p;
                return 0;
        }

        const char *e = secure_getenv("XDG_STATE_HOME");
        if (e && path_is_absolute(e)) {
                char *p = strdup(e);
                if (!p)
                        return -ENOMEM;

                *ret = p;
                return 0;
        }

        _cleanup_free_ char *home = NULL;
        int r = get_home_dir(&home);
        if (r < 0)
                return r;

        char *p = path_join(home, ".local/state");
        if (!p)
                return -ENOMEM;

        *ret = p;
        return 0;
}

static inline int storage_open_tree_attr(int fd, bool read_only) {
        int mount_fd;

        mount_fd = open_tree(
                        fd,
                        "",
                        OPEN_TREE_CLONE|OPEN_TREE_CLOEXEC|AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW);
        if (mount_fd < 0)
                return -errno;

        if (mount_setattr(
                        mount_fd,
                        "",
                        AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,
                        &(struct mount_attr) {
                                .attr_set = read_only ? MOUNT_ATTR_RDONLY : 0,
                                .attr_clr = MOUNT_ATTR_NOSUID|MOUNT_ATTR_NOEXEC|MOUNT_ATTR_NODEV,
                                .propagation = MS_PRIVATE,
                        },
                        MOUNT_ATTR_SIZE_VER0) < 0) {
                safe_close(mount_fd);
                return -errno;
        }

        return mount_fd;
}

#define VARLINK_DISPATCH_POLKIT_FIELD {            \
                "allowInteractiveAuthentication", \
                JSON_VARIANT_BOOLEAN,               \
                NULL,                               \
                0,                                  \
                0,                                  \
        }

static inline int sd_varlink_error_invalid_parameter_name(sd_varlink *link, const char *name) {
        return varlink_errorb(
                        link,
                        VARLINK_ERROR_INVALID_PARAMETER,
                        JSON_BUILD_OBJECT(JSON_BUILD_PAIR_STRING("parameter", name)));
}

static inline int storage_verify_peer(sd_varlink *link) {
        uid_t peer_uid;
        int r;

        assert(link);

        /* systemd 255 cannot authenticate Varlink peers through polkit without a PID reuse race. Keep
         * system providers root-only and user providers restricted to their own UID. */

        r = varlink_get_peer_uid(link, &peer_uid);
        if (r < 0)
                return r;

        if (peer_uid != getuid())
                return varlink_error(link, VARLINK_ERROR_PERMISSION_DENIED, NULL);

        return 1;
}

static inline int storage_varlink_reply_append(
                Varlink *link,
                JsonVariant **pending,
                JsonVariant *reply) {

        int r;

        assert(link);
        assert(pending);
        assert(reply);

        if (*pending) {
                r = varlink_notify(link, *pending);
                if (r < 0)
                        return r;

                *pending = json_variant_unref(*pending);
        }

        *pending = json_variant_ref(reply);
        return 0;
}

static inline int storage_varlink_reply_finish(
                Varlink *link,
                JsonVariant *pending,
                const char *empty_error_id) {

        assert(link);
        assert(empty_error_id);

        if (!pending)
                return varlink_error(link, empty_error_id, NULL);

        return varlink_reply(link, pending);
}
