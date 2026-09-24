/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_ite_it52xx.h"
#include "emul/emul_pdc.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define IT52XX_NODE DT_NODELABEL(pdc_emul1)
#define IT52XX_NODE2 DT_NODELABEL(pdc_emul2)

static const struct emul *emul = EMUL_DT_GET(IT52XX_NODE);
static const struct emul *emul2 = EMUL_DT_GET(IT52XX_NODE2);
static const struct device *dev = DEVICE_DT_GET(IT52XX_NODE);
static const struct device *dev2 = DEVICE_DT_GET(IT52XX_NODE2);
static const struct gpio_dt_spec irq_gpio =
	GPIO_DT_SPEC_GET(IT52XX_NODE, irq_gpios);

#define PDC_COMMAND_COMPLETE BIT(0)
#define PDC_CONNECTOR_CHANGE BIT(1)
#define PDC_OPERATION_TIMEOUT K_SECONDS(2)
#define IT52XX_GET_ERROR_STATUS_DATA_LENGTH 16
#define IT52XX_GET_CAPABILITY_DATA_LENGTH sizeof(struct capability_t)
#define IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH sizeof(union connector_status_t)
#define IT52XX_CMD_CONTROL 0x81
#define IT52XX_CMD_MSGIN 0x82
#define IT52XX_CMD_CLEAR_ALERT 0xbc

static K_EVENT_DEFINE(pdc_event);
static atomic_t waiting_for_command_response;
static const struct device *callback_dev;
static union cci_event_t callback_cci;
static const struct device *ci_callback_dev;
static union cci_event_t ci_callback_cci;
static atomic_t ci_callback_count;
static atomic_t inject_connector_change;
static const struct emul *injection_emul;
static union connector_status_t injection_status;
static int injection_result;
static atomic_t track_command_ci_order;
static atomic_t command_callback_seen;
static atomic_t ci_before_command_callback;

/* Track the failed transfer only until recovery sends PPM_RESET. */
static struct {
	int reg;
	bool read;
	uint8_t control_command;
	atomic_t injected;
	atomic_t attempts;
	atomic_t resets;
	atomic_t msgin_reads;
	i2c_common_emul_write_func original_write;
	void *original_write_data;
	i2c_common_emul_finish_write_func original_finish_write;
	i2c_common_emul_start_read_func original_start_read;
} i2c_fault;

static int record_i2c_write(const struct emul *target, int reg, uint8_t value,
			    int bytes, void *data)
{
	ARG_UNUSED(data);

	if (reg == IT52XX_CMD_CONTROL && bytes == 2) {
		i2c_fault.control_command = value;
	}
	if (i2c_fault.original_write != NULL) {
		return i2c_fault.original_write(target, reg, value, bytes,
						i2c_fault.original_write_data);
	}
	return 1;
}

static int fail_completed_i2c_write(const struct emul *target, int reg,
				    int bytes)
{
	int rv = i2c_fault.original_finish_write(target, reg, bytes);

	if (rv != 0) {
		return rv;
	}
	if (reg == IT52XX_CMD_CONTROL && bytes > 2 &&
	    i2c_fault.control_command == UCSI_PPM_RESET) {
		atomic_inc(&i2c_fault.resets);
	}
	if (!i2c_fault.read && reg == i2c_fault.reg &&
	    atomic_get(&i2c_fault.resets) == 0) {
		atomic_inc(&i2c_fault.attempts);
		if (atomic_cas(&i2c_fault.injected, false, true)) {
			/* The PDC accepted the write, but the EC saw a timeout.
			 */
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static int fail_i2c_read_once(const struct emul *target, int reg)
{
	if (atomic_get(&i2c_fault.resets) == 0) {
		if (reg == IT52XX_CMD_MSGIN) {
			atomic_inc(&i2c_fault.msgin_reads);
		}
		if (i2c_fault.read && reg == i2c_fault.reg) {
			atomic_inc(&i2c_fault.attempts);
			if (atomic_cas(&i2c_fault.injected, false, true)) {
				return -EIO;
			}
		}
	}
	return i2c_fault.original_start_read(target, reg);
}

static void restore_i2c_fault_hooks(void)
{
	struct i2c_common_emul_data *common =
		emul_it52xx_get_i2c_common_data(emul);

	if (i2c_fault.original_finish_write == NULL) {
		return;
	}
	zassert_ok(i2c_common_emul_lock_data(common, K_FOREVER));
	common->write_func = i2c_fault.original_write;
	common->write_func_data = i2c_fault.original_write_data;
	common->finish_write = i2c_fault.original_finish_write;
	common->start_read = i2c_fault.original_start_read;
	i2c_fault.original_finish_write = NULL;
	zassert_ok(i2c_common_emul_unlock_data(common));
}

static struct {
	bool installed;
	uint8_t control_command;
	atomic_t resets;
	atomic_t notifications;
	atomic_t info_requests;
	atomic_t error_requests;
	i2c_common_emul_write_func original_write;
	void *original_write_data;
} init_fault;

static int fail_first_init_info_request(const struct emul *target, int reg,
					uint8_t value, int bytes, void *data)
{
	ARG_UNUSED(data);

	if (reg == IT52XX_CMD_CONTROL && bytes == 2) {
		init_fault.control_command = value;
		switch (value) {
		case UCSI_PPM_RESET:
			atomic_inc(&init_fault.resets);
			break;
		case UCSI_SET_NOTIFICATION_ENABLE:
			atomic_inc(&init_fault.notifications);
			break;
		case UCSI_VENDOR_DEFINED_COMMAND:
			atomic_inc(&init_fault.info_requests);
			break;
		case UCSI_GET_ERROR_STATUS:
			atomic_inc(&init_fault.error_requests);
			break;
		}
	}
	if (reg == IT52XX_CMD_CONTROL && bytes == 4 &&
	    init_fault.control_command == UCSI_VENDOR_DEFINED_COMMAND &&
	    atomic_get(&init_fault.info_requests) == 1) {
		struct i2c_common_emul_data *common =
			emul_it52xx_get_i2c_common_data(target);
		int rv;

		/*
		 * Give GET_PDC_INFO an invalid connector in the emulator only.
		 * The transfer succeeds, but CCI reports a UCSI error which
		 * GET_ERROR_STATUS must read before the port is disabled.
		 */
		rv = i2c_common_emul_lock_data(common, K_FOREVER);
		if (rv != 0) {
			return rv;
		}
		rv = common->write_byte(target, reg, 0, bytes);
		i2c_common_emul_unlock_data(common);
		return rv;
	}
	if (init_fault.original_write != NULL) {
		return init_fault.original_write(
			target, reg, value, bytes,
			init_fault.original_write_data);
	}
	return 1;
}

static void restore_init_fault_hook(void)
{
	struct i2c_common_emul_data *common =
		emul_it52xx_get_i2c_common_data(emul);

	if (!init_fault.installed) {
		return;
	}
	zassert_ok(i2c_common_emul_lock_data(common, K_FOREVER));
	common->write_func = init_fault.original_write;
	common->write_func_data = init_fault.original_write_data;
	init_fault.installed = false;
	zassert_ok(i2c_common_emul_unlock_data(common));
}

static void it52xx_after(void *fixture)
{
	ARG_UNUSED(fixture);
	restore_i2c_fault_hooks();
	restore_init_fault_hook();
}

/* Unit-test callback for CC events from PDC driver. */
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
	if (atomic_get(&track_command_ci_order)) {
		atomic_set(&command_callback_seen, true);
	}
	if (atomic_cas(&inject_connector_change, true, false)) {
		injection_result = emul_pdc_set_connector_status(
			injection_emul, &injection_status);
		if (injection_result == 0) {
			injection_result = emul_pdc_pulse_irq(injection_emul);
		}
	}
	k_event_post(&pdc_event, PDC_COMMAND_COMPLETE);
}

static struct pdc_callback cc_callback = {
	.handler = pdc_cc_handler_cb,
};

/* Unit-test callback for CI events from PDC driver. */
static void pdc_ci_handler_cb(const struct device *pdc,
			      const struct pdc_callback *callback,
			      union cci_event_t cci_event)
{
	ARG_UNUSED(callback);

	ci_callback_dev = pdc;
	ci_callback_cci = cci_event;
	if (atomic_get(&track_command_ci_order) &&
	    !atomic_get(&command_callback_seen)) {
		atomic_set(&ci_before_command_callback, true);
	}
	atomic_inc(&ci_callback_count);
	k_event_post(&pdc_event, PDC_CONNECTOR_CHANGE);
}

/* Each PDC instance's callback list requires a unique callback object. */
static struct pdc_callback ci_callback = {
	.handler = pdc_ci_handler_cb,
};

static struct pdc_callback ci_callback2 = {
	.handler = pdc_ci_handler_cb,
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

	/* Verify the notification-enable fields. */
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

	/* Verify the PDC information fields. */
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

static void wait_for_connector_change(const struct device *pdc,
				      uint8_t connector_number)
{
	uint32_t events = k_event_wait_safe(&pdc_event, PDC_CONNECTOR_CHANGE,
					    false, PDC_OPERATION_TIMEOUT);

	zassert_equal(events, PDC_CONNECTOR_CHANGE);
	zassert_equal(ci_callback_dev, pdc);
	zassert_equal(ci_callback_cci.connector_change, connector_number);
	zassert_true(ci_callback_cci.vendor_defined_indicator);
	zassert_false(ci_callback_cci.command_completed);
	/* Allow the driver to leave the callback and enter ST_GET_CCI. */
	k_msleep(1);
}

static void inject_connector_change_after_get(
	const struct emul *target,
	const union connector_status_t *connector_status)
{
	injection_emul = target;
	injection_status = *connector_status;
	injection_result = -EINPROGRESS;
	atomic_set(&inject_connector_change, true);
}

static void get_connector_status_after_ci(const struct device *pdc,
					  union connector_status_t *status)
{
	int rv = -ETIMEDOUT;

	prepare_for_command();
	for (int i = 0; i < 200; ++i) {
		rv = pdc_get_connector_status(pdc, status);
		if (rv != -EBUSY) {
			break;
		}
		k_msleep(10);
	}
	zassert_ok(rv);
}

static void
read_connector_status_after_ci(const struct emul *target,
			       const struct device *pdc,
			       const union connector_status_t *expected)
{
	union connector_status_t actual = { 0 };
	get_connector_status_after_ci(pdc, &actual);
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
	zassert_ok(pdc_add_ci_callback(dev, &ci_callback));
	zassert_ok(pdc_add_ci_callback(dev2, &ci_callback2));

	return NULL;
}

ZTEST(it52xx, test_unsupported_command_reports_cci_error)
{
	union error_status_t expected = { .unrecognized_command = 1 };
	uint8_t command_specific[1] = { 0 };

	prepare_for_command();
	/* Send an unsupported UCSI command 0xff. */
	zassert_ok(pdc_execute_ucsi_cmd(dev, 0xff, 0, command_specific, NULL,
					&cc_callback));
	/*
	 * Verify [CCI] indicators:
	 * command_completed = 1, error = 1, not_supported = 1.
	 */
	wait_for_command_result(emul, dev, 0, true, true);
	/* Verify GET_ERROR_STATUS reports an unrecognized command. */
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
	/* Send VENDOR_DEFINED_COMMAND with invalid connector number 0. */
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_VENDOR_DEFINED_COMMAND,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	/*
	 * Verify [CCI] indicators:
	 * command_completed = 1, error = 1, not_supported = 0.
	 */
	wait_for_command_result(emul, dev, 0, true, false);
	/* Verify GET_ERROR_STATUS reports a non-existent connector. */
	read_error_status(emul, dev, &invalid_connector);

	/* Send vendor command 1 to connector 1 (IT5271 only supports cmd 0). */
	command_specific[0] = BIT(7) | 1;
	prepare_for_command();
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_VENDOR_DEFINED_COMMAND,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	wait_for_command_result(emul, dev, 0, true, false);
	read_error_status(emul, dev, &invalid_command);
}

ZTEST(it52xx, test_connector_change_interrupt)
{
	union connector_status_t first = { 0 };
	union connector_status_t second = { 0 };
	union connector_status_t no_enabled_change = { 0 };
	union connector_status_t first_after_read;
	union connector_status_t second_after_read;
	uint32_t events;

	/* Choose different connector status for two emulators. */
	first.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE |
					    UCSI_CHANGE_BITS_CONNECTOR_PARTNER;
	first.connect_status = 1;
	first.conn_partner_type = DFP_ATTACHED;
	second.raw_conn_status_change_bits =
		UCSI_CHANGE_BITS_PWR_DIRECTION |
		UCSI_CHANGE_BITS_SINK_PATH_STATUS_CHANGE;
	second.connect_status = 1;
	second.power_direction = 1;
	second.sink_path_status = 1;
	first_after_read = first;
	first_after_read.raw_conn_status_change_bits = 0;
	second_after_read = second;
	second_after_read.raw_conn_status_change_bits = 0;
	/* Reserved status-change bit 4 has no notification-enable mapping. */
	no_enabled_change.raw_conn_status_change_bits = BIT(4);

	zassert_ok(emul_pdc_set_connector_status(emul, &first));
	k_event_set(&pdc_event, 0);
	/* Trigger a connector-change interrupt on 1st PDC. */
	zassert_ok(emul_pdc_pulse_irq(emul));
	/* Verify that the CI callback reports connector 1 on 1st PDC. */
	wait_for_connector_change(dev, 1);
	/* Read and verify GET_CONNECTOR_STATUS after the CI event. */
	read_connector_status_after_ci(emul, dev, &first);
	/* Verify that GET_CONNECTOR_STATUS cleared only the change bits. */
	read_connector_status(emul, dev, &first_after_read);

	zassert_ok(emul_pdc_set_connector_status(emul2, &second));
	k_event_set(&pdc_event, 0);
	/* Trigger a connector-change interrupt on 2nd PDC. */
	zassert_ok(emul_pdc_pulse_irq(emul2));
	/* Verify that the CI callback reports connector 2 on 2nd PDC. */
	wait_for_connector_change(dev2, 2);
	/* Read and verify GET_CONNECTOR_STATUS after the CI event. */
	read_connector_status_after_ci(emul2, dev2, &second);
	/* Verify that GET_CONNECTOR_STATUS cleared only the change bits. */
	read_connector_status(emul2, dev2, &second_after_read);

	/* Reserved change bit 4 has no enabled notification mapping. */
	zassert_ok(emul_pdc_set_connector_status(emul, &no_enabled_change));
	k_event_set(&pdc_event, 0);
	/* Verify that a disabled status-change notification does not invoke the
	 * CI callback. */
	zassert_ok(emul_pdc_pulse_irq(emul));
	events = k_event_wait_safe(&pdc_event, PDC_CONNECTOR_CHANGE, false,
				   K_MSEC(50));
	zassert_equal(events, 0);
	zassert_ok(emul_pdc_idle_wait(emul));
}

ZTEST(it52xx, test_no_empty_ci_after_connector_change_is_consumed)
{
	union connector_status_t first = { 0 };
	union connector_status_t combined;
	union connector_status_t actual = { 0 };
	uint32_t events;

	first.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE;
	first.connect_status = 1;
	/* Build the status expected when both changes are pending. */
	combined = first;
	combined.raw_conn_status_change_bits |=
		UCSI_CHANGE_BITS_CONNECTOR_PARTNER;
	combined.conn_partner_type = DFP_ATTACHED;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	atomic_set(&ci_callback_count, 0);
	atomic_set(&inject_connector_change, false);

	/* Report the first connector change and wait for its CI callback. */
	zassert_ok(emul_pdc_set_connector_status(emul, &first));
	zassert_ok(emul_pdc_pulse_irq(emul));
	wait_for_connector_change(dev, 1);
	zassert_equal(atomic_get(&ci_callback_count), 1);

	/* Queue a second change before the EC reads the connector status. */
	zassert_ok(emul_pdc_set_connector_status(emul, &combined));
	zassert_ok(emul_pdc_pulse_irq(emul));

	/* Verify that the first GET_CONNECTOR_STATUS reports both changes. */
	get_connector_status_after_ci(dev, &actual);
	wait_for_command_callback(dev, IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH,
				  false, false);
	zassert_mem_equal(actual.raw_value, combined.raw_value,
			  sizeof(actual.raw_value));

	/* Verify that reported changes do not generate a second empty CI. */
	events = k_event_wait_safe(&pdc_event, PDC_CONNECTOR_CHANGE, false,
				   K_MSEC(50));
	zassert_equal(events, 0);
	zassert_equal(atomic_get(&ci_callback_count), 1);
	zassert_ok(emul_pdc_idle_wait(emul));
}

ZTEST(it52xx, test_connector_change_after_get_reports_followup_ci)
{
	union connector_status_t first = { 0 };
	union connector_status_t second = { 0 };
	union connector_status_t actual = { 0 };

	/* Choose connector status values for the first and second changes. */
	first.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE;
	first.connect_status = 1;
	second.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECTOR_PARTNER;
	second.connect_status = 1;
	second.conn_partner_type = DFP_ATTACHED;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	atomic_set(&ci_callback_count, 0);
	atomic_set(&inject_connector_change, false);

	/* Report the first connector change and wait for its CI callback. */
	zassert_ok(emul_pdc_set_connector_status(emul, &first));
	zassert_ok(emul_pdc_pulse_irq(emul));
	wait_for_connector_change(dev, 1);
	zassert_equal(atomic_get(&ci_callback_count), 1);

	/*
	 * Inject the second change after the first GET_CONNECTOR_STATUS
	 * response, but before the driver sends ACK_CC_CI.
	 */
	inject_connector_change_after_get(emul, &second);
	get_connector_status_after_ci(dev, &actual);
	wait_for_command_callback(dev, IT52XX_GET_CONNECTOR_STATUS_DATA_LENGTH,
				  false, false);
	/*
	 * Verify that the second change was queued but not included in the
	 * first GET_CONNECTOR_STATUS.
	 */
	zassert_ok(injection_result);
	zassert_mem_equal(actual.raw_value, first.raw_value,
			  sizeof(actual.raw_value));

	/*
	 * Verify that the second change generates a follow-up CI after
	 * the first CI is acknowledged.
	 */
	wait_for_connector_change(dev, 1);
	zassert_equal(atomic_get(&ci_callback_count), 2);
	/*
	 * Verify that the follow-up GET_CONNECTOR_STATUS returns the second
	 * change.
	 */
	read_connector_status_after_ci(emul, dev, &second);
	/* Verify that no additional CI callback is generated. */
	zassert_equal(atomic_get(&ci_callback_count), 2);
}

ZTEST(it52xx, test_pending_ci_is_deferred_when_command_starts)
{
	union connector_status_t status = { 0 };
	union error_status_t error_status = { 0 };

	status.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE;
	status.connect_status = 1;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	atomic_set(&ci_callback_count, 0);
	atomic_set(&command_callback_seen, false);
	atomic_set(&ci_before_command_callback, false);
	atomic_set(&track_command_ci_order, true);

	/*
	 * Inject a connector change when the emulator receives the next CONTROL
	 * write. This reproduces a command and CI occurring at the same time.
	 */
	zassert_ok(emul_it52xx_inject_ci_on_next_control(emul, &status));

	/*
	 * Send GET_ERROR_STATUS. Its command-complete CCI must be returned
	 * first. The connector change remains pending until the command's
	 * internal ACK_CC_CI transaction completes.
	 */
	prepare_for_command();
	zassert_ok(pdc_get_error_status(dev, &error_status));
	wait_for_command_callback(dev, IT52XX_GET_ERROR_STATUS_DATA_LENGTH,
				  false, false);
	/* Verify that command callback is delivered before the pending CI. */
	zassert_false(atomic_get(&ci_before_command_callback));

	/* Verify that the deferred change generates exactly one CI callback. */
	wait_for_connector_change(dev, 1);
	zassert_equal(atomic_get(&ci_callback_count), 1);
	/* Verify that GET_CONNECTOR_STATUS returns the deferred change. */
	read_connector_status_after_ci(emul, dev, &status);

	atomic_set(&track_command_ci_order, false);
}

ZTEST(it52xx, test_ack_connector_change_interrupt)
{
	union connector_status_t status = { 0 };
	/* Set the connector-change-ack bit in ACK_CC_CI. */
	uint8_t command_specific[6] = { BIT(0) };

	status.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE;
	status.connect_status = 1;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	zassert_ok(emul_pdc_set_connector_status(emul, &status));
	/* Trigger a connector-change interrupt and wait for its CI callback. */
	zassert_ok(emul_pdc_pulse_irq(emul));
	wait_for_connector_change(dev, 1);

	/* Acknowledge the CI before reading GET_CONNECTOR_STATUS. */
	prepare_for_command();
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_ACK_CC_CI,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	wait_for_command(emul, dev, 0);

	/*
	 * ACK_CC_CI acknowledges the CI but does not clear connector-status
	 * change bits. Allow the driver to return to idle before reading them.
	 */
	k_msleep(1);
	/* Verify GET_CONNECTOR_STATUS still returns the stored change bits. */
	read_connector_status(emul, dev, &status);
}

static void check_i2c_error_recovery(int reg, bool read)
{
	struct i2c_common_emul_data *i2c_emul =
		emul_it52xx_get_i2c_common_data(emul);
	struct capability_t capability;
	bool recovery_completed = false;

	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_ok(i2c_common_emul_lock_data(i2c_emul, K_FOREVER));
	memset(&i2c_fault, 0, sizeof(i2c_fault));
	i2c_fault.reg = reg;
	i2c_fault.read = read;
	i2c_fault.original_write = i2c_emul->write_func;
	i2c_fault.original_write_data = i2c_emul->write_func_data;
	i2c_fault.original_finish_write = i2c_emul->finish_write;
	i2c_fault.original_start_read = i2c_emul->start_read;
	i2c_emul->write_func = record_i2c_write;
	i2c_emul->write_func_data = NULL;
	i2c_emul->finish_write = fail_completed_i2c_write;
	i2c_emul->start_read = fail_i2c_read_once;
	zassert_ok(i2c_common_emul_unlock_data(i2c_emul));

	/* Even a single failure must report an error, not replay the transfer.
	 */
	prepare_for_command();
	zassert_ok(pdc_get_capability(dev, &capability));
	wait_for_command_callback(dev, 0, true, false);

	/* The injected fault clears itself, allowing PPM_RESET to recover. */
	for (int i = 0; i < 200; ++i) {
		if (atomic_get(&i2c_fault.resets) > 0 &&
		    pdc_is_init_done(dev) && emul_it52xx_init_complete(emul)) {
			recovery_completed = true;
			break;
		}
		k_msleep(10);
	}
	restore_i2c_fault_hooks();
	zassert_true(recovery_completed);
	zassert_equal(atomic_get(&i2c_fault.injected), 1);
	zassert_equal(atomic_get(&i2c_fault.attempts), 1);
	if (reg == IT52XX_CMD_CLEAR_ALERT) {
		/* Do not reread MSGIN after an ambiguous CLEAR_ALERT failure.
		 */
		zassert_equal(atomic_get(&i2c_fault.msgin_reads), 1);
	}

	/* A command must complete normally after recovery. */
	prepare_for_command();
	zassert_ok(pdc_get_capability(dev, &capability));
	wait_for_command(emul, dev, IT52XX_GET_CAPABILITY_DATA_LENGTH);
}

ZTEST(it52xx, test_i2c_write_error_recovery)
{
	check_i2c_error_recovery(IT52XX_CMD_CONTROL, false);
}

ZTEST(it52xx, test_i2c_read_error_recovery)
{
	check_i2c_error_recovery(IT52XX_CMD_MSGIN, true);
}

ZTEST(it52xx, test_i2c_clear_alert_error_recovery)
{
	check_i2c_error_recovery(IT52XX_CMD_CLEAR_ALERT, false);
}

ZTEST(it52xx, test_init_error_disables_until_reset)
{
	struct i2c_common_emul_data *common =
		emul_it52xx_get_i2c_common_data(emul);
	union error_status_t error_status = { 0 };
	struct capability_t capability;
	bool finished = false;
	bool initialized;
	int error_rv = 0;

	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_ok(i2c_common_emul_lock_data(common, K_FOREVER));
	memset(&init_fault, 0, sizeof(init_fault));
	init_fault.original_write = common->write_func;
	init_fault.original_write_data = common->write_func_data;
	init_fault.installed = true;
	common->write_func = fail_first_init_info_request;
	common->write_func_data = NULL;
	zassert_ok(i2c_common_emul_unlock_data(common));

	/* Wait for this reset, not the completed initialization from setup. */
	zassert_ok(pdc_reset(dev));
	for (int i = 0; i < 200; ++i) {
		if (atomic_get(&init_fault.resets) > 0 &&
		    pdc_is_init_done(dev)) {
			finished = true;
			break;
		}
		k_msleep(10);
	}
	/* A second initialization attempt would succeed; none may start. */
	k_msleep(200);
	restore_init_fault_hook();
	zassert_ok(emul_pdc_idle_wait(emul));
	initialized = emul_it52xx_init_complete(emul);

	if (!initialized) {
		/* Remove the fault and explicitly reset the disabled port. */
		error_rv = pdc_get_error_status(dev, &error_status);
		zassert_ok(pdc_reset(dev));
	}
	check_initialization(emul, dev);

	/* Counts stop when the hook is removed, before the explicit reset. */
	zassert_true(finished);
	zassert_false(initialized);
	zassert_equal(atomic_get(&init_fault.resets), 1);
	zassert_equal(atomic_get(&init_fault.notifications), 1);
	zassert_equal(atomic_get(&init_fault.info_requests), 1);
	zassert_equal(atomic_get(&init_fault.error_requests), 1);
	zassert_ok(error_rv);
	zassert_true(error_status.port_disabled);
	zassert_false(error_status.i2c_read_error);
	zassert_false(error_status.i2c_write_error);

	prepare_for_command();
	zassert_ok(pdc_get_capability(dev, &capability));
	wait_for_command(emul, dev, IT52XX_GET_CAPABILITY_DATA_LENGTH);
}

ZTEST(it52xx, test_ppm_reset_with_irq_low_and_no_pending_event)
{
	union connector_status_t status = {
		.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE,
		.connect_status = 1,
	};
	bool reinitialization_started = false;
	bool reinitialization_completed = false;
	bool cleanup_completed = false;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	atomic_set(&ci_callback_count, 0);

	/*
	 * Disable the GPIO interrupt while the emulator asserts its active-low
	 * IRQ. Re-enable falling-edge detection while the line is already low,
	 * leaving no IRQ event cached by the driver.
	 */
	zassert_ok(
		gpio_pin_interrupt_configure_dt(&irq_gpio, GPIO_INT_DISABLE));
	zassert_ok(emul_pdc_set_connector_status(emul, &status));
	zassert_ok(emul_pdc_pulse_irq(emul));
	zassert_equal(gpio_pin_get_raw(irq_gpio.port, irq_gpio.pin), 0);
	zassert_ok(gpio_pin_interrupt_configure_dt(&irq_gpio,
						   GPIO_INT_EDGE_FALLING));
	k_msleep(50);
	zassert_equal(atomic_get(&ci_callback_count), 0);

	/* Start a PPM_RESET initialization without a pending IRQ event. */
	zassert_ok(pdc_reset(dev));
	for (int i = 0; i < 200; ++i) {
		if (!pdc_is_init_done(dev)) {
			reinitialization_started = true;
			break;
		}
		k_msleep(10);
	}
	zassert_true(reinitialization_started);

	for (int i = 0; i < 200; ++i) {
		if (pdc_is_init_done(dev) && emul_it52xx_init_complete(emul)) {
			reinitialization_completed = true;
			break;
		}
		k_msleep(10);
	}

	/*
	 * If no new falling edge was generated, create one after the check so
	 * the driver can finish initialization before the next test starts.
	 */
	if (!reinitialization_completed) {
		zassert_ok(gpio_emul_input_set_dt(&irq_gpio, 1));
		zassert_ok(gpio_emul_input_set_dt(&irq_gpio, 0));
		for (int i = 0; i < 200; ++i) {
			if (pdc_is_init_done(dev) &&
			    emul_it52xx_init_complete(emul)) {
				cleanup_completed = true;
				break;
			}
			k_msleep(10);
		}
		zassert_true(cleanup_completed);
	}

	zassert_true(reinitialization_completed,
		     "PPM_RESET did not generate an observable IRQ event");
}

ZTEST(it52xx, test_repeated_recovery_failures_disable_pdc)
{
	struct i2c_common_emul_data *i2c_emul =
		emul_it52xx_get_i2c_common_data(emul2);
	struct capability_t capability;
	bool recovery_started = false;
	bool pdc_disabled = false;

	/*
	 * Clear the emulator's initialization record so init_done becoming true
	 * without a completed emulator initialization identifies ST_DISABLE.
	 */
	zassert_ok(emul_pdc_reset(emul2));
	zassert_false(emul_it52xx_init_complete(emul2));

	/* Keep every I2C write failing across all recovery attempts. */
	i2c_common_emul_set_fail_auto_clear(i2c_emul, false);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);
	prepare_for_command();
	zassert_ok(pdc_get_capability(dev2, &capability));
	wait_for_command_callback(dev2, 0, true, false);

	/* Verify that the driver starts a recovery initialization. */
	for (int i = 0; i < 200; ++i) {
		if (!pdc_is_init_done(dev2)) {
			recovery_started = true;
			break;
		}
		k_msleep(10);
	}
	zassert_true(recovery_started);

	/*
	 * After all recovery attempts fail, ST_DISABLE sets init_done without
	 * completing the emulator initialization sequence.
	 */
	for (int i = 0; i < 500; ++i) {
		if (pdc_is_init_done(dev2) &&
		    !emul_it52xx_init_complete(emul2)) {
			pdc_disabled = true;
			break;
		}
		k_msleep(10);
	}
	zassert_true(pdc_disabled);

	/* Restoring I2C must not restart a PDC that entered ST_DISABLE. */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_fail_auto_clear(i2c_emul, true);
	k_msleep(200);
	zassert_true(pdc_is_init_done(dev2));
	zassert_false(emul_it52xx_init_complete(emul2));
}

ZTEST(it52xx, test_resume_with_pending_ci_and_irq_low)
{
	union connector_status_t status = {
		.raw_conn_status_change_bits = UCSI_CHANGE_BITS_CONNECT_CHANGE,
		.connect_status = 1,
	};
	struct capability_t capability;
	bool reinitialization_started = false;
	bool reinitialization_completed = false;

	zassert_ok(emul_pdc_idle_wait(emul));
	k_event_set(&pdc_event, 0);
	atomic_set(&ci_callback_count, 0);

	/* Suspend PDC communication before generating a connector change. */
	zassert_ok(pdc_set_comms_state(dev, false));
	zassert_ok(emul_pdc_set_connector_status(emul, &status));
	zassert_ok(emul_pdc_pulse_irq(emul));

	/*
	 * Let the driver observe the IRQ while suspended. The CI is not
	 * handled, so the active-low IRQ remains low when resume sends
	 * PPM_RESET.
	 */
	k_msleep(50);
	zassert_equal(atomic_get(&ci_callback_count), 0);

	zassert_ok(pdc_set_comms_state(dev, true));
	for (int i = 0; i < 200; ++i) {
		if (!pdc_is_init_done(dev)) {
			reinitialization_started = true;
			break;
		}
		k_msleep(10);
	}
	zassert_true(reinitialization_started);

	/* PPM_RESET must complete even though IRQ was already low. */
	for (int i = 0; i < 200; ++i) {
		if (pdc_is_init_done(dev) && emul_it52xx_init_complete(emul)) {
			reinitialization_completed = true;
			break;
		}
		k_msleep(10);
	}
	zassert_true(reinitialization_completed);
	zassert_equal(atomic_get(&ci_callback_count), 0);

	/* A command must complete normally after resume. */
	prepare_for_command();
	zassert_ok(pdc_get_capability(dev, &capability));
	wait_for_command(emul, dev, IT52XX_GET_CAPABILITY_DATA_LENGTH);
}

ZTEST(it52xx, test_comms_suspend_isolated_between_instances)
{
	struct capability_t capability;
	bool second_initialization_unchanged;
	int rv;

	zassert_ok(emul_pdc_idle_wait(emul));
	zassert_ok(emul_pdc_idle_wait(emul2));
	zassert_ok(pdc_set_comms_state(dev, false));

	/* Suspending the first device must not block the second device. */
	prepare_for_command();
	rv = pdc_get_capability(dev2, &capability);
	if (rv != 0) {
		/* Restore both devices before reporting an isolation failure.
		 */
		zassert_ok(pdc_set_comms_state(dev2, false));
		zassert_ok(emul_pdc_reset(emul));
		zassert_ok(emul_pdc_reset(emul2));
		zassert_ok(pdc_set_comms_state(dev, true));
		zassert_ok(pdc_set_comms_state(dev2, true));
		check_initialization(emul, dev);
		check_initialization(emul2, dev2);
		zassert_ok(rv,
			   "Suspending the first device blocked the second");
	}
	wait_for_command(emul2, dev2, IT52XX_GET_CAPABILITY_DATA_LENGTH);

	zassert_ok(pdc_set_comms_state(dev2, false));

	/* Clear the emulator records so we wait for fresh initialization,
	 * rather than observing the previous init_done value after resume.
	 * No pending connector-change IRQ is injected by this test.
	 */
	zassert_ok(emul_pdc_reset(emul));
	zassert_ok(emul_pdc_reset(emul2));
	zassert_ok(pdc_set_comms_state(dev, true));
	check_initialization(emul, dev);

	prepare_for_command();
	zassert_ok(pdc_get_capability(dev, &capability));
	wait_for_command(emul, dev, IT52XX_GET_CAPABILITY_DATA_LENGTH);

	/* Resuming the first device must leave the second device suspended. */
	second_initialization_unchanged = !emul_it52xx_init_complete(emul2);
	prepare_for_command();
	rv = pdc_get_capability(dev2, &capability);
	if (rv == 0) {
		wait_for_command(emul2, dev2,
				 IT52XX_GET_CAPABILITY_DATA_LENGTH);
	}

	/* Restore the second device before checking the recorded results. */
	zassert_ok(pdc_set_comms_state(dev2, true));
	check_initialization(emul2, dev2);
	zassert_equal(rv, -EBUSY,
		      "Resuming the first device resumed the second");
	zassert_true(second_initialization_unchanged,
		     "The second device initialized before its own resume");

	prepare_for_command();
	zassert_ok(pdc_get_capability(dev2, &capability));
	wait_for_command(emul2, dev2, IT52XX_GET_CAPABILITY_DATA_LENGTH);
}

ZTEST_SUITE(it52xx, NULL, it52xx_setup, NULL, it52xx_after, NULL);
