/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "json.h"
#include "path-util.h"
#include "string-util.h"

typedef enum VolumeType {
        VOLUME_BLK,
        VOLUME_REG,
        VOLUME_DIR,
        _VOLUME_TYPE_MAX,
        _VOLUME_TYPE_INVALID = -EINVAL,
} VolumeType;

typedef enum CreateMode {
        CREATE_ANY,
        CREATE_NEW,
        CREATE_OPEN,
        _CREATE_MODE_MAX,
        _CREATE_MODE_INVALID = -EINVAL,
} CreateMode;

const char* volume_type_to_string(VolumeType t);
VolumeType volume_type_from_string(const char *s);

const char* create_mode_to_string(CreateMode m);
CreateMode create_mode_from_string(const char *s);

int json_dispatch_volume_type(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata);
int json_dispatch_create_mode(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata);

static inline bool storage_volume_name_is_valid(const char *name) {
        return string_is_safe(name);
}

static inline bool storage_template_name_is_valid(const char *name) {
        return string_is_safe(name);
}

static inline bool storage_provider_name_is_valid(const char *name) {
        return filename_is_valid(name);
}
