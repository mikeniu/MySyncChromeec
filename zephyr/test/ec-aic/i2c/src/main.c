/*
 * Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DAGWOOD_I2C_DEV DT_NODELABEL(dagwood_eeprom)

LOG_MODULE_REGISTER(aic_i2c);

DEVICE_DT_DEFINE(DAGWOOD_I2C_DEV, NULL, NULL, NULL, NULL, POST_KERNEL, 90, 0);

struct aic_i2c_fixture {
	struct i2c_dt_spec dagwood_eeprom;
};

static void *aic_i2c_setup(void)
{
	static struct aic_i2c_fixture fixture = {
		.dagwood_eeprom = I2C_DT_SPEC_GET(DAGWOOD_I2C_DEV),
	};

	return &fixture;
}

ZTEST_SUITE(aic_i2c, NULL, aic_i2c_setup, NULL, NULL, NULL);

/* Must match platform/dagwood/firmware/include/dagwood/i2c_target.h */
#define I2C_GOOD_OFFSET 24
#define I2C_STRETCH_50MS_OFFSET 0x30
#define I2C_STRETCH_100MS_OFFSET 0x40
#define I2C_STRETCH_500MS_OFFSET 0x50

ZTEST_F(aic_i2c, test_i2c_read_write)
{
	uint8_t read_byte;
	uint8_t write_byte;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	uint32_t start_time;
	uint32_t elapsed_ms;

	LOG_INF("Dagwood eeprom %s:0x%02x", fixture->dagwood_eeprom.bus->name,
		fixture->dagwood_eeprom.addr);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Initial read took %u ms", elapsed_ms);

	write_byte = ~read_byte;
	start_time = k_uptime_get_32();
	zassert_ok(
		i2c_reg_write_byte_dt(i2c_spec, I2C_GOOD_OFFSET, write_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Write took %u ms", elapsed_ms);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Read back took %u ms", elapsed_ms);

	zassert_equal(read_byte, write_byte);

	LOG_INF("I2C Read modify write successful");
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_50ms)
{
	uint8_t read_byte;
	uint8_t write_byte = 0x5a;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	uint32_t start_time;
	uint32_t elapsed_ms;

	LOG_INF("Testing 50ms clock stretch at offset 0x%02x",
		I2C_STRETCH_50MS_OFFSET);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_50MS_OFFSET,
					 write_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Write to offset 0x%02x took %u ms", I2C_STRETCH_50MS_OFFSET,
		elapsed_ms);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_50MS_OFFSET,
					&read_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("50ms clock stretch read took %u ms", elapsed_ms);

	/* Allow a 10% under delay to account for clock difference between
	 * dagwood and the EC. Allow for 10% + 10ms over delay to account
	 * for any latency in the EC completing the I2C transaction.
	 */
	zassert_between_inclusive(
		elapsed_ms, 45, 60,
		"Read with 50ms stretch completed out of bounds: "
		"%u ms not between 45ms and 60ms",
		elapsed_ms);
	zassert_equal(read_byte, write_byte,
		      "Mismatch reading 50ms stretch offset");
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_100ms)
{
	uint8_t read_byte;
	uint8_t write_byte = 0xa5;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	uint32_t start_time;
	uint32_t elapsed_ms;

	LOG_INF("Testing 100ms clock stretch at offset 0x%02x",
		I2C_STRETCH_100MS_OFFSET);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_100MS_OFFSET,
					 write_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Write to offset 0x%02x took %u ms", I2C_STRETCH_100MS_OFFSET,
		elapsed_ms);

	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_100MS_OFFSET,
					&read_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("100ms clock stretch read took %u ms", elapsed_ms);

	/* Allow a 10% under delay to account for clock difference between
	 * dagwood and the EC. Allow for 10% + 10ms over delay to account
	 * for any latency in the EC completing the I2C transaction.
	 */
	zassert_between_inclusive(
		elapsed_ms, 90, 120,
		"Read with 100ms stretch completed out of bounds: "
		"%u ms not between 94ms and 115ms",
		elapsed_ms);
	zassert_equal(read_byte, write_byte,
		      "Mismatch reading 100ms stretch offset");
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_500ms_timeout)
{
	uint8_t read_byte;
	uint8_t write_byte = 0x3c;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	uint32_t start_time;
	uint32_t elapsed_ms;
	int ret;

	LOG_INF("Testing 500ms clock stretch timeout at offset 0x%02x",
		I2C_STRETCH_500MS_OFFSET);

	/* Write to memory succeeds normally without clock stretching */
	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_500MS_OFFSET,
					 write_byte));
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Write to offset 0x%02x took %u ms", I2C_STRETCH_500MS_OFFSET,
		elapsed_ms);

	/* Reading from offset 0x50 causes a 500ms clock stretch, exceeding
	 * transfer timeout */
	start_time = k_uptime_get_32();
	ret = i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_500MS_OFFSET,
				   &read_byte);
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("500ms clock stretch read returned %d (expected error), took %u ms",
		ret, elapsed_ms);
	zassert_not_equal(
		ret, 0,
		"Expected read to fail due to timeout on 500ms stretch, got %d",
		ret);

	/* For this test, the EC I2C controller should configure a timeout of
	 * at least 400ms.
	 */
	zassert_true(
		elapsed_ms >= 350,
		"Timeout transaction completed too quickly: %u ms < 350 ms",
		elapsed_ms);

	/* Make sure the target device clock stretch has expired. */
	k_msleep(500);

	/* Verify controller recovers after timeout and can perform normal
	 * transfers */
	start_time = k_uptime_get_32();
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte),
		   "Controller failed to recover after clock stretch timeout");
	elapsed_ms = k_uptime_get_32() - start_time;
	LOG_INF("Recovery read took %u ms", elapsed_ms);
}
