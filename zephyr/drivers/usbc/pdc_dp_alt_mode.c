/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_pdc_dp_alt_mode

#include "timer.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pdc_dp_alt_mode, LOG_LEVEL_INF);

BUILD_ASSERT(
	DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	"Exactly one instance of cros-ec,pdc-dp-alt-mode should be defined.");

struct pdc_dp_alt_mode_config {
	struct gpio_dt_spec hpd_gpio;
	struct gpio_dt_spec aux_path_sel_gpio;
};

static const struct pdc_dp_alt_mode_config dp_alt_mode_cfg = {
	.hpd_gpio = GPIO_DT_SPEC_INST_GET(0, hpd_gpios),
	.aux_path_sel_gpio =
		GPIO_DT_SPEC_INST_GET_OR(0, aux_path_sel_gpios, { 0 }),
};

static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];
static int active_aux_port = -1;

static int dp_get_hpd_gpio(int port)
{
	if (active_aux_port != port) {
		return 0;
	}

	return gpio_pin_get_dt(&dp_alt_mode_cfg.hpd_gpio);
}

static void dp_set_aux_path_sel(int port)
{
	if (dp_alt_mode_cfg.aux_path_sel_gpio.port == NULL) {
		return;
	}

	gpio_pin_set_dt(&dp_alt_mode_cfg.aux_path_sel_gpio, port);
	LOG_INF("Set DP_AUX_PATH_SEL: %d", port);
}

static void dp_set_hpd_gpio(int port, int en)
{
	/*
	 * Implement FCFS policy:
	 * 1) Enable HPD if no active port.
	 * 2) Disable HPD if active port is the given port.
	 */
	if (en && active_aux_port < 0) {
		gpio_pin_set_dt(&dp_alt_mode_cfg.hpd_gpio, 1);
		active_aux_port = port;
	}

	if (!en && active_aux_port == port) {
		gpio_pin_set_dt(&dp_alt_mode_cfg.hpd_gpio, 0);
		active_aux_port = -1;
	}
}

static bool is_dp_muxable(int port)
{
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i != port && dp_get_hpd_gpio(i)) {
			return false;
		}
	}

	return true;
}

static void pdc_dp_attention_cb(int port, uint32_t vdo_dp_status)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(vdo_dp_status);
	int irq = PD_VDO_DPSTS_HPD_IRQ(vdo_dp_status);

	if (!is_dp_muxable(port)) {
		LOG_INF("p%d: The other port is already muxed.", port);
		return;
	}

	int cur_lvl = dp_get_hpd_gpio(port);

	if (lvl) {
		dp_set_aux_path_sel(port);
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
		if (now < hpd_deadline[port]) {
			k_usleep(hpd_deadline[port] - now);
		}

		/* Generate IRQ_HPD pulse */
		dp_set_hpd_gpio(port, 0);
		/*
		 * b/171172053#comment14: since the HPD_DSTREAM_DEBOUNCE_IRQ is
		 * very short (500us), we can use k_busy_wait for more stable
		 * pulse period.
		 */
		k_busy_wait(HPD_DSTREAM_DEBOUNCE_IRQ);
		dp_set_hpd_gpio(port, 1);
	} else {
		dp_set_hpd_gpio(port, lvl);
	}

	/* Set the minimum time delay (2ms) for the next HPD IRQ */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

static void pdc_dp_unattach_cb(int port)
{
	dp_set_hpd_gpio(port, 0);
}

static int pdc_dp_alt_mode_init(const struct device *dev)
{
	const struct pdc_dp_alt_mode_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->hpd_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->hpd_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->hpd_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure HPD GPIO: %d", ret);
		return ret;
	}

	if (cfg->aux_path_sel_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->aux_path_sel_gpio)) {
			LOG_ERR_DEVICE_NOT_READY(cfg->aux_path_sel_gpio.port);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->aux_path_sel_gpio,
					    GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure AUX path GPIO: %d", ret);
			return ret;
		}
	}

	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_UNATTACH,
					       pdc_dp_unattach_cb);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_DP_ATTENTION,
					       pdc_dp_attention_cb);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, pdc_dp_alt_mode_init, NULL, NULL, &dp_alt_mode_cfg,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
