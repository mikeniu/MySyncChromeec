/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Minimal PDC driver used to simulate an absent PDC chip in tests.
 *
 * pdc_get_hw_config() succeeds and returns an I2C spec pointing to the node's
 * address on i2c0.  Because no I2C emulator is registered for this compatible,
 * i2c_transfer_dt() returns an error and probe_pdc_chip() treats the chip as
 * not present.
 */

#define DT_DRV_COMPAT cros_fake_absent_pdc

#include "drivers/pdc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

struct fake_absent_pdc_config {
	struct i2c_dt_spec i2c;
};

static int fake_absent_get_hw_config(const struct device *dev,
				     struct pdc_hw_config_t *config)
{
	const struct fake_absent_pdc_config *cfg = dev->config;

	config->bus_type = PDC_BUS_TYPE_I2C;
	config->i2c = cfg->i2c;
	return 0;
}

static const struct pdc_driver_api fake_absent_pdc_api = {
	.get_hw_config = fake_absent_get_hw_config,
};

#define FAKE_ABSENT_PDC_DEFINE(n)                                         \
	static const struct fake_absent_pdc_config fake_absent_cfg_##n = { \
		.i2c = I2C_DT_SPEC_INST_GET(n),                           \
	};                                                                 \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &fake_absent_cfg_##n,   \
			      POST_KERNEL, 50, &fake_absent_pdc_api);

DT_INST_FOREACH_STATUS_OKAY(FAKE_ABSENT_PDC_DEFINE)
