/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le_ll.h"
#include "common.h"
#include "console.h"
#include "test_util.h"
#include "util.h"

static int adv_enable_calls;

uint8_t ll_reset(void)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_buffer_size(uint8_t *return_params)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_local_supported_features(uint8_t *return_params)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_supported_states(uint8_t *return_params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_host_channel_classification(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_random_address(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_advertising_enable(uint8_t *params)
{
	adv_enable_calls++;
	return HCI_SUCCESS;
}

uint8_t ll_set_adv_data(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_advertising_params(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_tx_power(void)
{
	return 0;
}

uint8_t ll_set_scan_response_data(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_remote_used_features(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_scan_enable(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_set_scan_params(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_clear_allow_list(void)
{
	return HCI_SUCCESS;
}

uint8_t ll_read_allow_list_size(uint8_t *return_params)
{
	return HCI_SUCCESS;
}

uint8_t ll_add_device_to_allow_list(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_remove_device_from_allow_list(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_receiver_test(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_transmitter_test(uint8_t *params)
{
	return HCI_SUCCESS;
}

uint8_t ll_test_end(uint8_t *return_params)
{
	return HCI_SUCCESS;
}

static int run_cmd(const char *cmd)
{
	char buf[64];

	strzcpy(buf, cmd, sizeof(buf));
	return test_send_console_command(buf);
}

test_static int test_command_ble_hci_adv_bounds(void)
{
	/* Valid boundary indices (0 and ARRAY_SIZE - 1 == 1) */
	adv_enable_calls = 0;
	TEST_EQ(run_cmd("ble_hci_adv 0"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 1, "%d");

	TEST_EQ(run_cmd("ble_hci_adv 1"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 2, "%d");

	TEST_EQ(run_cmd("ble_hci_adv 0 0"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 3, "%d");

	TEST_EQ(run_cmd("ble_hci_adv 0 1"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 4, "%d");

	TEST_EQ(run_cmd("ble_hci_adv 0 0 0"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 5, "%d");

	TEST_EQ(run_cmd("ble_hci_adv 1 1 1"), EC_SUCCESS, "%d");
	TEST_EQ(adv_enable_calls, 6, "%d");

	/* Invalid adv (argv[1]): negative, exact boundary (2), above boundary
	 * (3), non-numeric */
	adv_enable_calls = 0;
	TEST_EQ(run_cmd("ble_hci_adv -1"), EC_ERROR_PARAM1, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 2"), EC_ERROR_PARAM1, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 3"), EC_ERROR_PARAM1, "%d");
	TEST_EQ(run_cmd("ble_hci_adv invalid"), EC_ERROR_PARAM1, "%d");
	TEST_EQ(adv_enable_calls, 0, "%d");

	/* Invalid p (argv[2]): negative, exact boundary (2), above boundary
	 * (3), non-numeric */
	TEST_EQ(run_cmd("ble_hci_adv 0 -1"), EC_ERROR_PARAM2, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 2"), EC_ERROR_PARAM2, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 3"), EC_ERROR_PARAM2, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 invalid"), EC_ERROR_PARAM2, "%d");
	TEST_EQ(adv_enable_calls, 0, "%d");

	/* Invalid scan_rsp (argv[3]): negative, exact boundary (2), above
	 * boundary (3), non-numeric */
	TEST_EQ(run_cmd("ble_hci_adv 0 0 -1"), EC_ERROR_PARAM3, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 0 2"), EC_ERROR_PARAM3, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 0 3"), EC_ERROR_PARAM3, "%d");
	TEST_EQ(run_cmd("ble_hci_adv 0 0 invalid"), EC_ERROR_PARAM3, "%d");
	TEST_EQ(adv_enable_calls, 0, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_command_ble_hci_adv_bounds);
	test_print_result();
}
