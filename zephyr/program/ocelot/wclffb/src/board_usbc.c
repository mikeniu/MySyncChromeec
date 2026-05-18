/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* wclffb board-specific USB-C configuration */

#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif

#include "battery.h"
#include "console.h"
#include "driver/tcpm/anx7447.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

#define USBC_PORT_C0 0

void board_reset_pd_mcu(void)
{
	CPRINTS("Resetting TCPCs...");
	/* reset C0 ANX3447 */
	/* Assert reset */
	gpio_pin_set_dt(&tcpc_config[0].rst_gpio, 1);
	k_msleep(1);
	gpio_pin_set_dt(&tcpc_config[0].rst_gpio, 0);
	/* After TEST_R release, anx7447/3447 needs 2ms to finish eFuse
	 * loading.
	 */
	k_msleep(2);
}

void board_tcpc_init(void)
{
	/* Reset TCPC if we only we have a battery connected, or the SINK
	 * gpio to the PPC might be reset and cause brown-out.
	 */
	if (!system_jumped_late() && battery_is_present() == BP_YES) {
		board_reset_pd_mcu();
	}

	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_POST_I2C);

int board_set_active_charge_port(int port)
{
	/* TODO */
	return EC_SUCCESS;
}

__override int board_get_vbus_voltage(int port)
{
	int voltage = 0;
	int rv;

	switch (port) {
	case USBC_PORT_C0:
		rv = tcpc_config[USBC_PORT_C0].drv->get_vbus_voltage(port,
								     &voltage);
		if (rv)
			return 0;
		break;
	default:
		return 0;
	}
	return voltage;
}

int pd_check_vconn_swap(int port)
{
	/* TODO */
	return 0;
}

void pd_power_supply_reset(int port)
{
	/* TODO */
}

int pd_set_power_supply_ready(int port)
{
	/* TODO */
	return EC_SUCCESS;
}
