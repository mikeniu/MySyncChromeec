/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for Hekla - dynamic PDC detection
 */

#include "drivers/pdc.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hekla_usbc, LOG_LEVEL_INF);

#define DEV_PDC_P0 DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0))
#define DEV_PDC_P1 DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1))

enum hekla_pdc_modules {
	HEKLA_PDC_NONE = 0,
	HEKLA_PDC_P0 = BIT(0),
	HEKLA_PDC_P1 = BIT(1),
};

static struct {
	bool initialized;
	enum hekla_pdc_modules detected_pdc;
} ctx;

static bool probe_pdc_chip(const struct device *dev)
{
	struct pdc_hw_config_t config;
	int rv;

	if (dev == NULL) {
		LOG_ERR("%s: Invalid pointer", __func__);
		return false;
	}

	rv = pdc_get_hw_config(dev, &config);
	if (rv) {
		LOG_ERR("%s: Cannot get bus info for PDC %s: %d", __func__,
			dev->name ? dev->name : "unnamed", rv);
		return false;
	}

	struct i2c_msg msgs[1];
	uint8_t dst;

	msgs[0].buf = &dst;
	msgs[0].len = 0U;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	/* If the transfer succeeds, a chip is at this address */
	return i2c_transfer_dt(&config.i2c, &msgs[0], 1) == 0;
}

static void discover_pdc_modules(void)
{
	if (ctx.initialized) {
		/* Only run once */
		return;
	}

	/* Probe each PDC to see which are present */
	if (probe_pdc_chip(DEV_PDC_P0)) {
		ctx.detected_pdc |= HEKLA_PDC_P0;
		LOG_INF("%s: PDC port 0 detected", __func__);
	} else {
		LOG_WRN("%s: PDC port 0 not detected, skipping", __func__);
	}

	if (probe_pdc_chip(DEV_PDC_P1)) {
		ctx.detected_pdc |= HEKLA_PDC_P1;
		LOG_INF("%s: PDC port 1 detected", __func__);
	} else {
		LOG_WRN("%s: PDC port 1 not detected, skipping", __func__);
	}

	LOG_INF("%s: PDC detection result: 0x%02x", __func__, ctx.detected_pdc);

	ctx.initialized = true;
}

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	if (dev == NULL) {
		LOG_ERR("%s: Bad pointer", __func__);
		return -EINVAL;
	}

	discover_pdc_modules();

	/*
	 * The PDC runtime port config requires continuous port ranges
	 * starting from port 0. If a PDC is not detected, remap the
	 * remaining PDC to port 0 so the system can still boot.
	 *
	 * Both PDCs detected: port 0 -> PDC_P0, port 1 -> PDC_P1 (count=2)
	 * Only PDC_P0:        port 0 -> PDC_P0, port 1 -> NULL    (count=1)
	 * Only PDC_P1:        port 0 -> PDC_P1, port 1 -> NULL    (count=1)
	 * Neither:            port 0 -> NULL                      (count=0)
	 */
	switch (port) {
	case 0:
		if (ctx.detected_pdc & HEKLA_PDC_P0) {
			*dev = DEV_PDC_P0;
			return 0;
		}
		if (ctx.detected_pdc & HEKLA_PDC_P1) {
			LOG_WRN("%s: PDC_P0 not detected, using PDC_P1 "
				"for port 0",
				__func__);
			*dev = DEV_PDC_P1;
			return 0;
		}
		LOG_ERR("%s: No PDC detected", __func__);
		*dev = NULL;
		return 0;

	case 1:
		/* Port 1 only valid when both PDCs are present */
		if ((ctx.detected_pdc & HEKLA_PDC_P0) &&
		    (ctx.detected_pdc & HEKLA_PDC_P1)) {
			*dev = DEV_PDC_P1;
			return 0;
		}
		LOG_INF("%s: Port 1 disabled (missing PDC)", __func__);
		*dev = NULL;
		return 0;

	default:
		LOG_ERR("%s: Invalid port %d", __func__, port);
		*dev = NULL;
		return -EINVAL;
	}
}
