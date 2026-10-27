/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dougie_usbc, LOG_LEVEL_INF);

uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];
static int active_aux_port = -1;

int svdm_get_hpd_gpio(int port)
{
	if (active_aux_port != port) {
		return 0;
	}

	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd));
}

static void set_dp_path_sel(int port)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_dp_aux_path_sel), port);
	LOG_INF("Set DP_AUX_PATH_SEL: %d", port);
}

void svdm_set_hpd_gpio(int port, int en)
{
	/*
	 * Implement FCFS policy:
	 * 1) Enable HPD if no active port.
	 * 2) Disable HPD if active port is the given port.
	 */
	if (en && active_aux_port < 0) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd), 1);
		active_aux_port = port;
	}

	if (!en && active_aux_port == port) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd), 0);
		active_aux_port = -1;
	}
}

static bool is_dp_muxable(int port)
{
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port && svdm_get_hpd_gpio(i)) {
			return false;
		}
	}

	return true;
}

static void dougie_dp_attention(int port, uint32_t vdo_dp_status)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(vdo_dp_status);
	int irq = PD_VDO_DPSTS_HPD_IRQ(vdo_dp_status);

	if (!is_dp_muxable(port)) {
		LOG_INF("p%d: The other port is already muxed.", port);
		return;
	}

	int cur_lvl = svdm_get_hpd_gpio(port);

	if (lvl) {
		set_dp_path_sel(port);
	}

	if (irq && !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		LOG_ERR("ERR:HPD:IRQ&LOW");
		return;
	}

	if (irq && cur_lvl) {
		uint64_t now = get_time().val;

		/* Wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port]) {
			k_usleep(svdm_hpd_deadline[port] - now);
		}

		/* Generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		/*
		 * b/171172053#comment14: since the HPD_DSTREAM_DEBOUNCE_IRQ is
		 * very short (500us), we can use k_busy_wait for more stable
		 * pulse period.
		 */
		k_busy_wait(HPD_DSTREAM_DEBOUNCE_IRQ);
		svdm_set_hpd_gpio(port, 1);
	} else {
		svdm_set_hpd_gpio(port, lvl);
	}

	/* Set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

static void dougie_set_unattached(int port)
{
	svdm_set_hpd_gpio(port, 0);
}

static int dougie_pdc_cb_init(void)
{
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_UNATTACH,
					       dougie_set_unattached);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_DP_ATTENTION,
					       dougie_dp_attention);
	return 0;
}
SYS_INIT(dougie_pdc_cb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
