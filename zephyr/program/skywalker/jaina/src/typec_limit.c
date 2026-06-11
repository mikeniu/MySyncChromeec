/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/init.h>

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Override the default 3A port allocation.
 * This serves as the single source of truth for the DPM's 3A budget.
 */
__override int pd_get_usb_pd_3a_ports(void)
{
#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING
	/* Block all 3A allocations if the system is currently overloaded */
	if (pdc_is_battery_overloaded()) {
		return 0;
	}
#endif
	/* Normal operation: return the system's default 3A budget */
	return CONFIG_USB_PD_3A_PORTS;
}

/* Callback triggered upon Type-C device detachment */
static void jaina_pdc_unattached_cb(int port)
{
#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING
	/* If a detachment occurs while overloaded, release the lock. */
	if (pdc_clear_battery_overload()) {
		CPRINTS("Jaina Overload: Port %d removed, restoring 3A capacity.",
			port);

		/* Let the DPM handle the normal rebalancing.
		 * This cleanly redistributes the 3A budget without exceeding
		 * CONFIG_USB_PD_3A_PORTS.
		 */
		pdc_dpm_remove_sink(port);
	}
#endif
}

static int jaina_overload_init(void)
{
	pdc_power_mgmt_register_board_callback(
		PDC_BOARD_CB_UNATTACH, (const void *)jaina_pdc_unattached_cb);

	return 0;
}
SYS_INIT(jaina_overload_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
