/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>


#if HAVE_USEC
#include <usec/usec.h>
#endif

#include "log.h"
#include "macro.h"
#include "usec-setup.h"
#include "usec-util.h"
#include "string-util.h"
#include "initrd-util.h"

#if HAVE_USEC
_printf_(2,3)
static int null_log(int type, const char *fmt, ...) {
        return 0;
}
#endif

int mac_usec_setup(bool *loaded_policy) {

 
#if HAVE_USEC
        int enforce = 0;
        usec_t before_load, after_load;
        char *con;
        int r;


        assert(loaded_policy);

        /* Turn off all of USEC' own logging, we want to do that */
        usec_set_callback(USEC_CB_LOG, (const union usec_callback) { .func_log = null_log });


        /* Don't load policy in the initrd if we don't appear to have
         * it.  For the real root, we check below if we've already
         * loaded policy, and return gracefully.
         */
        if (in_initrd() && access(usec_path(), F_OK) < 0)
                return 0;

        /* Make sure we have no fds open while loading the policy and
         * transitioning */
        log_close();

        /* Now load the policy */
        before_load = now(CLOCK_MONOTONIC);
        r = usec_init_load_policy(&enforce);
        if (r == 0) {
                char timespan[FORMAT_TIMESPAN_MAX];

                mac_usec_reset();

                log_open();

                after_load = now(CLOCK_MONOTONIC);

                log_info("Successfully loaded USEC policy in %s.",
                         format_timespan(timespan, sizeof(timespan), after_load - before_load, 0));
        
                        *loaded_policy = true;
        } else {
                log_open();
                if (enforce > 0) {
                        log_warning("Failed to load new USEC policy. Continuing with old policy.");
                }else{
                        log_debug("Unable to load USEC policy. Ignoring.");
                }
        }
#endif

        return 0;
}
