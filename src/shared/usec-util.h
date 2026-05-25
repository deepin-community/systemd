/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>

bool mac_usec_use(void);
void mac_usec_reset(void);
int mac_usec_init(void);

void mac_usec_finish(void);