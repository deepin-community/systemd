/* SPDX-License-Identifier: LGPL-2.1+ */

#if HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/usec.h>
#endif
#include "umac-util.h"
#include "string-util.h"
#include "log.h"

#if HAVE_SELINUX
static int cached_use = -1;
#endif

bool umac_use(void) {
#if HAVE_SELINUX
        if (cached_use < 0){
                cached_use = is_usec_enabled() > 0;
                log_trace("USEC enabled state cached to: %s", enabled_disabled(cached_use));
        }

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
