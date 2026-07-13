/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "varlink-io.systemd.StorageProvider.h"

static VARLINK_DEFINE_ENUM_TYPE(
                VolumeType,
                VARLINK_DEFINE_ENUM_VALUE(blk),
                VARLINK_DEFINE_ENUM_VALUE(reg),
                VARLINK_DEFINE_ENUM_VALUE(dir));

static VARLINK_DEFINE_ENUM_TYPE(
                CreateMode,
                VARLINK_DEFINE_ENUM_VALUE(any),
                VARLINK_DEFINE_ENUM_VALUE(new),
                VARLINK_DEFINE_ENUM_VALUE(open));

static VARLINK_DEFINE_METHOD(
                Acquire,
                VARLINK_DEFINE_INPUT(name, VARLINK_STRING, 0),
                VARLINK_DEFINE_INPUT_BY_TYPE(createMode, CreateMode, VARLINK_NULLABLE),
                VARLINK_DEFINE_INPUT(template, VARLINK_STRING, VARLINK_NULLABLE),
                VARLINK_DEFINE_INPUT(readOnly, VARLINK_BOOL, VARLINK_NULLABLE),
                VARLINK_DEFINE_INPUT_BY_TYPE(requestAs, VolumeType, VARLINK_NULLABLE),
                VARLINK_DEFINE_INPUT(createSizeBytes, VARLINK_INT, VARLINK_NULLABLE),
                VARLINK_DEFINE_INPUT(allowInteractiveAuthentication, VARLINK_BOOL, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(fileDescriptorIndex, VARLINK_INT, 0),
                VARLINK_DEFINE_OUTPUT_BY_TYPE(type, VolumeType, 0),
                VARLINK_DEFINE_OUTPUT(readOnly, VARLINK_BOOL, 0),
                VARLINK_DEFINE_OUTPUT(baseUID, VARLINK_INT, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(baseGID, VARLINK_INT, VARLINK_NULLABLE));

static VARLINK_DEFINE_METHOD(
                ListVolumes,
                VARLINK_DEFINE_INPUT(matchName, VARLINK_STRING, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(name, VARLINK_STRING, 0),
                VARLINK_DEFINE_OUTPUT(aliases, VARLINK_STRING, VARLINK_NULLABLE|VARLINK_ARRAY),
                VARLINK_DEFINE_OUTPUT_BY_TYPE(type, VolumeType, 0),
                VARLINK_DEFINE_OUTPUT(readOnly, VARLINK_BOOL, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(sizeBytes, VARLINK_INT, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(usedBytes, VARLINK_INT, VARLINK_NULLABLE));

static VARLINK_DEFINE_METHOD(
                ListTemplates,
                VARLINK_DEFINE_INPUT(matchName, VARLINK_STRING, VARLINK_NULLABLE),
                VARLINK_DEFINE_OUTPUT(name, VARLINK_STRING, 0),
                VARLINK_DEFINE_OUTPUT_BY_TYPE(type, VolumeType, 0));

static VARLINK_DEFINE_ERROR(NoSuchVolume);
static VARLINK_DEFINE_ERROR(VolumeExists);
static VARLINK_DEFINE_ERROR(NoSuchTemplate);
static VARLINK_DEFINE_ERROR(TypeNotSupported);
static VARLINK_DEFINE_ERROR(WrongType);
static VARLINK_DEFINE_ERROR(CreateNotSupported);
static VARLINK_DEFINE_ERROR(CreateSizeRequired);
static VARLINK_DEFINE_ERROR(ReadOnlyVolume);
static VARLINK_DEFINE_ERROR(BadTemplate);

VARLINK_DEFINE_INTERFACE(
                io_systemd_StorageProvider,
                "io.systemd.StorageProvider",
                &vl_type_VolumeType,
                &vl_type_CreateMode,
                &vl_method_Acquire,
                &vl_method_ListVolumes,
                &vl_method_ListTemplates,
                &vl_error_NoSuchVolume,
                &vl_error_VolumeExists,
                &vl_error_NoSuchTemplate,
                &vl_error_TypeNotSupported,
                &vl_error_WrongType,
                &vl_error_CreateNotSupported,
                &vl_error_CreateSizeRequired,
                &vl_error_ReadOnlyVolume,
                &vl_error_BadTemplate);
