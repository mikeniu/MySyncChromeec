/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Shared definition of enum power_state, used by both the legacy EC (power.h)
 * and the Zephyr shim (power_host_sleep.h / pdc.h) so there is exactly one
 * definition regardless of which headers a translation unit includes.
 *
 * CONFIG_POWER_S0IX  - legacy EC symbol for S0ix-capable platforms
 * CONFIG_AP_PWRSEQ_S0IX - Zephyr AP power-sequencing S0ix symbol
 * Either being set enables the S0ix states.
 */

#ifndef __CROS_EC_POWER_STATE_DEFS_H
#define __CROS_EC_POWER_STATE_DEFS_H

enum power_state {
	/* Steady states */
	POWER_G3 = 0, /*
		       * System is off (not technically all the way into G3,
		       * which means totally unpowered...)
		       */
	POWER_S5, /* System is soft-off */
	POWER_S4, /* System is suspended to disk */
	POWER_S3, /* Suspend; RAM on, processor is asleep */
	POWER_S0, /* System is on */
#if defined(CONFIG_POWER_S0IX) || defined(CONFIG_AP_PWRSEQ_S0IX)
	POWER_S0ix,
#endif
	/* Transitions */
	POWER_G3S5, /* G3 -> S5 (at system init time) */
	POWER_S5S3, /* S5 -> S3 (skips S4 on non-Intel systems) */
	POWER_S3S0, /* S3 -> S0 */
	POWER_S0S3, /* S0 -> S3 */
	POWER_S3S5, /* S3 -> S5 (skips S4 on non-Intel systems) */
	POWER_S5G3, /* S5 -> G3 */
	POWER_S3S4, /* S3 -> S4 */
	POWER_S4S3, /* S4 -> S3 */
	POWER_S4S5, /* S4 -> S5 */
	POWER_S5S4, /* S5 -> S4 */
#if defined(CONFIG_POWER_S0IX) || defined(CONFIG_AP_PWRSEQ_S0IX)
	POWER_S0ixS0, /* S0ix -> S0 */
	POWER_S0S0ix, /* S0 -> S0ix */
#endif
};

#endif /* __CROS_EC_POWER_STATE_DEFS_H */
