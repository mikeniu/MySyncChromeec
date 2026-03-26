/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DC Jack configuration */

#include "charge_manager.h"
#include "intelrvp.h"

#include <zephyr/devicetree.h>
#include <drivers/cros_irq_gpio.h>

bool is_typec_port(int port)
{
	return !(port == DEDICATED_CHARGE_PORT || port == CHARGE_PORT_NONE);
}

static void board_dc_jack_handler(const struct device *dev, void *data)
{
	struct charge_port_info charge_dc_jack;

	/* System is booted from DC Jack */
	if (irq_gpio_pin_get(dev)) {
		charge_dc_jack.current =
			min((CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW * 1000) /
				    DC_JACK_MAX_VOLTAGE_MV,
			    CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA);
		charge_dc_jack.voltage = DC_JACK_MAX_VOLTAGE_MV;
	} else {
		charge_dc_jack.current = 0;
		charge_dc_jack.voltage = 0;
	}

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &charge_dc_jack);
}

#ifdef CONFIG_USB_PDC_POWER_MGMT
CROS_IRQ_GPIO_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(dc_jack)), board_dc_jack_handler, NULL);
#else
DECLARE_HOOK(HOOK_INIT, board_charge_init, HOOK_PRIO_POST_CHARGE_MANAGER);
#endif
