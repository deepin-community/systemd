/* SPDX-License-Identifier: LGPL-2.1+ */

#include "selinux-access.h"

#if HAVE_SELINUX

#include <errno.h>
#include <selinux/avc.h>
#include <selinux/uavc.h>
#include <selinux/usec.h>
#include <selinux/selinux.h>
#include <stdio.h>
#include <sys/xattr.h>
#if HAVE_AUDIT
#include <libaudit.h>
#endif

#include "sd-bus.h"

#include "alloc-util.h"
#include "audit-fd.h"
#include "bus-util.h"
#include "log.h"
#include "path-util.h"
#include "selinux-util.h"
#include "umac-util.h"
#include "stdio-util.h"
#include "strv.h"
#include "format-util.h"

static bool initialized = false;

#define DEFAULT_USID_CONTEXT	"root:sysadm_r:sysadm_t:s0"

/*
   Any time an access gets denied this callback will be called
   with the audit data.  We then need to just copy the audit data into the msgbuf.
*/
static int audit_callback2(
                void *auditdata,
                security_class_t cls,
                char *msgbuf,
                size_t msgbufsize) {

        const struct audit_info *audit = auditdata;
        uid_t uid = 0, login_uid = 0;
        gid_t gid = 0;
        char login_uid_buf[DECIMAL_STR_MAX(uid_t) + 1] = "n/a";
        char uid_buf[DECIMAL_STR_MAX(uid_t) + 1] = "n/a";
        char gid_buf[DECIMAL_STR_MAX(gid_t) + 1] = "n/a";

        if (sd_bus_creds_get_audit_login_uid(audit->creds, &login_uid) >= 0)
                xsprintf(login_uid_buf, UID_FMT, login_uid);
        if (sd_bus_creds_get_euid(audit->creds, &uid) >= 0)
                xsprintf(uid_buf, UID_FMT, uid);
        if (sd_bus_creds_get_egid(audit->creds, &gid) >= 0)
                xsprintf(gid_buf, GID_FMT, gid);

        snprintf(msgbuf, msgbufsize,
                 "%s auid=%s uid=%s gid=%s%s%s%s%s%s%s",
                 (audit->avc_type == AVC_TYPE_USID) ? "usid" : "", login_uid_buf, uid_buf, gid_buf,
                 audit->path ? " path=\"" : "", strempty(audit->path), audit->path ? "\"" : "",
                 audit->cmdline ? " cmdline=\"" : "", strempty(audit->cmdline), audit->cmdline ? "\"" : "");

        return 0;
}

/*
   libselinux uses this callback when access gets denied or other
   events happen. If audit is turned on, messages will be reported
   using audit netlink, otherwise they will be logged using the usual
   channels.

   Code copied from dbus and modified.
*/
_printf_(2, 3) static int log_callback2(int type, const char *fmt, ...) {
        va_list ap;

#if HAVE_AUDIT
        int fd;

        fd = get_audit_fd();

        if (fd >= 0) {
                _cleanup_free_ char *buf = NULL;
                int r;

                va_start(ap, fmt);
                r = vasprintf(&buf, fmt, ap);
                va_end(ap);

                if (r >= 0) {
                        audit_log_user_avc_message(fd, AUDIT_USER_AVC, buf, NULL, NULL, NULL, 0);
                        return 0;
                }
        }
#endif

        return 0;
}

static int access_init2(sd_bus_error *error) {

        if (!umac_use())
                return 0;

        if (initialized)
                return 1;

        if (uavc_open(NULL, 0) != 0) {
                int enforce, saved_errno = errno;

                enforce = security_getenforce();
                log_full_errno(enforce != 0 ? LOG_ERR : LOG_WARNING, saved_errno, "Failed to open the SELinux AVC: %m");

                /* If enforcement isn't on, then let's suppress this
                 * error, and just don't do any AVC checks. The
                 * warning we printed is hence all the admin will
                 * see. */
                return 0;
        }

        selinux_set_callback(SELINUX_CB_AUDIT, (union selinux_callback) audit_callback2);
        selinux_set_callback(SELINUX_CB_LOG, (union selinux_callback) log_callback2);

        initialized = true;
        return 1;
}

static int mac_selinux_getfilecon2(const char *path, char **con) {
        char *context = NULL;
        int r = 0;

        *con = NULL;
#define MAX_USID 255
        if (0 == access("/proc/self/attr/usid", F_OK)) {
                /* Get the file context2 of the unit file */
                context = (char *)malloc(MAX_USID + 1);
                if (context == NULL)
                        return -ENOMEM;

                r = getxattr(path, "security.selinux2", context, MAX_USID);
                if (r <= 0) {
                        free(context);
                        context = NULL;
                } else {
                        context[r] = 0;
                        *con = context;
                }
        }

        return 0;
}

static int mac_selinux_freecon2(char *con) {
        if (con != NULL)
                free(con);
        return 0;
}

static int sd_bus_creds_get_umac_context(sd_bus_creds *c, char **ret) {
        assert_return(c, -EINVAL);

#define USID_LEN_MAX 255
        char proc_usid[32] = {0};
        char *label_buff = NULL;
        pid_t pid = 0;
        int usid_fd = 0;
        int num = 0;
        int r = 0;

        *ret = NULL;
        r = sd_bus_creds_get_pid(c, &pid);
        if (r < 0)
                return r;
        if (pid <= 0)
                return 0;

        snprintf(proc_usid, sizeof(proc_usid), "/proc/%d/attr/usid", pid);
        usid_fd = open(proc_usid, O_RDONLY);
        if (usid_fd < 0)
                return 0;

        label_buff = (char *)malloc(USID_LEN_MAX + 1);
        if (label_buff == NULL) {
                r = -ENOMEM;
                goto out;
        }

        num = read(usid_fd, label_buff, USID_LEN_MAX);
        if (num <= USID_LEN_MAX) {
                if (num <= 0) {
                        free(label_buff);
                        label_buff = NULL;
                        goto out;
                }
                label_buff[num] = 0;
        } else {
                free(label_buff);
                label_buff = NULL;
                goto out;
        }

        *ret = label_buff;
out:
        close(usid_fd);
        return r;
}

/*
   This function communicates with the kernel to check whether or not it should
   allow the access.
   If the machine is in permissive mode it will return ok.  Audit messages will
   still be generated if the access would be denied in enforcing mode.
*/
int umac_unit_access_check(
                sd_bus_message *message,
                const char *unit_path,
                const char *unit_context,
                const char *permission,
                const char *function,
                sd_bus_error *error) {

        _cleanup_(sd_bus_creds_unrefp) sd_bus_creds *creds = NULL;
        const char *tclass = NULL, *scon = NULL;
        struct audit_info audit_info = {};
        _cleanup_free_ char *cl = NULL;
        char *fcon = NULL;
        char **cmdline = NULL;
        int r = 0;
        char *scon2 = NULL, *fcon2 = NULL;

        assert(message);
        assert(permission);
        assert(function);
        assert(error);

        if (!unit_path)
                return 0;

        r = access_init2(error);
        if (r <= 0)
                return r;

        r = sd_bus_query_sender_creds(
                        message,
                        SD_BUS_CREDS_PID|SD_BUS_CREDS_EUID|SD_BUS_CREDS_EGID|
                        SD_BUS_CREDS_CMDLINE|SD_BUS_CREDS_AUDIT_LOGIN_UID|
                        SD_BUS_CREDS_AUGMENT /* get more bits from /proc */,
                        &creds);
        if (r < 0)
                goto finish;

        r = mac_selinux_getfilecon2(unit_path, &fcon2);
        if (r < 0)
                goto finish;

        if (fcon2) {
                fcon = fcon2;
                /* Get the subject usid if get the service file context2 success */
                r = sd_bus_creds_get_umac_context(creds, &scon2);
                if (r < 0) {
                        goto finish;
                }

                if (scon2 != NULL)
                        scon = scon2;
				else
                        scon = DEFAULT_USID_CONTEXT;
        } else
                return 0;

        tclass = "service";

        sd_bus_creds_get_cmdline(creds, &cmdline);
        cl = strv_join(cmdline, " ");

        audit_info.creds = creds;
        audit_info.path = unit_path;
        audit_info.cmdline = cl;
        audit_info.function = function;
        audit_info.avc_type = AVC_TYPE_USID;

        r = selinux_check_access(scon, fcon, tclass, permission, &audit_info);
        if (r < 0)
                r = sd_bus_error_setf(error, SD_BUS_ERROR_ACCESS_DENIED, "UMAC policy denies access.");

        log_debug("UMAC access check scon=%s tcon=%s tclass=%s perm=%s function=%s path=%s cmdline=%s: %i", scon, fcon, tclass, permission, function,unit_path, cl, r);

finish:
        mac_selinux_freecon2(fcon2);
        if (scon2 != NULL)
                free(scon2);

        if (r < 0 && security_getenforce() != 1) {
                sd_bus_error_free(error);
                r = 0;
        }

        return r;
}

#else

int umac_unit_access_check(
                sd_bus_message *message,
                const char *unit_path,
                const char *unit_context,
                const char *permission,
                const char *function,
                sd_bus_error *error) {

        return 0;
}

#endif
