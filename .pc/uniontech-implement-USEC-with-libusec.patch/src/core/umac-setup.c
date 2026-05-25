/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#if HAVE_SELINUX
#include <selinux/selinux.h>
#endif

#include "log.h"
#include "macro.h"
#include "umac-setup.h"
#include "umac-util.h"
#include "string-util.h"
#include "initrd-util.h"

#if HAVE_SELINUX
static int null_policyload2(int seqno __attribute__((unused))) {
        return 0;
}
#endif

int umac_setup(bool *loaded_policy) {

#if HAVE_SELINUX
        int enforce = 0;
        usec_t before_load, after_load;
        int r;
        static union selinux_callback cb_get = {
                .func_policyload = NULL,
        };

        static const union selinux_callback cb_set = {
                .func_policyload = null_policyload2,
        };

        assert(loaded_policy);

		/* set selinux callback type for loading policy of umac
		 * this is the exclusive type of UOS usid
		*/
        selinux_set_callback(SELINUX_CB_POLICYLOAD_USEC, cb_set);

		/* if libselinux support loading policy of umac,
		 * cb_get.func_policyload will be non-null pointer. and then
		 * call selinux_init_load_policy, it will loading
		 * umac policy rather than selinux policy.
		*/
        cb_get = selinux_get_callback(SELINUX_CB_POLICYLOAD_USEC);
		if (!cb_get.func_policyload)
			return 0;

        /* Don't load policy in the initrd if we don't appear to have
         * it.  For the real root, we check below if we've already
         * loaded policy, and return gracefully.
         */
        if (in_initrd() && access(selinux_path(), F_OK) < 0)
                return 0;

        /* Make sure we have no fds open while loading the policy and
         * transitioning */
        log_close();

        /* Now load the policy */
        before_load = now(CLOCK_MONOTONIC);
        r = selinux_init_load_policy(&enforce);
        if (r == 0) {
                char timespan[FORMAT_TIMESPAN_MAX];

                umac_reset();

                log_open();

                after_load = now(CLOCK_MONOTONIC);

                log_info("Successfully loaded umac policy in %s.",
                         format_timespan(timespan, sizeof(timespan), after_load - before_load, 0));
        } else {
                log_open();
                log_warning("Failed to load new umac policy.");
        }
#endif

        return 0;
}
