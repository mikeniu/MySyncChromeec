/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT nuvoton_npcx_cros_kb_raw

#include <assert.h>
#include <drivers/cros_kb_raw.h>
#include <drivers/kscan.h>

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "task.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#define KSCAN_DEV DT_NODELABEL(kscan0)
const static struct device *kscan_dev;
struct kscan_matrix_config matrix_config;

/* Cros ec keyboard raw api functions */
static int cros_kb_raw_npcx_enable_interrupt(const struct device *dev,
					     int enable)
{
	return kscan_resume_matrix_detection(kscan_dev, enable);
}

static int cros_kb_raw_npcx_read_row(const struct device *dev)
{
	int val;

	kscan_read_matrix_row(kscan_dev, &val);

	return val;
}

static int cros_kb_raw_npcx_drive_column(const struct device *dev, int col)
{
	uint32_t col_out;
	int ret = 0;

	/* Add support for CONFIG_KEYBOARD_KSO_BASE shifting */
	col_out = col + CONFIG_KEYBOARD_KSO_BASE;

	/* Drive all lines to high. ie. Key detection is disabled. */
	if (col == KEYBOARD_COLUMN_NONE) {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			gpio_set_level(GPIO_KBD_KSO2, 0);
		}
		ret = kscan_drive_matrix_column(kscan_dev, KEYBOARD_COLUMN_DRIVE_NONE);
	}
	/* Drive all lines to low for detection any key press */
	else if (col == KEYBOARD_COLUMN_ALL) {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			gpio_set_level(GPIO_KBD_KSO2, 1);
		}
		ret = kscan_drive_matrix_column(kscan_dev, KEYBOARD_COLUMN_DRIVE_ALL);
	}
	/* Drive one line to low for determining which key's state changed. */
	else {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			if (col == 2)
				gpio_set_level(GPIO_KBD_KSO2, 1);
			else
				gpio_set_level(GPIO_KBD_KSO2, 0);
		}
		ret = kscan_drive_matrix_column(kscan_dev, col_out);
	}

	return ret;
}

static void cros_kb_raw_ksi_isr_cb(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Wake-up keyboard scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int cros_kb_raw_npcx_init(const struct device *dev)
{
	matrix_config.isr_callback = cros_kb_raw_ksi_isr_cb;
	kscan_config_matrix(kscan_dev, &matrix_config);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_npcx_driver_api = {
	.init = cros_kb_raw_npcx_init,
	.drive_colum = cros_kb_raw_npcx_drive_column,
	.read_rows = cros_kb_raw_npcx_read_row,
	.enable_interrupt = cros_kb_raw_npcx_enable_interrupt,
};

static int kb_raw_npcx_init(const struct device *dev)
{
	kscan_dev = DEVICE_DT_GET(KSCAN_DEV);
	if (!device_is_ready(kscan_dev)) {
		LOG_ERR("%s device not ready", kscan_dev->name);
		return -ENODEV;
	}

	return EC_SUCCESS;
}

/* Verify there's exactly 1 enabled cros,kb-raw-npcx node. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
BUILD_ASSERT(CONFIG_CROS_KB_RAW_NPCX_INIT_PRIORITY >
	     CONFIG_KSCAN_INIT_PRIORITY);
DEVICE_DT_INST_DEFINE(0, kb_raw_npcx_init, NULL, NULL, NULL,
		      POST_KERNEL, CONFIG_CROS_KB_RAW_NPCX_INIT_PRIORITY,
		      &cros_kb_raw_npcx_driver_api);
