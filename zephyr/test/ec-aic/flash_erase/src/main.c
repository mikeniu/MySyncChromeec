/*
 * Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/ztest.h>

#define ZEPHYR_FLASH_DEV DT_CHOSEN(zephyr_flash_controller)

#define TEST_FLASH_BLOCK_NODE DT_NODELABEL(test_flash_block)
#define EC_RW_NODE DT_NODELABEL(ec_rw)

#define TEST_ITERATIONS 10
#define CHUNK_SIZE 256

struct aic_flash_erase_fixture {
	const struct device *flash_dev;
	off_t flash_offset;
	size_t erase_size;
};

static void *setup(void)
{
	static struct aic_flash_erase_fixture fixture;

	fixture.flash_dev = DEVICE_DT_GET(ZEPHYR_FLASH_DEV);
	fixture.flash_offset = DT_PROP(TEST_FLASH_BLOCK_NODE, offset) +
			       DT_PROP(EC_RW_NODE, offset);
	fixture.erase_size = DT_PROP(TEST_FLASH_BLOCK_NODE, size);

	zassert_not_null(fixture.flash_dev, "Flash device is NULL");
	zassert_true(device_is_ready(fixture.flash_dev),
		     "Flash device not ready");

	return &fixture;
}

ZTEST_SUITE(aic_flash_erase, NULL, setup, NULL, NULL, NULL);

ZTEST_F(aic_flash_erase, test_multi_sector_erase_and_write)
{
	uint8_t write_buf[CHUNK_SIZE];
	uint8_t read_buf[CHUNK_SIZE];
	int ret;

	TC_PRINT("Testing multi-sector flash erase (%zu bytes at 0x%08x)\n",
		 fixture->erase_size, (uint32_t)fixture->flash_offset);

	for (int iter = 1; iter <= TEST_ITERATIONS; iter++) {
		TC_PRINT("Iteration %d/%d: Erasing flash sectors...\n", iter,
			 TEST_ITERATIONS);

		ret = flash_erase(fixture->flash_dev, fixture->flash_offset,
				  fixture->erase_size);
		zassert_equal(ret, 0,
			      "Iteration %d: flash_erase failed with %d", iter,
			      ret);

		/* Verify all erased bytes are 0xFF */
		for (size_t chunk_off = 0; chunk_off < fixture->erase_size;
		     chunk_off += CHUNK_SIZE) {
			ret = flash_read(fixture->flash_dev,
					 fixture->flash_offset + chunk_off,
					 read_buf, CHUNK_SIZE);
			zassert_equal(ret, 0,
				      "Iter %d: flash_read failed at 0x%zx",
				      iter, chunk_off);

			for (int i = 0; i < CHUNK_SIZE; i++) {
				zassert_equal(
					read_buf[i], 0xFF,
					"Iter %d: erase error at 0x%zx+%d",
					iter, chunk_off, i);
			}
		}

		TC_PRINT("Iteration %d/%d: Writing test pattern...\n", iter,
			 TEST_ITERATIONS);

		/* Prepare unique pattern for this iteration */
		for (int i = 0; i < CHUNK_SIZE; i++) {
			write_buf[i] = (uint8_t)(iter + i);
		}

		/* Write pattern across all sectors in CHUNK_SIZE units */
		for (size_t chunk_off = 0; chunk_off < fixture->erase_size;
		     chunk_off += CHUNK_SIZE) {
			ret = flash_write(fixture->flash_dev,
					  fixture->flash_offset + chunk_off,
					  write_buf, CHUNK_SIZE);
			zassert_equal(ret, 0,
				      "Iter %d: flash_write failed at 0x%zx",
				      iter, chunk_off);

			ret = flash_read(fixture->flash_dev,
					 fixture->flash_offset + chunk_off,
					 read_buf, CHUNK_SIZE);
			zassert_equal(ret, 0,
				      "Iter %d: flash_read failed at 0x%zx",
				      iter, chunk_off);

			for (int i = 0; i < CHUNK_SIZE; i++) {
				zassert_equal(read_buf[i], write_buf[i],
					      "Iter %d: mismatch at 0x%zx+%d",
					      iter, chunk_off, i);
			}
		}

		TC_PRINT("Iteration %d/%d: PASSED\n", iter, TEST_ITERATIONS);
	}
}
