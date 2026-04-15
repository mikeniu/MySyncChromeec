/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "console.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/ft90_flash_api_ex.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

const static struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
static int wp = 1;
/* TODO(b/434894113): Change based on the final solution of WP state. */
int write_protect_is_asserted_custom(void)
{
	return wp;
}

static int command_wp0(int argc, const char **argv)
{
	wp = 0;
	return EC_SUCCESS;
};
DECLARE_SAFE_CONSOLE_COMMAND(wp0, command_wp0, "", "");

static int command_wp1(int argc, const char **argv)
{
	wp = 1;
	return EC_SUCCESS;
};
DECLARE_SAFE_CONSOLE_COMMAND(wp1, command_wp1, "", "");

static int command_sr0(int argc, const char **argv)
{
	struct ft_xip_ex_ops_set_in op_in = { 0 };
	struct ft_xip_ex_ops_lock_in op_in_lock = {
		.enable = false,
	};

	flash_ex_op(flash_dev, FLASH_FT_XIP_EX_OP_LOCK, (uintptr_t)&op_in_lock, 0);

	op_in.masks[0] = 0xff;
	op_in.masks[1] = 0xff;
	flash_ex_op(flash_dev, FLASH_FT_XIP_EX_OP_SET_STATUS_REGS, (uintptr_t)&op_in, 0);

	return EC_SUCCESS;
};
DECLARE_SAFE_CONSOLE_COMMAND(sr0, command_sr0, "", "");
