/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "otp_key.h"
#include "system.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_CPU_CORTEX_M) || defined(__arm__)
#include <cmsis_core.h>

static inline bool irqs_are_enabled(void)
{
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	return __get_PRIMASK() == 0;
#else
	return (__get_PRIMASK() == 0) && (__get_BASEPRI() == 0);
#endif
}
#else
static inline bool irqs_are_enabled(void)
{
	return true;
}
#endif

LOG_MODULE_REGISTER(otp_key, LOG_LEVEL_INF);

ZTEST_SUITE(otp_key, NULL, NULL, NULL, NULL, NULL);

void log_key_buffer(uint8_t *key_buff)
{
	LOG_INF("key buffer: 0x");
	for (uint8_t i = 0; i < OTP_KEY_SIZE_BYTES; i++)
		LOG_INF("%02X", key_buff[i]);
	LOG_INF("\n");
}

ZTEST(otp_key, test_otp_key_provision)
{
	zassert_true(irqs_are_enabled(), "IRQs should be enabled before test");

	otp_key_init();

	uint32_t status = otp_key_provision();
	zassert_equal(status, EC_SUCCESS);

	uint8_t otp_key_buffer[OTP_KEY_SIZE_BYTES] = { 0 };
	status = otp_key_read(otp_key_buffer);
	zassert_equal(status, EC_SUCCESS);

	zassert_false(bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES));

	log_key_buffer(otp_key_buffer);

	otp_key_exit();

	zassert_true(irqs_are_enabled(),
		     "IRQs were left locked after otp_key_provision");
}
