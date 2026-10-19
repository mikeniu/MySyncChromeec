/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Tests for the REALTEK RTS5817 cros-flash driver (cros_flash_rtkfp.c).
 *
 * The driver is exercised through the cros_flash API against a fake
 * zephyr,flash-controller device that emulates the RTS5817 status
 * registers and the software protection latch via flash_ex_op().
 */

#include "common.h"
#include "drivers/cros_flash.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio_signal.h"
#include "spi_flash_reg.h"
#include "test/drivers/test_state.h"

#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/rts5817_flash_api_extensions.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define WP_L_GPIO_PATH NAMED_GPIOS_GPIO_NODE(wp_l)

static int gpio_wp_l_set(int value)
{
	const struct device *wp_l_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(WP_L_GPIO_PATH, gpios));

	return gpio_emul_input_set(wp_l_gpio_dev,
				   DT_GPIO_PIN(WP_L_GPIO_PATH, gpios), value);
}

#define cros_flash_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_flash_controller))

/* First bank outside of the RO/WP region [0, CONFIG_WP_STORAGE_SIZE). */
#define WP_BANK (CONFIG_WP_STORAGE_SIZE / CONFIG_FLASH_BANK_SIZE)

#define FAKE_FLASH_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_flash))
#define FAKE_PAGE_SIZE CONFIG_FLASH_ERASE_SIZE

/*
 * Fake flash controller: RAM storage plus RTS5817 status registers
 * accessed through flash_ex_op().
 */
static uint8_t fake_storage[FAKE_FLASH_SIZE];
static uint8_t fake_sr1;
static uint8_t fake_sr2;
static bool fake_soft_protected;
static bool fake_ex_op_fail;

static void fake_flash_reset(void)
{
	memset(fake_storage, 0xff, FAKE_FLASH_SIZE);
	fake_sr1 = 0;
	fake_sr2 = 0;
	fake_soft_protected = false;
	fake_ex_op_fail = false;
}

static int fake_flash_read(const struct device *dev, off_t offset,
			   void *data, size_t len)
{
	if (offset < 0 || len > FAKE_FLASH_SIZE ||
	    offset > FAKE_FLASH_SIZE - len)
		return -EINVAL;

	memcpy(data, fake_storage + offset, len);

	return 0;
}

static int fake_flash_write(const struct device *dev, off_t offset,
			    const void *data, size_t len)
{
	const uint8_t *src = data;
	size_t i;

	if (offset < 0 || len > FAKE_FLASH_SIZE ||
	    offset > FAKE_FLASH_SIZE - len)
		return -EINVAL;

	/* NOR semantics: programming can only clear bits. */
	for (i = 0; i < len; i++)
		fake_storage[offset + i] &= src[i];

	return 0;
}

static int fake_flash_erase(const struct device *dev, off_t offset,
			    size_t size)
{
	if (offset < 0 || size > FAKE_FLASH_SIZE ||
	    offset > FAKE_FLASH_SIZE - size)
		return -EINVAL;
	if (offset % FAKE_PAGE_SIZE || size % FAKE_PAGE_SIZE)
		return -EINVAL;

	memset(fake_storage + offset, 0xff, size);

	return 0;
}

static const struct flash_parameters fake_flash_parameters = {
	.write_block_size = 1,
	.erase_value = 0xff,
};

static const struct flash_parameters *fake_flash_get_parameters(
	const struct device *dev)
{
	return &fake_flash_parameters;
}

static const struct flash_pages_layout fake_flash_layout = {
	.pages_count = FAKE_FLASH_SIZE / FAKE_PAGE_SIZE,
	.pages_size = FAKE_PAGE_SIZE,
};

static int fake_flash_page_layout(const struct device *dev,
				  const struct flash_pages_layout **layout,
				  size_t *size)
{
	*layout = &fake_flash_layout;
	*size = 1;

	return 0;
}

static int fake_flash_ex_op(const struct device *dev, uint16_t code,
			    const uintptr_t in, void *out)
{
	if (fake_ex_op_fail && code != FLASH_RTS5817_EX_OP_RD_SR)
		return -EIO;

	switch (code) {
	case FLASH_RTS5817_EX_OP_RD_SR: {
		uint8_t *sr = out;

		sr[0] = fake_sr1;
		sr[1] = fake_sr2;
		return 0;
	}
	case FLASH_RTS5817_EX_OP_WR_SR: {
		const uint8_t *sr = (const uint8_t *)in;

		fake_sr1 = sr[0];
		fake_sr2 = sr[1];
		return 0;
	}
	case FLASH_RTS5817_EX_OP_SOFT_PROTECT: {
		const uint8_t *val = (const uint8_t *)in;

		fake_soft_protected = (*val != 0);
		return 0;
	}
	case FLASH_EX_OP_RESET:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct flash_driver_api fake_flash_api = {
	.read = fake_flash_read,
	.write = fake_flash_write,
	.erase = fake_flash_erase,
	.get_parameters = fake_flash_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = fake_flash_page_layout,
#endif
#if defined(CONFIG_FLASH_EX_OP_ENABLED)
	.ex_op = fake_flash_ex_op,
#endif
};

static int fake_flash_init(const struct device *dev)
{
	fake_flash_reset();

	return 0;
}

DEVICE_DT_DEFINE(DT_CHOSEN(zephyr_flash_controller), fake_flash_init, NULL,
		 NULL, NULL, POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY,
		 &fake_flash_api);

static void cros_flash_rtkfp_before(void *fixture)
{
	/*
	 * Establish a deterministic baseline:
	 *  - HW WP deasserted,
	 *  - no protection range,
	 *  - software protection latch enabled (sw_protected),
	 *  - fake flash fully erased with zeroed status registers.
	 */
	gpio_wp_l_set(1);
	fake_flash_reset();

	zassert_equal(cros_flash_physical_protect_now(cros_flash_dev, false),
		      EC_SUCCESS);
	zassert_equal(cros_flash_physical_protect_at_boot(cros_flash_dev, 0),
		      EC_SUCCESS);
	zassert_equal(cros_flash_init(cros_flash_dev), EC_SUCCESS);
}

ZTEST_SUITE(cros_flash_rtkfp, drivers_predicate_post_main, NULL,
	    cros_flash_rtkfp_before, NULL, NULL);
