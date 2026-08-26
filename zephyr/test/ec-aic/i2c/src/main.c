/*
 * Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
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

#define I2C_GOOD_OFFSET 24
#define I2C_STRETCH_50MS_OFFSET 0x30
#define I2C_STRETCH_100MS_OFFSET 0x40
#define I2C_STRETCH_500MS_OFFSET 0x50

ZTEST_F(aic_i2c, test_i2c_read_write)
{
	uint8_t read_byte;
	uint8_t write_byte;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;

	LOG_INF("Dagwood eeprom %s:0x%02x", fixture->dagwood_eeprom.bus->name,
		fixture->dagwood_eeprom.addr);

	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));

	write_byte = ~read_byte;
	zassert_ok(
		i2c_reg_write_byte_dt(i2c_spec, I2C_GOOD_OFFSET, write_byte));

	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));

	zassert_equal(read_byte, write_byte);

	LOG_INF("I2C Read modify write successful");
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_50ms)
{
	uint8_t read_byte;
	uint8_t write_byte = 0x5a;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;

	LOG_INF("Testing 50ms clock stretch at offset 0x%02x",
		I2C_STRETCH_50MS_OFFSET);

	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_50MS_OFFSET,
					 write_byte));
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_50MS_OFFSET,
					&read_byte));
	zassert_equal(read_byte, write_byte,
		      "Mismatch reading 50ms stretch offset");
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_100ms)
{
	uint8_t read_byte;
	uint8_t write_byte = 0xa5;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	int ret;

	LOG_INF("Testing 100ms clock stretch at offset 0x%02x",
		I2C_STRETCH_100MS_OFFSET);

	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_100MS_OFFSET,
					 write_byte));
	ret = i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_100MS_OFFSET,
				   &read_byte);
	LOG_INF("100ms clock stretch read returned %d", ret);
	if (ret == 0) {
		zassert_equal(read_byte, write_byte,
			      "Mismatch reading 100ms stretch offset");
	}
}

ZTEST_F(aic_i2c, test_i2c_clock_stretch_500ms_timeout)
{
	uint8_t read_byte;
	uint8_t write_byte = 0x3c;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;
	int ret;

	LOG_INF("Testing 500ms clock stretch timeout at offset 0x%02x",
		I2C_STRETCH_500MS_OFFSET);

	/* Write to memory succeeds normally without clock stretching */
	zassert_ok(i2c_reg_write_byte_dt(i2c_spec, I2C_STRETCH_500MS_OFFSET,
					 write_byte));

	/* Reading from offset 0x50 causes a 500ms clock stretch, exceeding transfer timeout */
	ret = i2c_reg_read_byte_dt(i2c_spec, I2C_STRETCH_500MS_OFFSET,
				   &read_byte);
	LOG_INF("500ms clock stretch read returned %d (expected error)", ret);
	zassert_not_equal(ret, 0,
			  "Expected read to fail due to timeout on 500ms stretch, got %d",
			  ret);

	/* Verify controller recovers after timeout and can perform normal transfers */
	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte),
		   "Controller failed to recover after clock stretch timeout");
}
