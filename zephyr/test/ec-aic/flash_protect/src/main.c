/*
 * Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "write_protect.h"

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/ztest.h>

/*
 *_cros_flash drivers are required to use the zephyr,flash-controller
 * chosen node for communication with the actual internal flash
 * using the Zephyr flash API.
 *
 * These tests use the chosen node to validate that the actual
 * flash state instead of relying solely on the cros_flash_physical_*
 * APIs.
 */
#define ZEPHYR_FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

/* Assumed binman layout:
 * binman {
 *     wp_ro_ wp-ro {
 *         offset = <ro_section_offset>;
 *         ec-ro {
 *             ro_flash_block: ro-flash-block {
 *                 offset = <ro-flash-block-offset>;
 *             }
 *         }
 *     }
 *     ec_rw: ec-rw {
 *         offset = <rw_section_offset>;
 *         rw-fw {
 *             rw_flash_block: rw-flash-block {
 *                 offset = <rw-flash-block-offset>;
 *             }
 *         }
 *     }
 *
 */
#define RO_FLASH_BLOCK_NODE DT_NODELABEL(ro_flash_block)
#define WP_RO_NODE DT_NODELABEL(wp_ro)
#define RW_FLASH_BLOCK_NODE DT_NODELABEL(rw_flash_block)
#define EC_RW_NODE DT_NODELABEL(ec_rw)

#define FLASH_ACCESS_SIZE 128

struct aic_flash_protect_fixture {
	const struct device *flash_dev;
	off_t ro_flash_offset;
	off_t rw_flash_offset;
	size_t erase_size;
	uint8_t write_buf[FLASH_ACCESS_SIZE];
};

static void *setup(void)
{
	static struct aic_flash_protect_fixture fixture = {
		.flash_dev = DEVICE_DT_GET(ZEPHYR_FLASH_DEV),
		.ro_flash_offset = DT_PROP(RO_FLASH_BLOCK_NODE, offset) +
				   DT_PROP(WP_RO_NODE, offset),
		.rw_flash_offset = DT_PROP(RW_FLASH_BLOCK_NODE, offset) +
				   DT_PROP(EC_RW_NODE, offset),
		.erase_size = DT_PROP(RO_FLASH_BLOCK_NODE, size),
	};

	zassert_not_null(fixture.flash_dev);
	zassert_true(device_is_ready(fixture.flash_dev));

	/* Ensure flash is unprotected at start of test. */
	crec_flash_set_protect(
		EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_ALL_AT_BOOT, 0);

	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		fixture.write_buf[i] = i;
	}

	return &fixture;
}

ZTEST_SUITE(aic_flash_protect, NULL, setup, NULL, NULL, NULL);

static void verify_flash_writable(const struct device *dev, off_t offset,
				  size_t erase_size, uint8_t *write_buf)
{
	uint8_t read_buf[FLASH_ACCESS_SIZE];

	zassert_ok(flash_erase(dev, offset, erase_size));

	memset(read_buf, FLASH_ACCESS_SIZE, 0);
	zassert_ok(flash_read(dev, offset, read_buf, FLASH_ACCESS_SIZE));

	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		zassert_equal(read_buf[i], 0xFF);
	}

	zassert_ok(flash_write(dev, offset, write_buf, FLASH_ACCESS_SIZE));
	zassert_ok(flash_read(dev, offset, read_buf, FLASH_ACCESS_SIZE));

	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		zassert_equal(read_buf[i], write_buf[i]);
	}
}

static void verify_flash_locked(const struct device *dev, off_t offset,
				size_t erase_size)
{
	int ret;
	bool is_erased;
	uint8_t read_buf[FLASH_ACCESS_SIZE];
	uint8_t read_buf2[FLASH_ACCESS_SIZE];
	uint8_t write_buf[FLASH_ACCESS_SIZE];

	/* Reads are still permitted. Require that the flash block
	 * has non-erased data.
	 */
	zassert_ok(flash_read(dev, offset, read_buf, FLASH_ACCESS_SIZE));

	is_erased = true;
	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		if (read_buf[i] != 0xFF) {
			is_erased = false;
			break;
		}
	}

	zassert_false(
		is_erased,
		"flash offset already erased, cannot verify erase fails while locked");

	/* The flash_erase() call may succeed, but the data should not be
	 * be erased.
	 */
	ret = flash_erase(dev, offset, erase_size);

	zassert_ok(flash_read(dev, offset, read_buf, FLASH_ACCESS_SIZE));

	is_erased = true;
	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		if (read_buf[i] != 0xFF) {
			is_erased = false;
		}
	}
	zassert_false(is_erased, "Flash erase succeeded while flash is locked");

	/* Generate different data to write */
	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		write_buf[i] = ~read_buf[i];
	}

	ret = flash_write(dev, offset, write_buf, FLASH_ACCESS_SIZE);

	/* The flash_write() call may succeed, but the data should not
	 * change.
	 */
	zassert_ok(flash_read(dev, offset, read_buf2, FLASH_ACCESS_SIZE));

	for (int i = 0; i < FLASH_ACCESS_SIZE; i++) {
		zassert_equal(read_buf2[i], read_buf[i],
			      "Flash data changed after write while locked");
	}
}

ZTEST_F(aic_flash_protect, test__flash_protect_now)
{
	/* hardware write protect must be disabled */
	zassert_false(write_protect_is_asserted());

	/* Verify erase/program to RO and RW regions when WP is off. */
	TC_PRINT("HW WP is off\n");
	TC_PRINT("verify erase and program to unlocked RO: offset 0x%08x\n",
		 (uint32_t)fixture->ro_flash_offset);
	verify_flash_writable(fixture->flash_dev, fixture->ro_flash_offset,
			      fixture->erase_size, fixture->write_buf);

	TC_PRINT("verify erase and program to unlocked RW: offset 0x%08x\n",
		 (uint32_t)fixture->rw_flash_offset);
	verify_flash_writable(fixture->flash_dev, fixture->rw_flash_offset,
			      fixture->erase_size, fixture->write_buf);

	/* Protect the RO region. */
	zassert_ok(crec_flash_physical_protect_now(false));

	TC_PRINT("verify RO is locked: offset 0x%08x\n",
		 (uint32_t)fixture->ro_flash_offset);
	verify_flash_locked(fixture->flash_dev, fixture->ro_flash_offset,
			    fixture->erase_size);

	TC_PRINT("verify erase and program RW with RO locked: offset 0x%08x\n",
		 (uint32_t)fixture->rw_flash_offset);
	verify_flash_writable(fixture->flash_dev, fixture->rw_flash_offset,
			      fixture->erase_size, fixture->write_buf);
}
