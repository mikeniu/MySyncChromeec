/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_ite_it52xx.h"
#include "emul/emul_pdc.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define IT52XX_NODE DT_NODELABEL(pdc_emul1)
#define IT52XX_NODE2 DT_NODELABEL(pdc_emul2)

static const struct emul *emul = EMUL_DT_GET(IT52XX_NODE);
static const struct emul *emul2 = EMUL_DT_GET(IT52XX_NODE2);
static const struct device *dev = DEVICE_DT_GET(IT52XX_NODE);
static const struct device *dev2 = DEVICE_DT_GET(IT52XX_NODE2);

#define PDC_COMMAND_COMPLETE BIT(0)
#define PDC_OPERATION_TIMEOUT K_SECONDS(2)
#define IT52XX_GET_ERROR_STATUS_DATA_LENGTH 16
#define IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH sizeof(union connector_status_t)

static K_EVENT_DEFINE(pdc_event);
static atomic_t waiting_for_command_response;
static const struct device *callback_dev;
static union cci_event_t callback_cci;

/* Unit-test callback for CC events from the driver */
static void pdc_cc_handler_cb(const struct device *pdc,
			      const struct pdc_callback *callback,
			      union cci_event_t cci_event)
{
	ARG_UNUSED(callback);

	if (!cci_event.command_completed ||
	    !atomic_cas(&waiting_for_command_response, true, false)) {
		return;
	}

	callback_dev = pdc;
	callback_cci = cci_event;
	k_event_post(&pdc_event, PDC_COMMAND_COMPLETE);
}

static struct pdc_callback cc_callback = {
	.handler = pdc_cc_handler_cb,
};

static void prepare_for_command(void)
{
	k_event_set(&pdc_event, 0);
	atomic_set(&waiting_for_command_response, true);
}

static void wait_for_command_callback(const struct device *pdc,
				      uint8_t data_len, bool error,
				      bool not_supported)
{
	uint32_t events = k_event_wait_safe(&pdc_event, PDC_COMMAND_COMPLETE,
					    false, PDC_OPERATION_TIMEOUT);

	zassert_equal(events, PDC_COMMAND_COMPLETE);
	zassert_equal(callback_dev, pdc);
	zassert_true(callback_cci.command_completed);
	zassert_equal(callback_cci.error, error);
	zassert_equal(callback_cci.not_supported, not_supported);
	zassert_equal(callback_cci.data_len, data_len);
}

static void wait_for_command_result(const struct emul *target,
				    const struct device *pdc, uint8_t data_len,
				    bool error, bool not_supported)
{
	wait_for_command_callback(pdc, data_len, error, not_supported);
	zassert_ok(emul_pdc_idle_wait(target));
}

static void wait_for_command(const struct emul *target,
			     const struct device *pdc, uint8_t data_len)
{
	wait_for_command_result(target, pdc, data_len, false, false);
}

static void check_initialization(const struct emul *target,
				 const struct device *pdc)
{
	union notification_enable_t expected = { 0 };
	union notification_enable_t actual;
	struct pdc_info_t info;
	bool initialized = false;

	for (int i = 0; i < 200; ++i) {
		if (pdc_is_init_done(pdc) &&
		    emul_it52xx_init_complete(target)) {
			initialized = true;
			break;
		}
		k_msleep(10);
	}

	zassert_true(initialized);
	zassert_ok(emul_pdc_idle_wait(target));
	zassert_true(pdc_is_init_done(pdc));
	zassert_true(emul_it52xx_init_complete(target));

	/* Verify get notification fields */
	expected.command_completed = 1;
	expected.external_supply_change = 1;
	expected.power_operation_mode_change = 1;
	expected.attention = 1;
	expected.provider_capability_change_supported = 1;
	expected.negotiated_power_level_change = 1;
	expected.pd_reset_complete = 1;
	expected.support_cam_change = 1;
	expected.battery_charging_status_change = 1;
	expected.connector_partner_change = 1;
	expected.power_direction_change = 1;
	expected.connect_change = 1;
	expected.error = 1;
	expected.sink_path_status_change = 1;

	zassert_ok(emul_it52xx_get_notification_enable(target, &actual));
	zassert_equal(actual.raw_value, expected.raw_value);

	/* Verify get PDC info fields */
	zassert_ok(pdc_get_info(pdc, &info, false));
	zassert_equal(info.fw_version, 0x010200);
	zassert_equal(info.pd_revision, 0x0302);
	zassert_equal(info.pd_version, 0x0101);
	zassert_equal(info.vid, 0x048d);
	zassert_equal(info.pid, 0x5271);
	zassert_equal(info.is_running_flash_code, 1);
	zassert_equal(info.running_in_flash_bank, 1);
	zassert_equal(strcmp(info.project_name, "GOOGLE"), 0);
}

static void read_error_status(const struct emul *target,
			      const struct device *pdc,
			      const union error_status_t *expected)
{
	union error_status_t actual = { 0 };

	prepare_for_command();
	zassert_ok(pdc_get_error_status(pdc, &actual));
	wait_for_command(target, pdc, IT52XX_GET_ERROR_STATUS_DATA_LENGTH);
	zassert_equal(actual.raw_value, expected->raw_value);
}

static void read_connector_status(const struct emul *target,
				  const struct device *pdc,
				  const union connector_status_t *expected)
{
	union connector_status_t actual = { 0 };

	prepare_for_command();
	zassert_ok(pdc_get_connector_status(pdc, &actual));
	wait_for_command(target, pdc, IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH);
	zassert_mem_equal(actual.raw_value, expected->raw_value,
			  sizeof(actual.raw_value));
}

static void *it52xx_setup(void)
{
	zassert_true(device_is_ready(dev));
	zassert_true(device_is_ready(dev2));

	pdc_start_thread(dev);
	pdc_start_thread(dev2);
	check_initialization(emul, dev);
	check_initialization(emul2, dev2);

	pdc_set_cc_callback(dev, &cc_callback);
	pdc_set_cc_callback(dev2, &cc_callback);

	return NULL;
}

ZTEST(it52xx, test_driver_initialization)
{
	union notification_enable_t notifications;
	uint8_t command_specific[3];

	check_initialization(emul, dev);
	check_initialization(emul2, dev2);

	/* Verify notifications can be configured again after initialization. */
	zassert_ok(emul_it52xx_get_notification_enable(emul, &notifications));
	command_specific[0] = notifications.raw_value;
	command_specific[1] = notifications.raw_value >> 8;
	command_specific[2] = notifications.raw_value >> 16;
	prepare_for_command();
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_SET_NOTIFICATION_ENABLE,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	wait_for_command(emul, dev, 0);
}

ZTEST(it52xx, test_get_error_status)
{
	union error_status_t first = { 0 };
	union error_status_t second = { 0 };
	union error_status_t masked_input = { 0 };
	union error_status_t masked_expected = { 0 };
	union error_status_t no_errors = { 0 };

	/* Set different error bits for two emulators */
	first.unrecognized_command = 1;
	first.invalid_command_specific_param = 1;
	first.overcurrent = 1;
	second.cc_communication_error = 1;
	second.port_partner_rejected_swap = 1;
	second.set_sink_path_rejected = 1;

	/*
	 * IT5271 only support error bit0 - bit14. Set both supported
	 * and unsupported bits to verify that unsupported bits are masked.
	 */
	masked_input.unrecognized_command = 1;
	masked_input.reserved0 = 1;
	masked_input.pdc_internal_error = 1;
	masked_expected.unrecognized_command = 1;

	/* Verify that both APIs reject NULL pointers */
	zassert_equal(emul_pdc_set_error_status(emul, NULL), -EINVAL);
	zassert_equal(pdc_get_error_status(dev, NULL), -EINVAL);

	/* Set error status to both emul, then read verify fields. */
	zassert_ok(emul_pdc_set_error_status(emul, &first));
	zassert_ok(emul_pdc_set_error_status(emul2, &second));
	read_error_status(emul, dev, &first);
	read_error_status(emul2, dev2, &second);

	/* Verify IT5271 only reports error bit0 - bit14 */
	zassert_ok(emul_pdc_set_error_status(emul, &masked_input));
	read_error_status(emul, dev, &masked_expected);

	/* GET_ERROR_STATUS clears the stored error information */
	read_error_status(emul, dev, &no_errors);
	read_error_status(emul2, dev2, &no_errors);
}

ZTEST(it52xx, test_unsupported_command_reports_cci_error)
{
	union error_status_t expected = { .unrecognized_command = 1 };
	uint8_t command_specific[1] = { 0 };

	prepare_for_command();
	/* Send an unsupported UCSI command */
	zassert_ok(pdc_execute_ucsi_cmd(dev, 0xff, 0, command_specific, NULL,
					&cc_callback));
	/* Verify [CCI] indicators */
	wait_for_command_result(emul, dev, 0, true, true);
	/* Verify get error status bits */
	read_error_status(emul, dev, &expected);
}

ZTEST(it52xx, test_invalid_vendor_parameters_report_cci_error)
{
	union error_status_t invalid_connector = {
		.non_existent_connector_number = 1,
	};
	union error_status_t invalid_command = {
		.invalid_command_specific_param = 1,
	};
	uint8_t command_specific[6] = { 0 };

	prepare_for_command();
	/* Send a supported UCSI command with an invalid connector number */
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_VENDOR_DEFINED_COMMAND,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	/* Verify [CCI] indicators */
	wait_for_command_result(emul, dev, 0, true, false);
	/* Verify get error status bits */
	read_error_status(emul, dev, &invalid_connector);

	/* Connector 1 with vendor command 1 is not supported by IT5271. */
	command_specific[0] = BIT(7) | 1;
	prepare_for_command();
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_VENDOR_DEFINED_COMMAND,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	wait_for_command_result(emul, dev, 0, true, false);
	read_error_status(emul, dev, &invalid_command);
}

ZTEST(it52xx, test_get_connector_status)
{
	union connector_status_t first = { 0 };
	union connector_status_t second = { 0 };
	union connector_status_t first_after_read;
	union connector_status_t second_after_read;

	/* Set different connector status bits for two emulators */
	first.raw_conn_status_change_bits =
		UCSI_CHANGE_BITS_EXTERNAL_SUPPLY_CHANGE |
		UCSI_CHANGE_BITS_CONNECTOR_PARTNER |
		UCSI_CHANGE_BITS_CONNECT_CHANGE;
	first.power_operation_mode = PD_OPERATION;
	first.connect_status = 1;
	first.power_direction = 0;
	first.conn_partner_flags = CONNECTOR_PARTNER_FLAG_USB;
	first.conn_partner_type = DFP_ATTACHED;
	first.rdo = 0x12345678;
	first.battery_charging_cap_status = 1;
	first.bcd_pd_version = 0x0300;
	first.orientation = 1;
	first.sink_path_status = 1;
	first.power_reading_ready = 1;
	first.current_scale = 1;
	first.peak_current = 600;
	first.average_current = 500;
	first.voltage_scale = 10;
	first.voltage_reading = 100;

	second.raw_conn_status_change_bits =
		UCSI_CHANGE_BITS_PWR_OPERATION_MODE |
		UCSI_CHANGE_BITS_PWR_DIRECTION |
		UCSI_CHANGE_BITS_SINK_PATH_STATUS_CHANGE;
	second.power_operation_mode = USB_TC_CURRENT_3A;
	second.connect_status = 1;
	second.power_direction = 1;
	second.conn_partner_flags = CONNECTOR_PARTNER_FLAG_ALTERNATE_MODE;
	second.conn_partner_type = UFP_ATTACHED;
	second.rdo = 0xaabbccdd;
	second.provider_caps_limited_reason = 2;
	second.bcd_pd_version = 0x0200;
	second.power_reading_ready = 1;
	second.current_scale = 2;
	second.peak_current = 700;
	second.average_current = 650;
	second.voltage_scale = 5;
	second.voltage_reading = 360;

	first_after_read = first;
	first_after_read.raw_conn_status_change_bits = 0;
	second_after_read = second;
	second_after_read.raw_conn_status_change_bits = 0;

	/* Verify that both APIs reject NULL pointers */
	zassert_equal(emul_pdc_set_connector_status(emul, NULL), -EINVAL);
	zassert_equal(pdc_get_connector_status(dev, NULL), -EINVAL);

	/* Set connector status to both emul, then read verify fields. */
	zassert_ok(emul_pdc_set_connector_status(emul, &first));
	zassert_ok(emul_pdc_set_connector_status(emul2, &second));
	read_connector_status(emul, dev, &first);
	read_connector_status(emul2, dev2, &second);

	/* Only the change bits are cleared by GET_CONNECTOR_STATUS. */
	read_connector_status(emul, dev, &first_after_read);
	read_connector_status(emul2, dev2, &second_after_read);
}

ZTEST(it52xx, test_get_vbus_voltage)
{
	union connector_status_t status = { 0 };
	uint16_t voltage = 0;

	/* Verify that the API rejects a NULL output pointer */
	zassert_equal(pdc_get_vbus_voltage(dev, NULL), -EINVAL);

	status.voltage_scale = 10;
	status.voltage_reading = 100;
	zassert_ok(emul_pdc_set_connector_status(emul, &status));

	prepare_for_command();
	/* Verify that the connector-status fields convert to 5000 mV */
	zassert_ok(pdc_get_vbus_voltage(dev, &voltage));
	wait_for_command(emul, dev, IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH);
	zassert_equal(voltage, 5000);

	status.voltage_scale = 5;
	status.voltage_reading = 360;
	zassert_ok(emul_pdc_set_connector_status(emul2, &status));
	voltage = 0;

	prepare_for_command();
	/* Verify that the connector-status fields convert to 9000 mV */
	zassert_ok(pdc_get_vbus_voltage(dev2, &voltage));
	wait_for_command(emul2, dev2, IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH);
	zassert_equal(voltage, 9000);
}

ZTEST_SUITE(it52xx, NULL, it52xx_setup, NULL, NULL, NULL);
