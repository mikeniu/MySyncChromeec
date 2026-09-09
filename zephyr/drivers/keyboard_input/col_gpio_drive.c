/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ghost_filter.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(col_gpio_drive, CONFIG_INPUT_LOG_LEVEL);

#define DT_DRV_COMPAT cros_ec_col_gpio

struct col_gpio_config {
	const struct device *kbd_dev;
	struct gpio_dt_spec gpio;
	int col;
	uint32_t settle_time_us;
};

struct col_gpio_data {
	bool state;
};

static void drive_one_col_gpio(const struct device *col_dev,
			       const struct device *kbd_dev, int col)
{
	const struct col_gpio_config *cfg = col_dev->config;
	struct col_gpio_data *data = col_dev->data;
	bool state;

	if (kbd_dev != cfg->kbd_dev) {
		return;
	}

	if (col == INPUT_KBD_MATRIX_COLUMN_DRIVE_ALL || col == cfg->col) {
		gpio_pin_set_dt(&cfg->gpio, 1);
		state = true;
	} else {
		gpio_pin_set_dt(&cfg->gpio, 0);
		state = false;
	}

	if (state != data->state) {
		data->state = state;
		k_busy_wait(cfg->settle_time_us);
	}
}

#define DRIVE_ONE_INSTANCE(inst) \
	drive_one_col_gpio(DEVICE_DT_INST_GET(inst), dev, col);
void input_kbd_matrix_drive_column_hook(const struct device *dev, int col)
{
	DT_INST_FOREACH_STATUS_OKAY(DRIVE_ONE_INSTANCE);

	if (IS_ENABLED(CONFIG_CROS_EC_GHOST_FILTER)) {
		input_kbd_matrix_ghost_filter_hook(dev, col);
	}
}
#undef DRIVE_ONE_INSTANCE

static int col_gpio_init(const struct device *dev)
{
	const struct col_gpio_config *cfg = dev->config;
	struct col_gpio_data *data = dev->data;
	int ret;

	if (!gpio_is_ready_dt(&cfg->gpio)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}

	data->state = true;

	return 0;
}

#if CONFIG_DT_HAS_ITE_IT8XXX2_KBD_ENABLED
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_col_gpio)
#define KBD_NODE DT_PARENT(DT_DRV_INST(0))

/*
 * On ITE chips, the SSPI pins (SMOSI/SMISO) for spi0 are shared with the
 * KSO16/KSO17 keyboard matrix pins. If spi0 is enabled in the devicetree,
 * we must ensure that the keyboard controller is configured to ignore
 * KSO16 and KSO17 to prevent pin conflicts.
 */
#define KSO16_KSO17_MASK (BIT(16) | BIT(17))

BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(spi0), okay) ||
		     ((DT_PROP(KBD_NODE, kso_ignore_mask) & KSO16_KSO17_MASK) ==
		      KSO16_KSO17_MASK),
	     "spi0 is enabled, so KSO16 and KSO17 must be set in the keybaord "
	     "controller's kso-ignore-mask to prevent pin conflicts.");

#undef KSO16_KSO17_MASK

/*
 * The bits set in the parent keyboard controller's kso-ignore-mask property
 * must correspond exactly to the col-num properties of the cros-ec,col-gpio
 * child nodes.
 */
#define COL_GPIO_COL_NUM_BIT_OR(inst) | BIT(DT_INST_PROP(inst, col_num))
#define COL_GPIO_COL_NUM_MASK \
	(0 DT_INST_FOREACH_STATUS_OKAY(COL_GPIO_COL_NUM_BIT_OR))

BUILD_ASSERT(COL_GPIO_COL_NUM_MASK == DT_PROP(KBD_NODE, kso_ignore_mask),
	     "kso-ignore-mask must match the set of col-gpio nodes. Every "
	     "ignored column must have a corresponding col-gpio node, and "
	     "every col-gpio node must have its column ignored.");

#undef COL_GPIO_COL_NUM_MASK
#undef COL_GPIO_COL_NUM_BIT_OR
#undef KBD_NODE
#endif /* DT_HAS_COMPAT_STATUS_OKAY(cros_ec_col_gpio) */
#endif

#define COL_GPIO_DEVICE_INIT(inst)                                            \
	static const struct col_gpio_config col_gpio_config_##inst = {        \
		.kbd_dev = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))),       \
		.gpio = GPIO_DT_SPEC_INST_GET(inst, col_gpios),               \
		.col = DT_INST_PROP(inst, col_num),                           \
		.settle_time_us = DT_INST_PROP(inst, settle_time_us),         \
	};                                                                    \
                                                                              \
	static struct col_gpio_data col_gpio_data_##inst;                     \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, col_gpio_init, NULL,                      \
			      &col_gpio_data_##inst, &col_gpio_config_##inst, \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(COL_GPIO_DEVICE_INIT)
