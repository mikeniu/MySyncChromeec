/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define DP_ALT_MODE_NODE DT_NODELABEL(dp_alt_mode)
#define VDO_DP_HPD(lvl, irq) VDO_DP_STATUS((irq), (lvl), 0, 0, 0, 1, 0, 1)

static const struct device *const dp_dev = DEVICE_DT_GET(DP_ALT_MODE_NODE);
static const struct gpio_dt_spec hpd_gpio =
	GPIO_DT_SPEC_GET(DP_ALT_MODE_NODE, hpd_gpios);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
static const struct gpio_dt_spec aux_gpio =
	GPIO_DT_SPEC_GET(DP_ALT_MODE_NODE, aux_path_sel_gpios);
#endif

static pdc_power_mgmt_board_unattached_cb registered_unattach_cb;
static pdc_power_mgmt_board_dp_attention_cb registered_dp_attention_cb;
static int hpd_edge_count;
static struct gpio_callback hpd_gpio_cb;

int pdc_power_mgmt_register_board_callback(enum pdc_power_mgmt_board_cb_t type,
					   const void *callback)
{
	switch (type) {
	case PDC_BOARD_CB_UNATTACH:
		registered_unattach_cb = callback;
		break;
	case PDC_BOARD_CB_DP_ATTENTION:
		registered_dp_attention_cb = callback;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

uint8_t pdc_power_mgmt_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

int pdc_power_mgmt_get_pch_data_status(int port, uint8_t *status)
{
	ARG_UNUSED(port);
	ARG_UNUSED(status);
	return 0;
}

static void hpd_edge_isr(const struct device *port, struct gpio_callback *cb,
			 gpio_port_pins_t pins)
{
	hpd_edge_count++;
}

static void pdc_dp_alt_mode_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_not_null(registered_unattach_cb);
	zassert_not_null(registered_dp_attention_cb);

	/*
	 * Enable GPIO_INPUT on gpio_emul alongside GPIO_OUTPUT so that
	 * gpio_pin_get_dt() inside the driver reads back the driven output.
	 */
	zassert_ok(gpio_pin_configure_dt(&hpd_gpio,
					 GPIO_OUTPUT_INACTIVE | GPIO_INPUT));

	hpd_edge_count = 0;
}

static void pdc_dp_alt_mode_after(void *fixture)
{
	ARG_UNUSED(fixture);

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		registered_unattach_cb(i);
	}
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_ok(gpio_pin_set_dt(&aux_gpio, 1));
#endif
}

ZTEST(pdc_dp_alt_mode, test_init)
{
	zassert_true(device_is_ready(dp_dev));
	zassert_not_null(registered_unattach_cb);
	zassert_not_null(registered_dp_attention_cb);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 0);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_equal(gpio_emul_output_get(aux_gpio.port, aux_gpio.pin), 1);
#endif
}

ZTEST(pdc_dp_alt_mode, test_dp_attention_hpd_level)
{
	/* Port 0 asserts HPD level */
	registered_dp_attention_cb(0, VDO_DP_HPD(1, 0));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_equal(gpio_emul_output_get(aux_gpio.port, aux_gpio.pin), 0);
#endif

	/* Port 0 deasserts HPD level */
	registered_dp_attention_cb(0, VDO_DP_HPD(0, 0));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 0);
}

ZTEST(pdc_dp_alt_mode, test_fcfs_arbitration_and_unattach)
{
	/* Port 0 claims DP first */
	registered_dp_attention_cb(0, VDO_DP_HPD(1, 0));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_equal(gpio_emul_output_get(aux_gpio.port, aux_gpio.pin), 0);
#endif

	/* Port 1 DP attention is ignored while Port 0 is active */
	registered_dp_attention_cb(1, VDO_DP_HPD(1, 0));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_equal(gpio_emul_output_get(aux_gpio.port, aux_gpio.pin), 0);
#endif

	/* Unattaching inactive Port 1 keeps Port 0 active */
	registered_unattach_cb(1);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);

	/* Unattaching active Port 0 deasserts HPD and frees the mux */
	registered_unattach_cb(0);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 0);

	/* Port 1 can now claim DP */
	registered_dp_attention_cb(1, VDO_DP_HPD(1, 0));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);
#if DT_NODE_HAS_PROP(DP_ALT_MODE_NODE, aux_path_sel_gpios)
	zassert_equal(gpio_emul_output_get(aux_gpio.port, aux_gpio.pin), 1);
#endif
}

ZTEST(pdc_dp_alt_mode, test_hpd_irq_pulse)
{
	gpio_init_callback(&hpd_gpio_cb, hpd_edge_isr, BIT(hpd_gpio.pin));
	zassert_ok(gpio_add_callback(hpd_gpio.port, &hpd_gpio_cb));
	zassert_ok(
		gpio_pin_interrupt_configure_dt(&hpd_gpio, GPIO_INT_EDGE_BOTH));

	/* Assert HPD high (1 rising edge) */
	registered_dp_attention_cb(0, VDO_DP_HPD(1, 0));
	zassert_equal(hpd_edge_count, 1);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);

	/* Trigger IRQ_HPD while high: should pulse low then high (+2 edges) */
	registered_dp_attention_cb(0, VDO_DP_HPD(1, 1));
	zassert_equal(hpd_edge_count, 3);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);

	/* Second immediate IRQ_HPD waits for debounce deadline and pulses */
	registered_dp_attention_cb(0, VDO_DP_HPD(1, 1));
	zassert_equal(hpd_edge_count, 5);
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 1);

	zassert_ok(
		gpio_pin_interrupt_configure_dt(&hpd_gpio, GPIO_INT_DISABLE));
	zassert_ok(gpio_remove_callback(hpd_gpio.port, &hpd_gpio_cb));
}

ZTEST(pdc_dp_alt_mode, test_hpd_irq_with_low_lvl_ignored)
{
	/* IRQ=1 with LVL=0 is invalid and must not assert HPD */
	registered_dp_attention_cb(0, VDO_DP_HPD(0, 1));
	zassert_equal(gpio_emul_output_get(hpd_gpio.port, hpd_gpio.pin), 0);
}

ZTEST_SUITE(pdc_dp_alt_mode, NULL, NULL, pdc_dp_alt_mode_before,
	    pdc_dp_alt_mode_after, NULL);
