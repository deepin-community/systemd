/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>

#define SELINUX_CB_POLICYLOAD_USEC 999

int umac_setup(bool *loaded_policy);