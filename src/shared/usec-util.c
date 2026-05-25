/* SPDX-License-Identifier: LGPL-2.1+ */

#if HAVE_USEC
#include <usec/usec.h>
#include <usec/avc.h>
#endif
#include "usec-util.h"
#include "string-util.h"
#include "log.h"

#if HAVE_USEC
static int cached_use = -1;
#endif

bool mac_usec_use(void) {
#if HAVE_USEC
        if (cached_use < 0){
                cached_use = is_usec_enabled_v1() > 0;
                log_trace("USEC enabled state cached to: %s", enabled_disabled(cached_use));
        }

        return cached_use;
#else
        return false;
#endif
}

void mac_usec_reset(void) {
#if HAVE_USEC
        cached_use = -1;
#endif
}

int mac_usec_init(void) {
        int r = 0;

#if HAVE_USEC
        if (!mac_usec_use())
                return 0;
#endif
        return r;
}

void mac_usec_finish(void) {

#if HAVE_USEC
        usec_status_close();

#endif
}
