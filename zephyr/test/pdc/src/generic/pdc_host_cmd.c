/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "drivers/intel_altmode.h"
#include "drivers/ucsi_v3.h"
#include "ec_commands.h"
#include "emul/emul_pdc.h"
#include "fakes.h"
#include "mock_pdc_power_mgmt.h"

#include "i2c/i2c.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0

BUILD_ASSERT(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT);

static void host_cmd_pdc_reset(void *fixture)
{
	helper_reset_pdc_power_mgmt_fakes();
	RESET_FAKE(system_is_locked);
	RESET_FAKE(board_allow_i2c_passthru);
}

ZTEST_SUITE(host_cmd_pdc, NULL, NULL, host_cmd_pdc_reset, host_cmd_pdc_reset,
	    NULL);

const static struct pdc_info_t info = {
	/* 10.20.30 */
	.fw_version = (10 << 16) | (20 << 8) | (30 << 0),
	.pd_revision = 123,
	.pd_version = 456,
	/* VID:PID = 7890:3456 */
	.vid = 0x7890,
	.pid = 0x3456,
	.is_running_flash_code = 1,
	.running_in_flash_bank = 16,
	.project_name = "ProjectName",
	.extra = 0xffff,
	.driver_name = "driver_name",
	.no_fw_update = true,
};

const static union data_status_reg data_status = {
	/* 0x71 0x85 0x00 0x00 0x00 */
	.data_conn = 1, .usb2 = 1,   .usb3_2 = 1,  .usb3_2_speed = 1,
	.dp = 1,	.dp_pin = 1, .hpd_lvl = 1,
};

/**
 * @brief Custom fake for pdc_power_mgmt_get_info that outputs some test PDC
 *        chip info.
 */
static int custom_fake_pdc_power_mgmt_get_info(int port, struct pdc_info_t *out,
					       bool live)
{
	zassert_not_null(out);

	*out = info;

	return 0;
}

/**
 * @brief Custom fake for pdc_power_mgmt_get_pch_data_status that outputs some
 * test PDC data status.
 */
static int custom_fake_pdc_power_mgmt_get_pch_data_status(int port,
							  uint8_t *out)
{
	zassert_not_null(out);

	if (port < 0 || port > CONFIG_USB_PD_PORT_MAX_COUNT)
		return -ERANGE;

	memcpy(out, data_status.raw_value, sizeof(data_status));

	return 0;
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v0)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info resp;

	/* Error calling pdc_power_mgmt_chip_info() */
	pdc_power_mgmt_get_info_fake.return_val = -1;

	rv = ec_cmd_pd_chip_info(NULL, &req, &resp);

	zassert_equal(EC_RES_ERROR, rv, "Got %d, expected %d", rv,
		      EC_RES_ERROR);

	RESET_FAKE(pdc_power_mgmt_get_info);

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(info.vid, resp.vendor_id);
	zassert_equal(info.pid, resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v1)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v1 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v1(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(info.vid, resp.vendor_id);
	zassert_equal(info.pid, resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v2)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v2 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v2(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(info.vid, resp.vendor_id);
	zassert_equal(info.pid, resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);

	/* Fields added in V2 */
	zassert_mem_equal(info.project_name, resp.fw_name_str,
			  sizeof(info.project_name));
	zassert_equal(info.no_fw_update,
		      !!(resp.fw_update_flags &
			 USB_PD_CHIP_INFO_FWUP_FLAG_NO_UPDATE));
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v3)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v3 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v3(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(info.vid, resp.vendor_id);
	zassert_equal(info.pid, resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);

	/* Fields added in V2 */
	zassert_mem_equal(info.project_name, resp.fw_name_str,
			  sizeof(info.project_name));
	zassert_equal(info.no_fw_update,
		      !!(resp.fw_update_flags &
			 USB_PD_CHIP_INFO_FWUP_FLAG_NO_UPDATE));

	/* Field added in V3 */
	zassert_ok(strncmp(info.driver_name, resp.driver_name,
			   strlen(info.driver_name)));
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_ports)
{
	int rv;
	struct ec_response_usb_pd_ports resp;

	rv = ec_cmd_usb_pd_ports(NULL, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT, resp.num_ports);
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_mux_info)
{
	int expect = (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
		      USB_PD_MUX_HPD_LVL);
	struct ec_params_usb_pd_mux_info param;
	struct ec_response_usb_pd_mux_info resp;

	pdc_power_mgmt_get_pch_data_status_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_pch_data_status;

	param.port = -1;
	zassert_not_ok(ec_cmd_usb_pd_mux_info(NULL, &param, &resp));

	param.port = 0;
	zassert_ok(ec_cmd_usb_pd_mux_info(NULL, &param, &resp));
	zassert_equal(resp.flags, expect);
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_control__invalid_inputs)
{
	struct ec_params_usb_pd_control params;
	struct ec_response_usb_pd_control_v2 resp;
	int rv;

	/* Port out of range */
	params = (struct ec_params_usb_pd_control){
		.port = CONFIG_USB_PD_PORT_MAX_COUNT,
	};

	rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

	zassert_equal(EC_RES_INVALID_PARAM, rv,
		      "Expected EC_RES_INVALID_PARAM (%d), got %d",
		      EC_RES_INVALID_PARAM, rv);

	/* Role out of range */
	params = (struct ec_params_usb_pd_control){
		.role = USB_PD_CTRL_ROLE_COUNT,
	};

	rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

	zassert_equal(EC_RES_INVALID_PARAM, rv,
		      "Expected EC_RES_INVALID_PARAM (%d), got %d",
		      EC_RES_INVALID_PARAM, rv);

	/* Mux choice out of range */
	params = (struct ec_params_usb_pd_control){
		.mux = USB_PD_CTRL_MUX_COUNT,
	};

	rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

	zassert_equal(EC_RES_INVALID_PARAM, rv,
		      "Expected EC_RES_INVALID_PARAM (%d), got %d",
		      EC_RES_INVALID_PARAM, rv);
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_control__change_dual_role_mode)
{
	struct ec_params_usb_pd_control params = { 0 };
	struct ec_response_usb_pd_control_v2 resp;
	int rv;

	const static struct {
		enum usb_pd_control_role requested;
		enum pd_dual_role_states expected;
	} test_roles[] = {
		{
			.requested = USB_PD_CTRL_ROLE_NO_CHANGE,
		},
		{
			.requested = USB_PD_CTRL_ROLE_TOGGLE_ON,
			.expected = PD_DRP_TOGGLE_ON,
		},
		{
			.requested = USB_PD_CTRL_ROLE_TOGGLE_OFF,
			.expected = PD_DRP_TOGGLE_OFF,
		},
		{
			.requested = USB_PD_CTRL_ROLE_FORCE_SINK,
			.expected = PD_DRP_FORCE_SINK,
		},
		{
			.requested = USB_PD_CTRL_ROLE_FORCE_SOURCE,
			.expected = PD_DRP_FORCE_SOURCE,
		},
		{
			.requested = USB_PD_CTRL_ROLE_FREEZE,
			.expected = PD_DRP_FREEZE,
		},
	};

	for (int i = 0; i < ARRAY_SIZE(test_roles); i++) {
		RESET_FAKE(pdc_power_mgmt_set_dual_role);

		params.role = test_roles[i].requested;
		params.port = 0;

		rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

		zassert_equal(EC_RES_SUCCESS, rv,
			      "Expected EC_RES_SUCCESS (%d), got %d",
			      EC_RES_SUCCESS, rv);

		if (params.role == USB_PD_CTRL_ROLE_NO_CHANGE) {
			/* Special case where no call/change should occur. */
			zassert_equal(
				0, pdc_power_mgmt_set_dual_role_fake.call_count,
				"Dual role mode should not have been changed");
			continue;
		}

		zassert_equal(1, pdc_power_mgmt_set_dual_role_fake.call_count,
			      "Dual role mode should have been called once");
		zassert_equal(
			0, pdc_power_mgmt_set_dual_role_fake.arg0_history[0]);
		zassert_equal(test_roles[i].expected,
			      pdc_power_mgmt_set_dual_role_fake.arg1_history[0],
			      "Set dual role mode to %d but expected %d",
			      pdc_power_mgmt_set_dual_role_fake.arg1_history[0],
			      test_roles[i].expected);
	}
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_control__swap_power_role)
{
	struct ec_params_usb_pd_control params = { 0 };
	struct ec_response_usb_pd_control_v2 resp;
	int rv;

	params.swap = USB_PD_CTRL_SWAP_POWER;

	rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

	zassert_equal(EC_RES_SUCCESS, rv,
		      "Expected EC_RES_SUCCESS (%d), got %d", EC_RES_SUCCESS,
		      rv);

	zassert_equal(1, pdc_power_mgmt_request_power_swap_fake.call_count,
		      "pdc_power_mgmt_request_power_swap not called!");
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_control__swap_data_role)
{
	struct ec_params_usb_pd_control params = { 0 };
	struct ec_response_usb_pd_control_v2 resp;
	int rv;

	params.swap = USB_PD_CTRL_SWAP_DATA;

	rv = ec_cmd_usb_pd_control_v2(NULL, &params, &resp);

	zassert_equal(EC_RES_SUCCESS, rv,
		      "Expected EC_RES_SUCCESS (%d), got %d", EC_RES_SUCCESS,
		      rv);

	zassert_equal(1, pdc_power_mgmt_request_data_swap_fake.call_count,
		      "pdc_power_mgmt_request_data_swap not called!");
}

static bool custom_fake_pdc_is_update_mode(int *port)
{
	if (pdc_power_mgmt_update_start_fake.call_count > 0 &&
	    pdc_power_mgmt_update_finish_fake.call_count == 0) {
		if (port) {
			*port = pdc_power_mgmt_update_start_fake.arg0_history[0];
		}
		return true;
	}
	return false;
}

static int custom_fake_pdc_get_hw_config(int port,
					 struct pdc_hw_config_t *config)
{
	config->bus_type = PDC_BUS_TYPE_I2C;
	config->i2c.bus = i2c_get_device_for_port(0);
	config->i2c.addr = (port == 0) ? 0x66 : 0x67;
	return 0;
}

ZTEST(host_cmd_pdc, test_ec_cmd_pdc_update)
{
	struct ec_params_pdc_update params;
	struct ec_response_pdc_update resp;
	int rv;

	/* Test START */
	params.port = 0;
	params.subcmd = PDC_UPDATE_START;
	pdc_power_mgmt_update_start_fake.return_val = 0;
	rv = ec_cmd_pdc_update(NULL, &params, &resp);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(1, pdc_power_mgmt_update_start_fake.call_count);
	zassert_equal(0, pdc_power_mgmt_update_start_fake.arg0_history[0]);

	/* Test GET_STATUS while in update mode */
	params.subcmd = PDC_UPDATE_GET_STATUS;
	pdc_power_mgmt_is_update_mode_fake.custom_fake =
		custom_fake_pdc_is_update_mode;
	pdc_power_mgmt_get_hw_config_fake.custom_fake =
		custom_fake_pdc_get_hw_config;
	rv = ec_cmd_pdc_update(NULL, &params, &resp);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_true(resp.status);
	zassert_equal(0, resp.port);
	zassert_equal(0, resp.i2c_bus); /* i2c0 is bus 0 */
	zassert_equal(0x66, resp.i2c_addr);

	/* Test FINISH */
	params.subcmd = PDC_UPDATE_FINISH;
	pdc_power_mgmt_update_finish_fake.return_val = 0;
	rv = ec_cmd_pdc_update(NULL, &params, &resp);
	zassert_equal(EC_RES_SUCCESS, rv);
	zassert_equal(1, pdc_power_mgmt_update_finish_fake.call_count);
}

ZTEST(host_cmd_pdc, test_ec_cmd_i2c_passthru_pdc)
{
	struct {
		struct ec_params_i2c_passthru p;
		struct ec_params_i2c_passthru_msg msg[1];
		uint8_t data[2];
	} __packed req;
	struct {
		struct ec_response_i2c_passthru r;
		uint8_t data[2];
	} __packed resp;
	int rv;

	/* Setup: Mock update mode and hardware config */
	pdc_power_mgmt_get_usb_pd_port_count_fake.return_val = 1;
	pdc_power_mgmt_update_start_fake.call_count = 1;
	pdc_power_mgmt_update_start_fake.arg0_history[0] = 0;
	pdc_power_mgmt_is_update_mode_fake.custom_fake =
		custom_fake_pdc_is_update_mode;
	pdc_power_mgmt_get_hw_config_fake.custom_fake =
		custom_fake_pdc_get_hw_config;

	/* 1. Success path (Read 2 bytes from PDC) */
	req.p.port = 0; /* i2c0 */
	req.p.num_msgs = 1;
	req.msg[0].addr_flags = 0x66 | EC_I2C_FLAG_READ; /* Match PDC addr */
	req.msg[0].len = 2;

	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));
	zassert_equal(EC_RES_SUCCESS, rv, "Expected success, got %d", rv);
	zassert_equal(0, resp.r.i2c_status);

	/* 2. Access Denied (PDC not in update mode) */
	pdc_power_mgmt_update_start_fake.call_count = 0;
	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));
	zassert_equal(EC_RES_ACCESS_DENIED, rv);
	pdc_power_mgmt_update_start_fake.call_count = 1;

	/* 3. Success (Different address on same bus - NOT a PDC) */
	req.msg[0].addr_flags = 0x09 | EC_I2C_FLAG_READ;
	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));
	zassert_equal(EC_RES_SUCCESS, rv);
}

ZTEST(host_cmd_pdc, test_ec_cmd_i2c_passthru_pdc_port_mismatch)
{
	struct {
		struct ec_params_i2c_passthru p;
		struct ec_params_i2c_passthru_msg msg[1];
	} __packed req;
	struct {
		struct ec_response_i2c_passthru r;
		uint8_t data[1];
	} __packed resp;
	int rv;

	/* Setup: Port 0 is in update mode */
	pdc_power_mgmt_get_usb_pd_port_count_fake.return_val = 2;
	pdc_power_mgmt_update_start_fake.call_count = 1;
	pdc_power_mgmt_update_start_fake.arg0_history[0] = 0;
	pdc_power_mgmt_is_update_mode_fake.custom_fake =
		custom_fake_pdc_is_update_mode;
	pdc_power_mgmt_get_hw_config_fake.custom_fake =
		custom_fake_pdc_get_hw_config;

	/* Attempt passthru to Port 1's PDC (Addr 0x67) on I2C bus 0 */
	req.p.port = 0;
	req.p.num_msgs = 1;
	req.msg[0].addr_flags = 0x67 | EC_I2C_FLAG_READ;
	req.msg[0].len = 1;

	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));

	/* Should be denied because Port 0 is the one in update mode */
	zassert_equal(EC_RES_ACCESS_DENIED, rv, "Expected ACCESS_DENIED, got %d",
		      rv);
}

ZTEST(host_cmd_pdc, test_ec_cmd_i2c_passthru_pdc_mixed_msgs)
{
	struct {
		struct ec_params_i2c_passthru p;
		struct ec_params_i2c_passthru_msg msg[2];
	} __packed req;
	struct {
		struct ec_response_i2c_passthru r;
		uint8_t data[2];
	} __packed resp;
	int rv;

	/* Setup: PDC is NOT in update mode */
	pdc_power_mgmt_update_start_fake.call_count = 0;
	pdc_power_mgmt_is_update_mode_fake.return_val = false;
	pdc_power_mgmt_get_hw_config_fake.custom_fake =
		custom_fake_pdc_get_hw_config;

	req.p.port = 0;
	req.p.num_msgs = 2;

	/* Msg 1: Allowed address (Battery) */
	req.msg[0].addr_flags = 0x09 | EC_I2C_FLAG_READ;
	req.msg[0].len = 1;

	/* Msg 2: Forbidden address (PDC) */
	req.msg[1].addr_flags = 0x66 | EC_I2C_FLAG_READ;
	req.msg[1].len = 1;

	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));

	/* The entire transaction must be denied if any part targets an unauthorized PDC */
	zassert_equal(EC_RES_ACCESS_DENIED, rv,
		      "Expected ACCESS_DENIED for mixed msgs, got %d", rv);
}

ZTEST(host_cmd_pdc, test_ec_cmd_i2c_passthru_pdc_locked_system)
{
	struct {
		struct ec_params_i2c_passthru p;
		struct ec_params_i2c_passthru_msg msg[1];
	} __packed req;
	struct {
		struct ec_response_i2c_passthru r;
		uint8_t data[1];
	} __packed resp;
	int rv;

	/* Setup: PDC is in update mode, but system is LOCKED */
	pdc_power_mgmt_get_usb_pd_port_count_fake.return_val = 1;
	pdc_power_mgmt_update_start_fake.call_count = 1;
	pdc_power_mgmt_update_start_fake.arg0_history[0] = 0;
	pdc_power_mgmt_is_update_mode_fake.custom_fake =
		custom_fake_pdc_is_update_mode;
	pdc_power_mgmt_get_hw_config_fake.custom_fake =
		custom_fake_pdc_get_hw_config;

	system_is_locked_fake.return_val = 1;
	board_allow_i2c_passthru_fake.return_val = 0; /* Not in allowlist */

	req.p.port = 0;
	req.p.num_msgs = 1;
	req.msg[0].addr_flags = 0x66 | EC_I2C_FLAG_READ;
	req.msg[0].len = 1;

	rv = CROS_EC_COMMAND(NULL, EC_CMD_I2C_PASSTHRU, 0, &req, sizeof(req),
			     &resp, sizeof(resp));

	/* Locked system MUST deny passthru if not in allowlist, even in update mode */
	zassert_equal(EC_RES_ACCESS_DENIED, rv,
		      "Expected ACCESS_DENIED for locked system, got %d", rv);
}

