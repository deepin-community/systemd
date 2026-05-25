/* SPDX-License-Identifier: LGPL-2.1+ */

#if HAVE_SELINUX
#include <selinux/selinux.h>
#endif
#include "umac-util.h"

#if HAVE_SELINUX
static int cached_use = -1;
#endif

bool umac_use(void) {
#if HAVE_SELINUX
        if (cached_use < 0)
                cached_use = security_getenforce() >= 0;

        return cached_use;
#else
        return false;
#endif
}

void umac_reset(void) {
#if HAVE_SELINUX
        cached_use = -1;
#endif
}

int umac_init(void) {
        int r = 0;

#if HAVE_SELINUX
        if (!umac_use())
                return 0;
#endif
        return r;
}
