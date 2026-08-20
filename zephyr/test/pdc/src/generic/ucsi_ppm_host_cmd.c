/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "ec_commands.h"
#include "host_command.h"
#include "ppm_common.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <usbc/ppm.h>

LOG_MODULE_REGISTER(ucsi_ppm_host_cmd_test, LOG_LEVEL_INF);

static enum ec_status call_get_host_cmd(uint16_t offset, uint8_t size,
					void *resp_buf, uint16_t resp_max,
					int *out_size)
{
	struct ec_params_ucsi_ppm_get params = {
		.offset = offset,
		.size = size,
	};
	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_UCSI_PPM_GET,
		.version = 0,
		.params = &params,
		.params_size = sizeof(params),
		.response = resp_buf,
		.response_max = resp_max,
		.response_size = 0,
	};

	enum ec_status rv = host_command_process(&args);

	if (rv == EC_RES_SUCCESS && out_size != NULL) {
		*out_size = args.response_size;
	}
	return rv;
}

#define PPM_SET_SIZE (sizeof(struct ec_params_ucsi_ppm_set) + MESSAGE_OUT_SIZE)

static enum ec_status call_set_host_cmd(uint16_t offset, const void *data,
					size_t data_size)
{
	zassert_true(data_size <= MESSAGE_OUT_SIZE, "data_size too large");

	/* Allocate a memory to hold the ec_params_ucsi_ppm_set plus
	 * a buffer up to the max UCSI data structure size.
	 */
	union {
		uint8_t raw[PPM_SET_SIZE];
		struct ec_params_ucsi_ppm_set ppm_set;
	} params;

	params.ppm_set.offset = offset;
	if (data_size > 0 && data != NULL) {
		memcpy(params.ppm_set.data, data, data_size);
	}

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_UCSI_PPM_SET,
		.version = 0,
		.params = &params,
		.params_size =
			sizeof(struct ec_params_ucsi_ppm_set) + data_size,
		.response = NULL,
		.response_max = 0,
		.response_size = 0,
	};

	enum ec_status rv = host_command_process(&args);

	return rv;
}

ZTEST(ucsi_ppm, test_host_cmd_various_params)
{
	struct {
		uint16_t offset;
		uint8_t size;
		bool expect_success;
	} const cases[] = {
		/* Valid cases within MESSAGE_OUT */
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET,
			.size = 1,
			.expect_success = true,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + 10,
			.size = 5,
			.expect_success = true,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + 100,
			.size = 100,
			.expect_success = true,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + 254,
			.size = 1,
			.expect_success = true,
		},
		/* Invalid cases (out of bounds of MESSAGE_OUT) */
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET - 1,
			.size = 1,
			.expect_success = false,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + MESSAGE_OUT_SIZE,
			.size = 1,
			.expect_success = false,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + 255,
			.size = 1,
			.expect_success = false,
		},
		{
			.offset = UCSI_MESSAGE_OUT_OFFSET + 250,
			.size = 10,
			.expect_success = false,
		},
		/* Invalid offsets (not MESSAGE_OUT or CONTROL) */
		{
			.offset = UCSI_VERSION_OFFSET,
			.size = 1,
			.expect_success = false,
		},
		{
			.offset = UCSI_CCI_OFFSET,
			.size = 1,
			.expect_success = false,
		},
		{
			.offset = UCSI_MESSAGE_IN_OFFSET,
			.size = 1,
			.expect_success = false,
		},
	};

	uint8_t write_buf[255];
	uint8_t read_buf[255];

	/* Initialize write buffer */
	for (int i = 0; i < sizeof(write_buf); i++) {
		write_buf[i] = i;
	}

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		uint16_t offset = cases[i].offset;
		uint8_t size = cases[i].size;
		bool expect_success = cases[i].expect_success;

		LOG_INF("Case %zu: offset=%d, size=%d, expect_success=%d", i,
			offset, size, expect_success);

		enum ec_status set_rv =
			call_set_host_cmd(offset, write_buf, size);

		if (expect_success) {
			zassert_equal(set_rv, EC_RES_SUCCESS,
				      "Case %zu failed to set: %d", i, set_rv);

			/* Read back and verify */
			memset(read_buf, 0, sizeof(read_buf));
			int read_size = 0;
			enum ec_status get_rv =
				call_get_host_cmd(offset, size, read_buf,
						  sizeof(read_buf), &read_size);
			zassert_equal(get_rv, EC_RES_SUCCESS,
				      "Case %zu failed to get: %d", i, get_rv);
			zassert_equal(read_size, size,
				      "Case %zu returned wrong size: %d", i,
				      read_size);
			zassert_mem_equal(read_buf, write_buf, size,
					  "Case %zu data mismatch", i);
		} else {
			zassert_equal(set_rv, EC_RES_ERROR,
				      "Case %zu expected EC_RES_ERROR, got %d",
				      i, set_rv);
		}
	}
}

static struct ucsi_ppm_device *get_real_ppm_dev(void)
{
	const struct device *pdc = DEVICE_DT_GET(DT_INST(0, ucsi_ppm));
	const struct ucsi_pd_driver *drv = pdc->api;

	return drv->get_ppm_dev(pdc);
}

ZTEST(ucsi_ppm, test_host_cmd_control_offset)
{
	struct ucsi_ppm_device *ppm_dev = get_real_ppm_dev();
	zassert_not_null(ppm_dev, "Failed to get real PPM device");

	uint8_t write_buf[sizeof(struct ucsi_control_t)] = { 0 };
	uint8_t read_buf[sizeof(struct ucsi_control_t)] = { 0 };
	int read_size = 0;
	enum ec_status rv;

	/* Size = 1 (valid size, write UCSI_PPM_RESET) */
	write_buf[0] = UCSI_PPM_RESET;
	rv = call_set_host_cmd(UCSI_CONTROL_OFFSET, write_buf, 1);
	zassert_equal(rv, EC_RES_SUCCESS, "Failed to set control size 1: %d",
		      rv);

	zassert_true(ppm_wait_for_cmd_to_process(ppm_dev),
		     "Timeout waiting for command to process");

	rv = call_get_host_cmd(UCSI_CONTROL_OFFSET, 1, read_buf,
			       sizeof(read_buf), &read_size);
	zassert_equal(rv, EC_RES_SUCCESS, "Failed to get control size 1: %d",
		      rv);
	zassert_equal(read_size, 1,
		      "Returned wrong size for control size 1: %d", read_size);
	zassert_equal(read_buf[0], UCSI_PPM_RESET, "Control data mismatch");

	/* Size = 8 (valid max control size, write UCSI_PPM_RESET) */
	memset(write_buf, 0, sizeof(write_buf));
	write_buf[0] = UCSI_PPM_RESET;
	rv = call_set_host_cmd(UCSI_CONTROL_OFFSET, write_buf, 8);
	zassert_equal(rv, EC_RES_SUCCESS, "Failed to set control size 8: %d",
		      rv);

	zassert_true(ppm_wait_for_cmd_to_process(ppm_dev),
		     "Timeout waiting for command to process");

	rv = call_get_host_cmd(UCSI_CONTROL_OFFSET, 8, read_buf,
			       sizeof(read_buf), &read_size);
	zassert_equal(rv, EC_RES_SUCCESS, "Failed to get control size 8: %d",
		      rv);
	zassert_equal(read_size, 8,
		      "Returned wrong size for control size 8: %d", read_size);
	zassert_mem_equal(read_buf, write_buf, 8, "Control data mismatch");

	/* Size = 9 (invalid size > sizeof(struct ucsi_control_t)) */
	rv = call_set_host_cmd(UCSI_CONTROL_OFFSET, write_buf, 9);
	zassert_equal(rv, EC_RES_ERROR,
		      "Expected EC_RES_ERROR for size 9, got %d", rv);
}

ZTEST(ucsi_ppm, test_host_cmd_set_invalid_size)
{
	struct ec_params_ucsi_ppm_set ppm_set = {
		.offset = UCSI_MESSAGE_OUT_OFFSET,
	};

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_UCSI_PPM_SET,
		.version = 0,
		.params = &ppm_set,
		.params_size = 0,
		.response = NULL,
		.response_max = 0,
		.response_size = 0,
	};

	enum ec_status rv = host_command_process(&args);

	zassert_equal(rv, EC_RES_INVALID_PARAM, "Expected %d, got %d",
		      EC_RES_INVALID_PARAM, rv);

	args.params_size = 1;
	rv = host_command_process(&args);

	zassert_equal(rv, EC_RES_INVALID_PARAM, "Expected %d, got %d",
		      EC_RES_INVALID_PARAM, rv);
}

ZTEST(ucsi_ppm, test_host_cmd_get_invalid_offset)
{
	uint8_t read_buf[10];
	enum ec_status get_rv =
		call_get_host_cmd(10000, 10, read_buf, sizeof(read_buf), NULL);

	zassert_equal(get_rv, EC_RES_ERROR, "Expected EC_RES_ERROR, got %d",
		      get_rv);
}

ZTEST(ucsi_ppm, test_ppm_write_is_valid_len_zero)
{
	int rv;

	rv = call_set_host_cmd(UCSI_MESSAGE_OUT_OFFSET, NULL, 0);
	zassert_equal(rv, EC_RES_ERROR, "Expected EC_RES_ERROR, got %d", rv);
}

ZTEST(ucsi_ppm, test_host_cmd_get_invalid_size)
{
	struct ec_params_ucsi_ppm_get ppm_get = {
		.offset = UCSI_MESSAGE_OUT_OFFSET,
		.size = 10,
	};
	uint8_t resp_buf[10];

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_UCSI_PPM_GET,
		.version = 0,
		.params = &ppm_get,
		.params_size = 0,
		.response = resp_buf,
		.response_max = sizeof(resp_buf),
		.response_size = 0,
	};

	enum ec_status rv = host_command_process(&args);

	zassert_equal(rv, EC_RES_INVALID_PARAM, "Expected %d, got %d",
		      EC_RES_INVALID_PARAM, rv);

	args.params_size = sizeof(ppm_get) - 1;
	rv = host_command_process(&args);

	zassert_equal(rv, EC_RES_INVALID_PARAM, "Expected %d, got %d",
		      EC_RES_INVALID_PARAM, rv);
}

ZTEST(ucsi_ppm, test_host_cmd_get_response_too_large)
{
	uint8_t read_buf[10];
	enum ec_status get_rv = call_get_host_cmd(
		UCSI_MESSAGE_OUT_OFFSET, 20, read_buf, sizeof(read_buf), NULL);

	zassert_equal(get_rv, EC_RES_OVERFLOW, "Expected %d, got %d",
		      EC_RES_OVERFLOW, get_rv);
}
