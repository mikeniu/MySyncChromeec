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
	if (offset < 0 || offset + len > FAKE_FLASH_SIZE)
		return -EINVAL;

	memcpy(data, fake_storage + offset, len);

	return 0;
}

static int fake_flash_write(const struct device *dev, off_t offset,
			    const void *data, size_t len)
{
	const uint8_t *src = data;
	size_t i;

	if (offset < 0 || offset + len > FAKE_FLASH_SIZE)
		return -EINVAL;

	/* NOR semantics: programming can only clear bits. */
	for (i = 0; i < len; i++)
		fake_storage[offset + i] &= src[i];

	return 0;
}

static int fake_flash_erase(const struct device *dev, off_t offset,
			    size_t size)
{
	if (offset < 0 || offset + size > FAKE_FLASH_SIZE)
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

static void fake_set_sr(uint8_t sr1, uint8_t sr2)
{
	fake_sr1 = sr1;
	fake_sr2 = sr2;
}

ZTEST_USER(cros_flash_rtkfp, test_baseline_no_protection)
{
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      0);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 0);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, WP_BANK),
		      0);
}

ZTEST_USER(cros_flash_rtkfp, test_init_clears_srp_preserves_other_bits)
{
	uint8_t sr1, sr2;

	/* SRP bits are HW-WP related and must be cleared at init. */
	fake_set_sr(SPI_FLASH_SR1_SRP0 | SPI_FLASH_SR1_WEL,
		    SPI_FLASH_SR2_SRP1 | SPI_FLASH_SR2_QE);

	zassert_equal(cros_flash_init(cros_flash_dev), EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_status(cros_flash_dev, &sr1,
						     &sr2),
		      EC_SUCCESS);
	zassert_false(sr1 & SPI_FLASH_SR1_SRP0);
	zassert_false(sr2 & SPI_FLASH_SR2_SRP1);
	/* Non-protection bits must survive the status register rewrite. */
	zassert_true(sr1 & SPI_FLASH_SR1_WEL);
	zassert_true(sr2 & SPI_FLASH_SR2_QE);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      0);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_ro)
{
	uint8_t sr1, sr2;

	/* A non-protection bit (QE) must be preserved by range changes. */
	fake_sr2 |= SPI_FLASH_SR2_QE;

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_RO_NOW);

	zassert_equal(cros_flash_physical_get_status(cros_flash_dev, &sr1,
						     &sr2),
		      EC_SUCCESS);
	/* Range [0, 0x40000) encoded as {SEC=0, TB=1, BP2=0, BP1=1, BP0=1}. */
	zassert_false(sr1 & SPI_FLASH_SR1_SEC);
	zassert_true(sr1 & SPI_FLASH_SR1_TB);
	zassert_false(sr1 & SPI_FLASH_SR1_BP2);
	zassert_true(sr1 & SPI_FLASH_SR1_BP1);
	zassert_true(sr1 & SPI_FLASH_SR1_BP0);
	zassert_false(sr2 & SPI_FLASH_SR2_CMP);
	zassert_false(sr1 & SPI_FLASH_SR1_SRP0);
	zassert_false(sr2 & SPI_FLASH_SR2_SRP1);
	zassert_true(sr2 & SPI_FLASH_SR2_QE);

	/* Protection covers RO, not RW. */
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 1);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, WP_BANK),
		      0);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_all)
{
	uint8_t sr1, sr2;

	fake_sr2 |= SPI_FLASH_SR2_QE;

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_ALL_AT_BOOT),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_AT_BOOT |
			      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_ALL_NOW |
			      EC_FLASH_PROTECT_RO_NOW);

	zassert_equal(cros_flash_physical_get_status(cros_flash_dev, &sr1,
						     &sr2),
		      EC_SUCCESS);
	/* Range [0, 0x80000) encoded as {SEC=0, TB=1, BP2=1, BP1=0, BP0=0}. */
	zassert_false(sr1 & SPI_FLASH_SR1_SEC);
	zassert_true(sr1 & SPI_FLASH_SR1_TB);
	zassert_true(sr1 & SPI_FLASH_SR1_BP2);
	zassert_false(sr1 & SPI_FLASH_SR1_BP1);
	zassert_false(sr1 & SPI_FLASH_SR1_BP0);
	zassert_false(sr2 & SPI_FLASH_SR2_CMP);
	zassert_true(sr2 & SPI_FLASH_SR2_QE);

	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 1);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, WP_BANK),
		      1);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_zero_disables)
{
	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_ALL_AT_BOOT),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_protect_at_boot(cros_flash_dev, 0),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      0);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 0);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_idempotent_wp_asserted)
{
	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      EC_SUCCESS);

	/* Re-requesting the same range must succeed even with WP asserted. */
	gpio_wp_l_set(0);

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_RO_NOW);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_shrink_wp_asserted_denied)
{
	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_ALL_AT_BOOT),
		      EC_SUCCESS);

	gpio_wp_l_set(0);

	zassert_equal(cros_flash_physical_protect_at_boot(cros_flash_dev, 0),
		      EC_ERROR_ACCESS_DENIED);

	/* The range must be unchanged. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_AT_BOOT |
			      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_ALL_NOW |
			      EC_FLASH_PROTECT_RO_NOW);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_at_boot_extend_wp_asserted_allowed)
{
	gpio_wp_l_set(0);

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_RO_NOW);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_now_ro)
{
	uint32_t value = 42;
	uint32_t read_value = 0;

	zassert_equal(cros_flash_physical_protect_now(cros_flash_dev, false),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW);
	zassert_false(cros_flash_physical_get_protect_flags(cros_flash_dev) &
		      EC_FLASH_PROTECT_ALL_NOW);

	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 1);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, WP_BANK),
		      0);

	/* Writes into the protected region are rejected... */
	zassert_equal(cros_flash_physical_write(cros_flash_dev, 0x1000,
						sizeof(value),
						(const char *)&value),
		      EC_ERROR_ACCESS_DENIED);

	/* ...writes outside of it are allowed. */
	zassert_equal(cros_flash_physical_write(cros_flash_dev, 0x50000,
						sizeof(value),
						(const char *)&value),
		      EC_SUCCESS);
	crec_flash_physical_read(0x50000, sizeof(read_value),
				 (char *)&read_value);
	zassert_equal(value, read_value);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_now_all)
{
	zassert_equal(cros_flash_physical_protect_now(cros_flash_dev, true),
		      EC_SUCCESS);

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_AT_BOOT |
			      EC_FLASH_PROTECT_RO_AT_BOOT |
			      EC_FLASH_PROTECT_ALL_NOW |
			      EC_FLASH_PROTECT_RO_NOW);

	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, 0), 1);
	zassert_equal(cros_flash_physical_get_protect(cros_flash_dev, WP_BANK),
		      1);

	/* Everything is write protected now. */
	zassert_equal(cros_flash_physical_write(cros_flash_dev, 0x70000, 4,
						"\xff\xff\xff\xff"),
		      EC_ERROR_ACCESS_DENIED);
	zassert_equal(cros_flash_physical_erase(cros_flash_dev, 0x70000,
						CONFIG_FLASH_ERASE_SIZE),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_flags_srp_inconsistent)
{
	fake_sr1 |= SPI_FLASH_SR1_SRP0;
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ERROR_INCONSISTENT);

	fake_sr1 &= ~SPI_FLASH_SR1_SRP0;
	fake_sr2 |= SPI_FLASH_SR2_SRP1;
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ERROR_INCONSISTENT);
}

ZTEST_USER(cros_flash_rtkfp, test_protect_flags_undecodable_unknown)
{
	/*
	 * CMP=1 with these BP bits has no entry in the GD25Q16 protection
	 * table, so the status registers cannot be decoded.
	 */
	fake_sr2 |= SPI_FLASH_SR2_CMP;

	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ERROR_UNKNOWN);
}

ZTEST_USER(cros_flash_rtkfp, test_status_register_write_failure)
{
	fake_ex_op_fail = true;

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      -EIO);
	zassert_equal(cros_flash_physical_protect_now(cros_flash_dev, false),
		      -EIO);

	/* No protection state change happened. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      0);
}

ZTEST_USER(cros_flash_rtkfp, test_write_erase_roundtrip)
{
	uint32_t value = 42;
	uint32_t read_value = 0;

	zassert_equal(cros_flash_physical_write(cros_flash_dev, 0x20000,
						sizeof(value),
						(const char *)&value),
		      EC_SUCCESS);
	crec_flash_physical_read(0x20000, sizeof(read_value),
				 (char *)&read_value);
	zassert_equal(value, read_value);

	zassert_equal(cros_flash_physical_erase(cros_flash_dev, 0x20000,
						CONFIG_FLASH_ERASE_SIZE),
		      EC_SUCCESS);
	crec_flash_physical_read(0x20000, sizeof(read_value),
				 (char *)&read_value);
	zassert_equal(read_value, 0xffffffff);
}

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
