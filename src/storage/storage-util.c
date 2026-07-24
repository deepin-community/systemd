/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "storage-util.h"
#include "string-table.h"

static const char *volume_type_table[_VOLUME_TYPE_MAX] = {
        [VOLUME_BLK] = "blk",
        [VOLUME_REG] = "reg",
        [VOLUME_DIR] = "dir",
};

static const char *create_mode_table[_CREATE_MODE_MAX] = {
        [CREATE_ANY]  = "any",
        [CREATE_NEW]  = "new",
        [CREATE_OPEN] = "open",
};

DEFINE_STRING_TABLE_LOOKUP(volume_type, VolumeType);
DEFINE_STRING_TABLE_LOOKUP(create_mode, CreateMode);

int json_dispatch_volume_type(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        VolumeType *type = ASSERT_PTR(userdata);

        assert(variant);

        *type = volume_type_from_string(json_variant_string(variant));
        return *type < 0 ? -EINVAL : 0;
}

int json_dispatch_create_mode(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        CreateMode *mode = ASSERT_PTR(userdata);

        assert(variant);

        *mode = create_mode_from_string(json_variant_string(variant));
        return *mode < 0 ? -EINVAL : 0;
}
