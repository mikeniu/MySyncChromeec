/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "openssl/mem.h"
#include "otp_key.h"
#include "trng.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtkfp_otp_key, LOG_LEVEL_ERR);

#define RTKFP_OTP_KEY_ADDR 0x40

static const struct device *const otp_dev = DEVICE_DT_GET(DT_NODELABEL(ocotp));

BUILD_ASSERT(OTP_KEY_SIZE_BYTES % 4 == 0);

void otp_key_init(void)
{
	if (!device_is_ready(otp_dev)) {
		LOG_ERR("OTP device not ready");
	}
}

void otp_key_exit(void)
{
}

enum ec_error_list otp_key_read(uint8_t *key_buffer)
{
	int ret;

	if (!device_is_ready(otp_dev)) {
		return EC_ERROR_HW_INTERNAL;
	}

	if (!key_buffer) {
		return EC_ERROR_INVAL;
	}

	ret = otp_read(otp_dev, RTKFP_OTP_KEY_ADDR, key_buffer,
		       OTP_KEY_SIZE_BYTES);
	if (ret) {
		LOG_ERR("Failed to read OTP: %d", ret);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

enum ec_error_list otp_key_provision(void)
{
	enum ec_error_list ec_status;
	uint8_t __aligned(4) otp_key_buffer[OTP_KEY_SIZE_BYTES] = { 0 };
	int ret;

	ec_status = otp_key_read(otp_key_buffer);
	if (ec_status != EC_SUCCESS) {
		goto exit;
	}

	if (!bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES)) {
		ec_status = EC_SUCCESS;
		goto exit;
	}

	trng_rand_bytes(otp_key_buffer, OTP_KEY_SIZE_BYTES);

	if (bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES)) {
		LOG_ERR("Failed to generate non-trivial random data");
		ec_status = EC_ERROR_UNKNOWN;
		goto exit;
	}

	ret = otp_program(otp_dev, RTKFP_OTP_KEY_ADDR, otp_key_buffer,
			  OTP_KEY_SIZE_BYTES);
	if (ret) {
		LOG_ERR("Failed to program OTP: %d", ret);
		ec_status = EC_ERROR_UNKNOWN;
	}

exit:
	OPENSSL_cleanse(otp_key_buffer, OTP_KEY_SIZE_BYTES);
	return ec_status;
}
