/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file stubs.c
 * @brief Stub implementations for MFD IT8801 functions
 *
 * This file provides stub implementations of MFD functions that are not
 * compiled when CONFIG_MFD_ITE_IT8801=n. These stubs allow testing the
 * keyboard driver in isolation while maintaining control over the MFD
 * layer's behavior for test purposes.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/mfd_ite_it8801.h>

/* Test control variables */
static struct it8801_mfd_callback *registered_callback;
static int configure_pins_retval;
static int configure_pins_call_count;

/**
 * @brief Stub for mfd_it8801_configure_pins
 *
 * Simulates configuring IT8801 pins for alternate functions (e.g., KSO pins).
 * Returns the value set by test_set_configure_pins_retval().
 *
 * @param i2c_dev I2C device spec (unused in stub)
 * @param dev GPIO control device (unused in stub)
 * @param pin Pin number (unused in stub)
 * @param func Alternate function selection (unused in stub)
 * @return Configured return value (0 for success, negative for error)
 */
int mfd_it8801_configure_pins(const struct i2c_dt_spec *i2c_dev,
			      const struct device *dev, uint8_t pin,
			      uint8_t func)
{
	configure_pins_call_count++;
	return configure_pins_retval;
}

/**
 * @brief Stub for mfd_it8801_register_interrupt_callback
 *
 * Registers a callback function that will be invoked when the IT8801
 * asserts an interrupt. The test suite can retrieve this callback
 * via test_get_registered_callback() to simulate interrupts.
 *
 * @param mfd MFD parent device (unused in stub)
 * @param callback Callback structure containing the handler function
 */
void mfd_it8801_register_interrupt_callback(
	const struct device *mfd, struct it8801_mfd_callback *callback)
{
	registered_callback = callback;
}

/**
 * @brief Get the registered interrupt callback
 *
 * Test helper function to retrieve the callback registered by the driver.
 * Tests use this to simulate interrupt conditions.
 *
 * @return Pointer to the registered callback, or NULL if none registered
 */
struct it8801_mfd_callback *test_get_registered_callback(void)
{
	return registered_callback;
}

/**
 * @brief Set the return value for mfd_it8801_configure_pins
 *
 * Controls whether pin configuration succeeds or fails in tests.
 *
 * @param retval Return value (0 for success, negative for error)
 */
void test_set_configure_pins_retval(int retval)
{
	configure_pins_retval = retval;
}

/**
 * @brief Get the number of times configure_pins was called
 *
 * Used to verify that the driver correctly iterates through all
 * pin configurations in the device tree.
 *
 * @return Number of times mfd_it8801_configure_pins was called
 */
int test_get_configure_pins_call_count(void)
{
	return configure_pins_call_count;
}

/**
 * @brief Reset all stub state
 *
 * Clears all test control variables to their default state.
 * Should be called in test setup functions.
 */
void test_reset_stubs(void)
{
	registered_callback = NULL;
	configure_pins_retval = 0;
	configure_pins_call_count = 0;
}
