/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for Atria RVP only
 */

#include "drivers/pdc.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rvp_usbc, LOG_LEVEL_INF);

/* Ports supported by dual PDC chip in AIC */
#define DEV_PDC_C0_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0))
#define DEV_PDC_C0_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0))

#define DEV_PDC_C1_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1))
#define DEV_PDC_C1_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1))

#if CONFIG_USB_PD_PORT_MAX_COUNT > 2
/* Port in modular TCSS AIC */
#define DEV_PDC_C2_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c2))
#define DEV_PDC_C2_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c2))
#else
/* named-usbc-port is not declared. */
#define DEV_PDC_C2_RTK NULL
#define DEV_PDC_C2_TI NULL
#endif

/**
 * Bitfield for storing detected TCSS modules
 *  - C0 refers to TCP0 that is part of dual port AIC or Modular AIC
 *  - C1 refers to TCP1 that is part of dual port AIC or Modular AIC
 *  - C2 refers to TCP2 that is in modular AIC
 */
enum rvp_tcss_modules {
	RVP_TCSS_NONE = 0,
	RVP_TCSS_C0_RTK = BIT(0),
	RVP_TCSS_C1_RTK = BIT(1),
	RVP_TCSS_C2_RTK = BIT(2),
	RVP_TCSS_C0_TI = BIT(3),
	RVP_TCSS_C1_TI = BIT(4),
	RVP_TCSS_C2_TI = BIT(5),
	RVP_TCSS_DUAL_TI = BIT(3) | BIT(4),
	RVP_TCSS_DUAL_RTK = BIT(0) | BIT(1),
	RVP_TCSS_THREE_TI = BIT(3) | BIT(4) | BIT(5),
	RVP_TCSS_THREE_RTK = BIT(0) | BIT(1) | BIT(2),
};

static struct {
	bool initialized;
	enum rvp_tcss_modules detected_cards;
} ctx;

static bool probe_pdc_chip(const struct device *dev)
{
	struct pdc_hw_config_t config;
	int rv;

	if (dev == NULL) {
		/* LCOV_EXCL_START Unlikely to be NULL since these should be
		 * defined in the device tree for supported boards, but check to
		 * be safe */
		LOG_ERR("%s: Invalid pointer", __func__);
		return false;
		/* LCOV_EXCL_STOP */
	}

	rv = pdc_get_hw_config(dev, &config);
	if (rv) {
		/* LCOV_EXCL_START Unlikely that the device tree is
		 * misconfigured for a known board */
		LOG_ERR("%s: Cannot get bus info for PDC %s: %d", __func__,
			dev->name ? dev->name : "unnamed", rv);
		return false;
		/* LCOV_EXCL_STOP */
	}

	struct i2c_msg msgs[1];
	/* RTK EC I2C driver requires this to be a valid register address */
	uint8_t dst = 0x20;

	msgs[0].buf = &dst;
	msgs[0].len = 0U;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	/* If the transfer succeeds, a chip is at this address */
	return i2c_transfer_dt(&config.i2c, &msgs[0], 1) == 0;
}

static void discover_tcss_modules()
{
	if (ctx.initialized) {
		/* Only run once */
		return;
	}

	/* Perform trial I2C operations against each PDC target to see
	 * which are present.
	 */
	if (probe_pdc_chip(DEV_PDC_C0_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C0_RTK;
	}
	if (probe_pdc_chip(DEV_PDC_C1_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C1_RTK;
	}
	if (probe_pdc_chip(DEV_PDC_C2_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C2_RTK;
	}
	if (probe_pdc_chip(DEV_PDC_C0_TI)) {
		ctx.detected_cards |= RVP_TCSS_C0_TI;
	}
	if (probe_pdc_chip(DEV_PDC_C1_TI)) {
		ctx.detected_cards |= RVP_TCSS_C1_TI;
	}
	if (probe_pdc_chip(DEV_PDC_C2_TI)) {
		ctx.detected_cards |= RVP_TCSS_C2_TI;
	}

	LOG_INF("%s: TCSS detection result: 0x%02x", __func__,
		ctx.detected_cards);

	ctx.initialized = true;
}

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	discover_tcss_modules();

	/*
	 * Based on results, determine the PDC config. Not all combinations
	 * are allowed.
	 */
	switch (ctx.detected_cards) {
	case RVP_TCSS_NONE:
		/* No TCSS cards */
		LOG_INF("%s: PDC config: [---,---,---]", __func__);
		*dev = NULL;
		return 0;
	case RVP_TCSS_C0_RTK:
		/* Single RTK card on port 0 */
		LOG_INF("%s: PDC config: [RTK,---,---]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_RTK;
			return 0;
		}
		break;
	case RVP_TCSS_DUAL_RTK:
		/* Two RTK cards on port 0 and 1 */
		LOG_INF("%s: PDC config: [RTK,RTK,---]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_RTK;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_RTK;
			return 0;
		}
		break;
	case RVP_TCSS_THREE_RTK:
		/* Three RTK cards */
		LOG_INF("%s: PDC config: [RTK,RTK,RTK]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_RTK;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_RTK;
			return 0;
		} else if (port == 2) {
			*dev = DEV_PDC_C2_RTK;
			return 0;
		}
		break;
	case RVP_TCSS_C0_TI:
		/* Only Port 0 */
		LOG_INF("%s: PDC config: [TI,---,---]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_TI;
			return 0;
		}
		break;
	case RVP_TCSS_DUAL_TI:
		/* Port 0 and 1 */
		LOG_INF("%s: PDC config: [TI,TI,---]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_TI;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_TI;
			return 0;
		}
		break;
	case RVP_TCSS_THREE_TI:
		/* Port 0, 1 and 2 */
		LOG_INF("%s: PDC config: [TI,TI,TI]", __func__);
		if (port == 0) {
			*dev = DEV_PDC_C0_TI;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_TI;
			return 0;
		} else if (port == 2) {
			*dev = DEV_PDC_C2_TI;
			return 0;
		}
		break;

	default:
		LOG_ERR("%s: Unsupported PDC configuration (0x%02x)", __func__,
			ctx.detected_cards);
		break;
	}

	*dev = NULL;
	return -ENOENT;
}

#ifdef CONFIG_ZTEST
/* Test-only function to reset discovery state for i2c failure injection tests
 */
void reset_pdc_discovery_for_test(void)
{
	ctx.initialized = false;
	ctx.detected_cards = RVP_TCSS_NONE;
}
#endif
