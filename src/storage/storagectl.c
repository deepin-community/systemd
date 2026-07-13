/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <getopt.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "argv-util.h"
#include "build.h"
#include "bus-util.h"
#include "dirent-util.h"
#include "errno-list.h"
#include "escape.h"
#include "extract-word.h"
#include "fd-util.h"
#include "format-table.h"
#include "format-util.h"
#include "json.h"
#include "main-func.h"
#include "mount-util.h"
#include "namespace-util.h"
#include "pager.h"
#include "parse-argument.h"
#include "parse-util.h"
#include "path-lookup.h"
#include "path-util.h"
#include "socket-util.h"
#include "spawn-polkit-agent.h"
#include "stat-util.h"
#include "stdio-util.h"
#include "storage-util.h"
#include "string-util.h"
#include "strv.h"
#include "terminal-util.h"
#include "user-util.h"
#include "varlink.h"
#include "verbs.h"
#include "version.h"

static JsonFormatFlags arg_json_format_flags = JSON_FORMAT_OFF;
static PagerFlags arg_pager_flags = 0;
static bool arg_legend = true;
static bool arg_ask_password = true;
static RuntimeScope arg_runtime_scope = RUNTIME_SCOPE_SYSTEM;

static int help(void) {
        printf("%1$s [OPTIONS...] COMMAND\n\n"
               "%2$sEnumerate storage volumes and providers.%3$s\n"
               "\n%4$sCommands:%5$s\n"
               "  volumes [GLOB]         List storage volumes\n"
               "  templates [GLOB]       List storage volume templates\n"
               "  providers              List storage providers\n"
               "  help                   Show this help\n"
               "\n%4$sOptions:%5$s\n"
               "  -h --help              Show this help\n"
               "     --version           Show package version\n"
               "     --no-pager          Do not pipe output into a pager\n"
               "     --no-legend         Do not show the headers and footers\n"
               "     --system            Operate in system mode\n"
               "     --user              Operate in user mode\n"
               "     --no-ask-password   Do not prompt for password\n"
               "     --json=MODE         Output as JSON\n"
               "  -j                     Same as --json=pretty on tty, --json=short otherwise\n",
               program_invocation_short_name,
               ansi_highlight(),
               ansi_normal(),
               ansi_underline(),
               ansi_normal());

        return 0;
}

static int verb_help(int argc, char *argv[], void *userdata) {
        return help();
}

static int storage_provider_directory(char **ret) {
        assert(ret);

        if (arg_runtime_scope == RUNTIME_SCOPE_SYSTEM) {
                char *path = strdup("/run/systemd/io.systemd.StorageProvider");
                if (!path)
                        return -ENOMEM;

                *ret = path;
                return 0;
        }

        return xdg_user_runtime_dir(ret, "/systemd/io.systemd.StorageProvider");
}

typedef int (*StorageReplyHandler)(const char *provider, JsonVariant *reply, Table *table);

static int collect_from_providers(
                const char *method,
                const char *match,
                StorageReplyHandler handler,
                Table *table) {

        _cleanup_closedir_ DIR *dir = NULL;
        _cleanup_(json_variant_unrefp) JsonVariant *parameters = NULL;
        _cleanup_free_ char *path = NULL;
        int r;

        assert(method);
        assert(handler);
        assert(table);

        r = storage_provider_directory(&path);
        if (r < 0)
                return log_error_errno(r, "Failed to determine storage provider directory: %m");

        if (match) {
                r = json_build(&parameters, JSON_BUILD_OBJECT(JSON_BUILD_PAIR_STRING("matchName", match)));
                if (r < 0)
                        return log_oom();
        }

        dir = opendir(path);
        if (!dir) {
                if (errno == ENOENT)
                        return 0;

                return log_error_errno(errno, "Failed to open '%s': %m", path);
        }

        FOREACH_DIRENT(de, dir, return log_error_errno(errno, "Failed to enumerate '%s': %m", path)) {
                if (de->d_type != DT_SOCK || !storage_provider_name_is_valid(de->d_name))
                        continue;

                _cleanup_free_ char *socket_path = path_join(path, de->d_name);
                if (!socket_path)
                        return log_oom();

                _cleanup_(varlink_flush_close_unrefp) Varlink *link = NULL;
                r = varlink_connect_address(&link, socket_path);
                if (r < 0) {
                        log_debug_errno(r, "Failed to connect to storage provider '%s', ignoring: %m", de->d_name);
                        continue;
                }

                _cleanup_(json_variant_unrefp) JsonVariant *replies = NULL;
                r = varlink_collect(link, method, parameters, &replies, /* ret_error_id= */ NULL, /* ret_flags= */ NULL);
                if (r < 0) {
                        log_debug_errno(r, "Failed to call %s on storage provider '%s', ignoring: %m", method, de->d_name);
                        continue;
                }
                if (r == 0)
                        continue;

                JsonVariant *reply;
                JSON_VARIANT_ARRAY_FOREACH(reply, replies) {
                        r = handler(de->d_name, reply, table);
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}

static int add_volume_reply(const char *provider, JsonVariant *reply, Table *table) {
        struct {
                const char *name;
                char **aliases;
                const char *type;
                int read_only;
                uint64_t size_bytes;
                uint64_t used_bytes;
        } p = {
                .read_only = -1,
                .size_bytes = UINT64_MAX,
                .used_bytes = UINT64_MAX,
        };

        static const JsonDispatch dispatch_table[] = {
                { "name",      JSON_VARIANT_STRING,        json_dispatch_const_string, offsetof(typeof(p), name),       0 },
                { "aliases",   JSON_VARIANT_ARRAY,         json_dispatch_strv,         offsetof(typeof(p), aliases),    0 },
                { "type",      JSON_VARIANT_STRING,        json_dispatch_const_string, offsetof(typeof(p), type),       0 },
                { "readOnly",  JSON_VARIANT_BOOLEAN,       json_dispatch_tristate,     offsetof(typeof(p), read_only),  0 },
                { "sizeBytes", _JSON_VARIANT_TYPE_INVALID, json_dispatch_uint64,       offsetof(typeof(p), size_bytes), 0 },
                { "usedBytes", _JSON_VARIANT_TYPE_INVALID, json_dispatch_uint64,       offsetof(typeof(p), used_bytes), 0 },
                {}
        };

        int r;

        assert(provider);
        assert(reply);
        assert(table);

        r = json_dispatch(reply, dispatch_table, JSON_LOG, &p);
        if (r < 0) {
                strv_free(p.aliases);
                return log_error_errno(r, "Failed to decode ListVolumes() reply from '%s': %m", provider);
        }

        strv_free(p.aliases);

        r = table_add_many(
                        table,
                        TABLE_STRING, provider,
                        TABLE_STRING, p.name,
                        TABLE_STRING, p.type);
        if (r < 0)
                return table_log_add_error(r);

        if (p.read_only < 0)
                r = table_add_many(table, TABLE_EMPTY);
        else
                r = table_add_many(table, TABLE_BOOLEAN, p.read_only > 0);
        if (r < 0)
                return table_log_add_error(r);

        if (p.size_bytes == UINT64_MAX)
                r = table_add_many(table, TABLE_EMPTY, TABLE_SET_ALIGN_PERCENT, 100);
        else
                r = table_add_many(table, TABLE_SIZE, p.size_bytes, TABLE_SET_ALIGN_PERCENT, 100);
        if (r < 0)
                return table_log_add_error(r);

        if (p.used_bytes == UINT64_MAX)
                r = table_add_many(table, TABLE_EMPTY, TABLE_SET_ALIGN_PERCENT, 100);
        else
                r = table_add_many(table, TABLE_SIZE, p.used_bytes, TABLE_SET_ALIGN_PERCENT, 100);
        if (r < 0)
                return table_log_add_error(r);

        return 0;
}

static int verb_list_volumes(int argc, char *argv[], void *userdata) {
        _cleanup_(table_unrefp) Table *table = table_new("provider", "name", "type", "ro", "size", "used");
        if (!table)
                return log_oom();

        (void) table_set_sort(table, (size_t) 0, (size_t) 1);
        table_set_ersatz_string(table, TABLE_ERSATZ_DASH);

        int r = collect_from_providers(
                        "io.systemd.StorageProvider.ListVolumes",
                        argc >= 2 ? argv[1] : NULL,
                        add_volume_reply,
                        table);
        if (r < 0)
                return r;

        if (table_get_rows(table) > 1) {
                r = table_print_with_pager(table, arg_json_format_flags, arg_pager_flags, arg_legend);
                if (r < 0)
                        return r;
        }

        if (arg_legend && FLAGS_SET(arg_json_format_flags, JSON_FORMAT_OFF)) {
                if (table_get_rows(table) <= 1)
                        printf("No storage volumes.\n");
                else
                        printf("\n%zu storage volumes listed.\n", table_get_rows(table) - 1);
        }

        return 0;
}

static int add_template_reply(const char *provider, JsonVariant *reply, Table *table) {
        struct {
                const char *name;
                const char *type;
        } p = {};

        static const JsonDispatch dispatch_table[] = {
                { "name", JSON_VARIANT_STRING, json_dispatch_const_string, offsetof(typeof(p), name), 0 },
                { "type", JSON_VARIANT_STRING, json_dispatch_const_string, offsetof(typeof(p), type), 0 },
                {}
        };

        int r;

        assert(provider);
        assert(reply);
        assert(table);

        r = json_dispatch(reply, dispatch_table, JSON_LOG, &p);
        if (r < 0)
                return log_error_errno(r, "Failed to decode ListTemplates() reply from '%s': %m", provider);

        r = table_add_many(
                        table,
                        TABLE_STRING, provider,
                        TABLE_STRING, p.name,
                        TABLE_STRING, p.type);
        if (r < 0)
                return table_log_add_error(r);

        return 0;
}

static int verb_list_templates(int argc, char *argv[], void *userdata) {
        _cleanup_(table_unrefp) Table *table = table_new("provider", "name", "type");
        if (!table)
                return log_oom();

        (void) table_set_sort(table, (size_t) 0, (size_t) 1);
        table_set_ersatz_string(table, TABLE_ERSATZ_DASH);

        int r = collect_from_providers(
                        "io.systemd.StorageProvider.ListTemplates",
                        argc >= 2 ? argv[1] : NULL,
                        add_template_reply,
                        table);
        if (r < 0)
                return r;

        if (table_get_rows(table) > 1) {
                r = table_print_with_pager(table, arg_json_format_flags, arg_pager_flags, arg_legend);
                if (r < 0)
                        return r;
        }

        if (arg_legend && FLAGS_SET(arg_json_format_flags, JSON_FORMAT_OFF)) {
                if (table_get_rows(table) <= 1)
                        printf("No templates.\n");
                else
                        printf("\n%zu templates listed.\n", table_get_rows(table) - 1);
        }

        return 0;
}

static int verb_providers(int argc, char *argv[], void *userdata) {
        _cleanup_closedir_ DIR *dir = NULL;
        _cleanup_free_ char *path = NULL;
        int r;

        r = storage_provider_directory(&path);
        if (r < 0)
                return log_error_errno(r, "Failed to determine storage provider directory: %m");

        _cleanup_(table_unrefp) Table *table = table_new("provider", "listening");
        if (!table)
                return log_oom();

        (void) table_set_sort(table, (size_t) 0);
        table_set_ersatz_string(table, TABLE_ERSATZ_DASH);

        dir = opendir(path);
        if (!dir) {
                if (errno != ENOENT)
                        return log_error_errno(errno, "Failed to open '%s': %m", path);
        } else
                FOREACH_DIRENT(de, dir, return log_error_errno(errno, "Failed to enumerate '%s': %m", path)) {
                        if (de->d_type != DT_SOCK || !storage_provider_name_is_valid(de->d_name))
                                continue;

                        _cleanup_close_ int socket_fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
                        if (socket_fd < 0)
                                return log_error_errno(errno, "Failed to allocate AF_UNIX socket: %m");

                        _cleanup_free_ char *unavailable = NULL;
                        r = connect_unix_path(socket_fd, dirfd(dir), de->d_name);
                        if (r < 0) {
                                unavailable = strjoin("no (", strna(errno_to_name(-r)), ")");
                                if (!unavailable)
                                        return log_oom();
                        }

                        r = table_add_many(
                                        table,
                                        TABLE_STRING, de->d_name,
                                        TABLE_STRING, unavailable ?: "yes",
                                        TABLE_SET_COLOR, ansi_highlight_green_red(!unavailable));
                        if (r < 0)
                                return table_log_add_error(r);
                }

        if (table_get_rows(table) > 1) {
                r = table_print_with_pager(table, arg_json_format_flags, arg_pager_flags, arg_legend);
                if (r < 0)
                        return r;
        }

        if (arg_legend && FLAGS_SET(arg_json_format_flags, JSON_FORMAT_OFF)) {
                if (table_get_rows(table) <= 1)
                        printf("No providers.\n");
                else
                        printf("\n%zu providers listed.\n", table_get_rows(table) - 1);
        }

        return 0;
}

static int parse_argv(int argc, char *argv[]) {
        enum {
                ARG_VERSION = 0x100,
                ARG_NO_PAGER,
                ARG_NO_LEGEND,
                ARG_SYSTEM,
                ARG_USER,
                ARG_NO_ASK_PASSWORD,
                ARG_JSON,
        };

        static const struct option options[] = {
                { "help",      no_argument,       NULL, 'h'           },
                { "version",   no_argument,       NULL, ARG_VERSION   },
                { "no-pager",  no_argument,       NULL, ARG_NO_PAGER  },
                { "no-legend", no_argument,       NULL, ARG_NO_LEGEND },
                { "system",    no_argument,       NULL, ARG_SYSTEM    },
                { "user",      no_argument,       NULL, ARG_USER      },
                { "no-ask-password", no_argument, NULL, ARG_NO_ASK_PASSWORD },
                { "json",      required_argument, NULL, ARG_JSON      },
                {},
        };

        int c, r;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "hj", options, NULL)) >= 0)
                switch (c) {
                case 'h':
                        return help();

                case ARG_VERSION:
                        return version();

                case ARG_NO_PAGER:
                        arg_pager_flags |= PAGER_DISABLE;
                        break;

                case ARG_NO_LEGEND:
                        arg_legend = false;
                        break;

                case ARG_SYSTEM:
                        arg_runtime_scope = RUNTIME_SCOPE_SYSTEM;
                        break;

                case ARG_USER:
                        arg_runtime_scope = RUNTIME_SCOPE_USER;
                        break;

                case ARG_NO_ASK_PASSWORD:
                        arg_ask_password = false;
                        break;

                case ARG_JSON:
                        r = parse_json_argument(optarg, &arg_json_format_flags);
                        if (r <= 0)
                                return r;
                        break;

                case 'j':
                        arg_json_format_flags = JSON_FORMAT_PRETTY_AUTO|JSON_FORMAT_COLOR_AUTO;
                        break;

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached();
                }

        return 1;
}

static int acquire_volume(
                Varlink *link,
                const char *name,
                CreateMode create_mode,
                const char *template,
                int read_only,
                const char *request_as,
                uint64_t create_size,
                JsonVariant **ret_reply,
                const char **ret_error_id) {

        assert(link);
        assert(name);
        assert(request_as);

        return varlink_callb(
                        link,
                        "io.systemd.StorageProvider.Acquire",
                        ret_reply,
                        ret_error_id,
                        /* ret_flags= */ NULL,
                        JSON_BUILD_OBJECT(
                                JSON_BUILD_PAIR_STRING("name", name),
                                JSON_BUILD_PAIR_CONDITION(
                                        create_mode >= 0,
                                        "createMode",
                                        JSON_BUILD_STRING(create_mode_to_string(create_mode))),
                                JSON_BUILD_PAIR_STRING_NON_EMPTY("template", template),
                                JSON_BUILD_PAIR_CONDITION(
                                        read_only >= 0,
                                        "readOnly",
                                        JSON_BUILD_BOOLEAN(read_only)),
                                JSON_BUILD_PAIR_STRING("requestAs", request_as),
                                JSON_BUILD_PAIR_CONDITION(
                                        create_size != UINT64_MAX,
                                        "createSizeBytes",
                                        JSON_BUILD_UNSIGNED(create_size)),
                                JSON_BUILD_PAIR_BOOLEAN("allowInteractiveAuthentication", arg_ask_password)));
}

static int storage_varlink_error_to_errno(const char *error_id, JsonVariant *reply) {
        struct {
                int error;
        } p = {};

        static const JsonDispatch dispatch_table[] = {
                { "errno", JSON_VARIANT_INTEGER, json_dispatch_int32, offsetof(typeof(p), error), JSON_MANDATORY },
                {}
        };

        int r;

        assert(error_id);

        if (!streq(error_id, VARLINK_ERROR_SYSTEM))
                return -EBADR;

        r = json_dispatch(reply, dispatch_table, 0, &p);
        if (r < 0)
                return r;
        if (p.error <= 0)
                return -EPROTO;

        return -p.error;
}

static int mount_helper_idmap_fd(int fd, uid_t base_uid, gid_t base_gid) {
        _cleanup_free_ char *uid_line = NULL, *gid_line = NULL;
        _cleanup_close_ int userns_fd = -EBADF, mount_fd = -EBADF;

        if (asprintf(
                            &uid_line,
                            UID_FMT " " UID_FMT " " UID_FMT "\n",
                            base_uid, (uid_t) 0, (uid_t) 0x10000) < 0)
                return -ENOMEM;
        if (asprintf(
                            &gid_line,
                            GID_FMT " " GID_FMT " " GID_FMT "\n",
                            base_gid, (gid_t) 0, (gid_t) 0x10000) < 0)
                return -ENOMEM;

        userns_fd = userns_acquire(uid_line, gid_line);
        if (userns_fd < 0)
                return userns_fd;

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
                                .attr_set = MOUNT_ATTR_IDMAP,
                                .userns_fd = userns_fd,
                        },
                        MOUNT_ATTR_SIZE_VER0) < 0)
                return -errno;

        return TAKE_FD(mount_fd);
}

static int run_as_mount_helper(int argc, char *argv[]) {
        const char *fstype = NULL, *options = NULL;
        bool fake = false;
        int c, r;

        while ((c = getopt(argc, argv, "sfnvN:o:t:")) >= 0)
                switch (c) {
                case 'f':
                        fake = true;
                        break;
                case 'o':
                        options = optarg;
                        break;
                case 't':
                        fstype = startswith(optarg, "storage.");
                        if (fstype) {
                                if (startswith(fstype, "storage.") || streq(fstype, "storage"))
                                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Refusing nested storage volumes.");
                        } else if (!streq(optarg, "storage"))
                                log_warning("Unexpected file system type '%s', ignoring.", optarg);
                        break;
                case 's':
                case 'n':
                case 'v':
                        log_debug("Ignoring option -%c, not implemented.", c);
                        break;
                case 'N':
                        return log_error_errno(SYNTHETIC_ERRNO(EOPNOTSUPP), "Option -%c is not implemented, refusing.", c);
                case '?':
                        return -EINVAL;
                default:
                        assert_not_reached();
                }

        if (optind + 2 != argc)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Expected a storage volume specification and target directory as only arguments.");

        const char *colon = strchr(argv[optind], ':');
        if (!colon)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                       "Invalid storage volume specification, refusing: %s", argv[optind]);

        _cleanup_free_ char *provider = strndup(argv[optind], colon - argv[optind]);
        if (!provider)
                return log_oom();
        if (!storage_provider_name_is_valid(provider))
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Invalid storage provider name: %s", provider);

        const char *name = colon + 1;
        if (!storage_volume_name_is_valid(name))
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Invalid storage volume name: %s", name);

        _cleanup_free_ char *path = NULL;
        r = parse_path_argument(argv[optind + 1], /* suppress_root= */ false, &path);
        if (r < 0)
                return r;

        _cleanup_free_ char *filtered = NULL, *template = NULL;
        CreateMode create_mode = _CREATE_MODE_INVALID;
        uint64_t create_size = UINT64_MAX;
        int read_only = -1;

        for (const char *p = strempty(options);;) {
                _cleanup_free_ char *word = NULL;

                r = extract_first_word(&p, &word, ",", EXTRACT_KEEP_QUOTE|EXTRACT_UNESCAPE_SEPARATORS);
                if (r < 0)
                        return log_error_errno(r, "Failed to extract mount option: %m");
                if (r == 0)
                        break;

                const char *storage_option = startswith(word, "storage.");
                if (storage_option) {
                        const char *value;

                        if ((value = startswith(storage_option, "create="))) {
                                create_mode = create_mode_from_string(value);
                                if (create_mode < 0)
                                        return log_error_errno(create_mode, "Failed to parse storage.create= parameter: %s", value);
                        } else if ((value = startswith(storage_option, "create-size="))) {
                                r = parse_size(value, 1024, &create_size);
                                if (r < 0)
                                        return log_error_errno(r, "Failed to parse storage.create-size= parameter: %s", value);
                        } else if ((value = startswith(storage_option, "template="))) {
                                if (!storage_template_name_is_valid(value))
                                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Invalid template name, refusing: %s", value);

                                r = free_and_strdup(&template, value);
                                if (r < 0)
                                        return log_oom();
                        } else
                                return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Unknown mount option '%s', refusing.", word);
                } else if (streq(word, "ro"))
                        read_only = true;
                else if (streq(word, "rw"))
                        read_only = false;
                else if (!strextend_with_separator(&filtered, ",", word))
                        return log_oom();
        }

        if (fake)
                return 0;

        _cleanup_free_ char *socket_path = NULL;
        r = storage_provider_directory(&socket_path);
        if (r < 0)
                return log_error_errno(r, "Failed to determine socket directory: %m");
        if (!path_extend(&socket_path, provider))
                return log_oom();

        _cleanup_(varlink_unrefp) Varlink *link = NULL;
        r = varlink_connect_address(&link, socket_path);
        if (r < 0)
                return log_error_errno(r, "Failed to connect to '%s': %m", socket_path);

        r = varlink_set_allow_fd_passing_input(link, true);
        if (r < 0)
                return log_error_errno(r, "Failed to enable file descriptor passing: %m");

        (void) polkit_agent_open_if_enabled(BUS_TRANSPORT_LOCAL, arg_ask_password);

        JsonVariant *mreply = NULL;
        const char *merror_id = NULL, *vtype = fstype ? "reg" : "dir";
        r = acquire_volume(link, name, create_mode, template, read_only, vtype, create_size, &mreply, &merror_id);
        if (r < 0)
                return log_error_errno(r, "Failed to issue io.systemd.StorageProvider.Acquire() call: %m");

        _cleanup_(json_variant_unrefp) JsonVariant *reply = json_variant_ref(mreply);
        _cleanup_free_ char *error_id = merror_id ? strdup(merror_id) : NULL;
        if (merror_id && !error_id)
                return log_oom();

        if (error_id && streq(vtype, "reg") &&
            STR_IN_SET(error_id,
                       "io.systemd.StorageProvider.TypeNotSupported",
                       "io.systemd.StorageProvider.WrongType")) {
                JsonVariant *freply = NULL;
                const char *ferror_id = NULL;

                r = acquire_volume(link, name, create_mode, template, read_only, "blk", create_size, &freply, &ferror_id);
                if (r < 0)
                        return log_error_errno(r, "Failed to retry io.systemd.StorageProvider.Acquire() call: %m");
                if (!ferror_id) {
                        json_variant_unref(reply);
                        reply = json_variant_ref(freply);
                        error_id = mfree(error_id);
                        vtype = "blk";
                }
        }

        if (error_id) {
                if (streq(error_id, "io.systemd.StorageProvider.NoSuchVolume"))
                        return log_error_errno(SYNTHETIC_ERRNO(ENOENT), "Volume '%s' not known.", name);
                if (streq(error_id, "io.systemd.StorageProvider.NoSuchTemplate"))
                        return log_error_errno(SYNTHETIC_ERRNO(ENOENT), "Template '%s' not known.", strna(template));
                if (streq(error_id, "io.systemd.StorageProvider.VolumeExists"))
                        return log_error_errno(SYNTHETIC_ERRNO(EEXIST), "Volume '%s' exists already.", name);
                if (streq(error_id, "io.systemd.StorageProvider.TypeNotSupported"))
                        return log_error_errno(SYNTHETIC_ERRNO(EOPNOTSUPP), "Storage provider does not support volume type '%s'.", vtype);
                if (streq(error_id, "io.systemd.StorageProvider.WrongType"))
                        return log_error_errno(SYNTHETIC_ERRNO(EADDRNOTAVAIL), "Volume '%s' is not of type '%s'.", name, vtype);
                if (streq(error_id, "io.systemd.StorageProvider.CreateNotSupported"))
                        return log_error_errno(SYNTHETIC_ERRNO(EOPNOTSUPP), "Storage provider does not support creating volumes.");
                if (streq(error_id, "io.systemd.StorageProvider.CreateSizeRequired"))
                        return log_error_errno(SYNTHETIC_ERRNO(ENODATA), "Storage provider requires storage.create-size=.");
                if (streq(error_id, "io.systemd.StorageProvider.ReadOnlyVolume"))
                        return log_error_errno(SYNTHETIC_ERRNO(EROFS), "Volume '%s' is read-only.", name);
                if (streq(error_id, "io.systemd.StorageProvider.BadTemplate"))
                        return log_error_errno(SYNTHETIC_ERRNO(EADDRNOTAVAIL), "Template does not apply to this volume type.");

                r = storage_varlink_error_to_errno(error_id, reply);
                if (r != -EBADR)
                        return log_error_errno(r, "Storage provider call failed: %m");

                return log_error_errno(SYNTHETIC_ERRNO(EPROTO), "Storage provider call failed: %s", error_id);
        }

        struct {
                unsigned fd_idx;
                int read_only;
                const char *type;
                uid_t base_uid;
                gid_t base_gid;
        } p = {
                .fd_idx = UINT_MAX,
                .read_only = -1,
                .base_uid = UID_INVALID,
                .base_gid = GID_INVALID,
        };

        static const JsonDispatch dispatch_table[] = {
                { "fileDescriptorIndex", _JSON_VARIANT_TYPE_INVALID, json_dispatch_uint,         offsetof(typeof(p), fd_idx),    JSON_MANDATORY },
                { "readOnly",            JSON_VARIANT_BOOLEAN,      json_dispatch_tristate,     offsetof(typeof(p), read_only), 0              },
                { "type",                JSON_VARIANT_STRING,       json_dispatch_const_string, offsetof(typeof(p), type),      JSON_MANDATORY },
                { "baseUID",             _JSON_VARIANT_TYPE_INVALID, json_dispatch_uid_gid,     offsetof(typeof(p), base_uid),  0              },
                { "baseGID",             _JSON_VARIANT_TYPE_INVALID, json_dispatch_uid_gid,     offsetof(typeof(p), base_gid),  0              },
                {}
        };

        r = json_dispatch(reply, dispatch_table, 0, &p);
        if (r < 0)
                return log_error_errno(r, "Failed to decode Acquire() reply: %m");

        _cleanup_close_ int fd = varlink_take_fd(link, p.fd_idx);
        if (fd < 0)
                return log_error_errno(fd, "Failed to acquire fd from Varlink connection: %m");

        struct stat st;
        if (fstat(fd, &st) < 0)
                return log_error_errno(errno, "Failed to stat returned file descriptor: %m");

        _cleanup_strv_free_ char **cmdline = strv_new("mount", "-c");
        if (!cmdline)
                return log_oom();

        if (fstype) {
                if (!STR_IN_SET(p.type, "reg", "blk") || !IN_SET(st.st_mode & S_IFMT, S_IFREG, S_IFBLK))
                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                               "Volume cannot be mounted as file system type '%s'.", fstype);

                if (strv_extend_strv(&cmdline, STRV_MAKE("-t", fstype), false) < 0)
                        return log_oom();
        } else {
                if (!streq(p.type, "dir") || !S_ISDIR(st.st_mode))
                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Volume is not a directory.");
                if (!uid_is_valid(p.base_uid) || !gid_is_valid(p.base_gid))
                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Provider did not report base UID/GID.");
                if (p.base_uid > UINT32_MAX - 0x10000U || p.base_gid > UINT32_MAX - 0x10000U)
                        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "Returned base UID/GID out of range.");
                if (st.st_uid < p.base_uid || st.st_uid >= p.base_uid + 0x10000 ||
                    st.st_gid < p.base_gid || st.st_gid >= p.base_gid + 0x10000)
                        return log_error_errno(SYNTHETIC_ERRNO(EPERM), "Directory ownership is outside the reported UID/GID range.");

                _cleanup_close_ int remapped_fd = mount_helper_idmap_fd(fd, p.base_uid, p.base_gid);
                if (remapped_fd < 0)
                        return log_error_errno(remapped_fd, "Failed to apply ID mapping to returned mount: %m");

                close_and_replace(fd, remapped_fd);
                if (strv_extend(&cmdline, "--bind") < 0)
                        return log_oom();
        }

        if (p.read_only > 0)
                read_only = true;
        if (!strextend_with_separator(&filtered, ",", read_only > 0 ? "ro" : "rw"))
                return log_oom();
        if (strv_extend_strv(&cmdline, STRV_MAKE("-o", filtered), false) < 0)
                return log_oom();
        if (strv_extend_strv(&cmdline, STRV_MAKE(FORMAT_PROC_FD_PATH(fd), path), false) < 0)
                return log_oom();

        r = fd_cloexec(fd, false);
        if (r < 0)
                return log_error_errno(r, "Failed to disable O_CLOEXEC for mount fd: %m");

        if (DEBUG_LOGGING) {
                _cleanup_free_ char *q = quote_command_line(cmdline, SHELL_ESCAPE_EMPTY);
                log_debug("Chain-loading: %s", strna(q));
        }

        execv("/bin/mount", cmdline);
        return log_error_errno(errno, "Failed to execute mount tool: %m");
}

static int run(int argc, char *argv[]) {
        static const Verb verbs[] = {
                { "volumes",   1,        2,        VERB_DEFAULT, verb_list_volumes   },
                { "templates", 1,        2,        0,            verb_list_templates },
                { "providers", 1,        1,        0,            verb_providers      },
                { "help",      VERB_ANY, VERB_ANY, 0,            verb_help           },
                {}
        };

        int r;

        log_setup();

        if (invoked_as(argv, "mount.storage"))
                return run_as_mount_helper(argc, argv);

        r = parse_argv(argc, argv);
        if (r <= 0)
                return r;

        return dispatch_verb(argc, argv, verbs, NULL);
}

DEFINE_MAIN_FUNCTION(run);
