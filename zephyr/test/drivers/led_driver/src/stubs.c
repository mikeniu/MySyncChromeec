// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "drivers/led.h"
#include "ec_commands.h"
#include "led_common.h"
#include "console.h"
#include "driver/led/max695x.h"

/* MAX695x test configuration */
#ifndef PORT80_I2C_ADDR
#define PORT80_I2C_ADDR MAX695X_I2C_ADDR1_FLAGS
#endif

static int mock_alt_policy = 1;

void set_board_led_alt_policy(int label)
{
	mock_alt_policy = label;
}

__override int board_led_alt_policy(void)
{
	return mock_alt_policy;
}

/*
 * Console stubs for MAX695x testing
 */
#ifdef CONFIG_CMD_SEVEN_SEG_DISPLAY
/*
 * Stub for console output functions called by MAX695x driver
 */
void cprints(enum console_channel channel, const char *format, ...)
{
	/* Stub implementation - no output needed for tests */
}
#endif

/*
 * I2C stubs for MAX695x driver - only compile when MAX695x tests are
 * enabled
 */
#ifdef CONFIG_LINK_TEST_SUITE_LED_DRIVER_MAX695X
#include "i2c.h"

/*
 * Weak stub I2C write function for MAX695x testing - can be overridden
 */
__weak int i2c_write8(const int port, const uint16_t slave_addr_flags,
	       int offset, int data)
{
	/*
	 * Stub implementation - return success
	 */
	return EC_SUCCESS;
}

/*
 * Weak stub I2C burst write function for MAX695x testing - can be
 * overridden
 */
__weak int i2c_write_block(const int port, const uint16_t slave_addr_flags,
		    int offset, const uint8_t *data, int len)
{
	/*
	 * Stub implementation - return success
	 */
	return EC_SUCCESS;
}
#endif /* CONFIG_LINK_TEST_SUITE_LED_DRIVER_MAX695X */
