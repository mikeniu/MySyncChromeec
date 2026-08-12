/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

#define STUB_SENSOR_NODE DT_NODELABEL(stub_pwrmon_sensor)

static const struct gpio_dt_spec pwrmon_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(pwrmon), gpios);

struct mock_channel_data {
	struct sensor_value power;
};

struct mock_sensor_data {
	int attr_set_fail_count;
	int sample_fetch_fail_count;
	int channel_get_fail_count;
	uint16_t last_sample_rate;
	uint32_t sample_count;
	uint8_t read_toggle;
	struct mock_channel_data channels[2];
};

static struct mock_sensor_data mock_sensor;

static void mock_sensor_reset(void)
{
	memset(&mock_sensor, 0, sizeof(mock_sensor));
	mock_sensor.sample_count = 100;
	/* Channel 0: 100 W (100000000 uW) */
	mock_sensor.channels[0].power.val1 = 100;
	mock_sensor.channels[0].power.val2 = 0;
	/* Channel 1: 50 W (50000000 uW) */
	mock_sensor.channels[1].power.val1 = 50;
	mock_sensor.channels[1].power.val2 = 0;
}

static int mock_sensor_attr_set(const struct device *dev,
				enum sensor_channel chan,
				enum sensor_attribute attr,
				const struct sensor_value *val)
{
	if (mock_sensor.attr_set_fail_count > 0) {
		mock_sensor.attr_set_fail_count--;
		return -EIO;
	}

	if (attr == SENSOR_ATTR_SAMPLING_FREQUENCY) {
		mock_sensor.last_sample_rate = val->val1;
	}
	return 0;
}

static int mock_sensor_sample_fetch(const struct device *dev,
				    enum sensor_channel chan)
{
	if (mock_sensor.sample_fetch_fail_count > 0) {
		mock_sensor.sample_fetch_fail_count--;
		return -EIO;
	}
	return 0;
}

static int mock_sensor_channel_get(const struct device *dev,
				   enum sensor_channel chan,
				   struct sensor_value *val)
{
	if (mock_sensor.channel_get_fail_count > 0) {
		mock_sensor.channel_get_fail_count--;
		return -EIO;
	}

	if (chan == SENSOR_CHAN_FREQUENCY) {
		val->val1 = mock_sensor.sample_count;
		val->val2 = 0;
		return 0;
	}

	int ch_idx = 0;
	if (chan > SENSOR_CHAN_POWER) {
		ch_idx = chan - SENSOR_CHAN_POWER;
	} else {
		ch_idx = mock_sensor.read_toggle %
			 ARRAY_SIZE(mock_sensor.channels);
		mock_sensor.read_toggle++;
	}

	if (ch_idx >= ARRAY_SIZE(mock_sensor.channels)) {
		ch_idx = 0;
	}
	*val = mock_sensor.channels[ch_idx].power;
	return 0;
}

static DEVICE_API(sensor, mock_sensor_api) = {
	.attr_set = mock_sensor_attr_set,
	.sample_fetch = mock_sensor_sample_fetch,
	.channel_get = mock_sensor_channel_get,
};

static int mock_sensor_init(const struct device *dev)
{
	return 0;
}

DEVICE_DT_DEFINE(STUB_SENSOR_NODE, mock_sensor_init, NULL, NULL, NULL,
		 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		 &mock_sensor_api);

static enum ec_status run_pwrmon_cmd(struct ec_params_pwrmon *p,
				     struct ec_response_pwrmon *r)
{
	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_PWRMON,
		.version = 0,
		.params = p,
		.params_size = p ? sizeof(*p) : 0,
		.response = r,
		.response_max = r ? sizeof(*r) : 0,
	};

	return host_command_process(&args);
}

static enum ec_status
run_get_next_event(struct ec_response_get_next_event_v3 *resp)
{
	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_GET_NEXT_EVENT,
		.version = 3,
		.params = NULL,
		.params_size = 0,
		.response = resp,
		.response_max = sizeof(*resp),
	};

	return host_command_process(&args);
}

static void pwrmon_before(void *fixture)
{
	mock_sensor_reset();

	/* Ensure pwrmon monitoring is running before each test case */
	struct ec_params_pwrmon req = { .cmd = EC_PWRMON_START };
	struct ec_response_pwrmon resp;

	run_pwrmon_cmd(&req, &resp);

	/* Drain any leftover MKBP events */
	struct ec_response_get_next_event_v3 mkbp_resp;
	while (run_get_next_event(&mkbp_resp) == EC_RES_SUCCESS) {
	}
}

ZTEST_SUITE(pwrmon, NULL, NULL, pwrmon_before, NULL, NULL);

ZTEST(pwrmon, test_invalid_command)
{
	struct ec_params_pwrmon req = { .cmd = 0xFF };
	struct ec_response_pwrmon resp = { 0 };

	zassert_equal(EC_RES_INVALID_COMMAND, run_pwrmon_cmd(&req, &resp));
}

ZTEST(pwrmon, test_get_channel_count)
{
	struct ec_params_pwrmon req = { .cmd = EC_PWRMON_GET_CHANNEL_COUNT };
	struct ec_response_pwrmon resp = { 0 };

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req, &resp));
	zassert_equal(2, resp.channel_count, "Expected 2 channels from DTS");
}

ZTEST(pwrmon, test_dump_info)
{
	struct ec_params_pwrmon req = {
		.cmd = EC_PWRMON_DUMP_INFO,
		.channel_id = 0,
	};
	struct ec_response_pwrmon resp = { 0 };

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req, &resp));
	zassert_equal(0, resp.dump_info.channel_id);
	zassert_str_equal("VDD_CORE", resp.dump_info.channel_name);

	req.channel_id = 1;
	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req, &resp));
	zassert_equal(1, resp.dump_info.channel_id);
	zassert_str_equal("1", resp.dump_info.channel_name);

	req.channel_id = 2;
	zassert_equal(EC_RES_ERROR, run_pwrmon_cmd(&req, &resp));
}

ZTEST(pwrmon, test_get_rate)
{
	struct ec_params_pwrmon req = { .cmd = EC_PWRMON_GET_RATE };
	struct ec_response_pwrmon resp = { 0 };

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req, &resp));
	zassert_equal(1000, resp.sample_rate);
}

ZTEST(pwrmon, test_set_rate)
{
	struct ec_params_pwrmon req_get = { .cmd = EC_PWRMON_GET_RATE };
	struct ec_params_pwrmon req_set = { .cmd = EC_PWRMON_SET_RATE,
					    .set_rate = 500 };
	struct ec_response_pwrmon resp = { 0 };

	mock_sensor.attr_set_fail_count = 1;
	zassert_equal(EC_RES_ERROR, run_pwrmon_cmd(&req_set, &resp));

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_set, &resp));
	zassert_equal(500, mock_sensor.last_sample_rate);

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_get, &resp));
	zassert_equal(500, resp.sample_rate);
}

ZTEST(pwrmon, test_gpio_start_stop)
{
	struct ec_params_pwrmon req_stop = { .cmd = EC_PWRMON_STOP };
	struct ec_params_pwrmon req_start = { .cmd = EC_PWRMON_START };
	struct ec_params_pwrmon req_latch = { .cmd = EC_PWRMON_LATCH };
	struct ec_response_pwrmon resp = { 0 };

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_stop, &resp));
	zassert_equal(0, gpio_emul_output_get_dt(&pwrmon_gpio));

	zassert_equal(EC_RES_ERROR, run_pwrmon_cmd(&req_latch, &resp));

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_start, &resp));
	zassert_equal(1, gpio_emul_output_get_dt(&pwrmon_gpio));
	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_latch, &resp));
}

ZTEST(pwrmon, test_mkbp_telemetry)
{
	struct ec_params_pwrmon req_latch = { .cmd = EC_PWRMON_LATCH };
	struct ec_response_pwrmon resp = { 0 };
	struct ec_response_get_next_event_v3 event = { 0 };
	enum ec_status status;

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_latch, &resp));

	k_sleep(K_MSEC(100));

	status = run_get_next_event(&event);
	zassert_equal(EC_RES_SUCCESS, status,
		      "First event failed with status %d", status);
	zassert_equal(EC_MKBP_EVENT_PWRMON,
		      event.event_type & EC_MKBP_EVENT_TYPE_MASK);
	zassert_equal(0, event.data.pwrmon_data.channel_id);
	zassert_equal(100, event.data.pwrmon_data.samples);
	zassert_equal(100000000LL, event.data.pwrmon_data.value);

	memset(&event, 0, sizeof(event));
	status = run_get_next_event(&event);
	zassert_equal(EC_RES_SUCCESS, status,
		      "Second event failed with status %d", status);
	zassert_equal(EC_MKBP_EVENT_PWRMON,
		      event.event_type & EC_MKBP_EVENT_TYPE_MASK);
	zassert_equal(1, event.data.pwrmon_data.channel_id);
	zassert_equal(100, event.data.pwrmon_data.samples);
	zassert_equal(50000000LL, event.data.pwrmon_data.value);

	status = run_get_next_event(&event);
	zassert_equal(EC_RES_UNAVAILABLE, status,
		      "Expected queue empty (UNAVAILABLE), got %d", status);
}

ZTEST(pwrmon, test_stop_purges_msgq)
{
	struct ec_params_pwrmon req_latch = { .cmd = EC_PWRMON_LATCH };
	struct ec_params_pwrmon req_stop = { .cmd = EC_PWRMON_STOP };
	struct ec_response_pwrmon resp = { 0 };
	struct ec_response_get_next_event_v3 event = { 0 };
	enum ec_status status;

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_latch, &resp));
	k_sleep(K_MSEC(100));

	zassert_equal(EC_RES_SUCCESS, run_pwrmon_cmd(&req_stop, &resp));

	/* If the MKBP interrupt raised before STOP is read now, verify that
	 * its payload contains no data (0 samples, 0 value) because msgq was
	 * purged.
	 */
	status = run_get_next_event(&event);
	if (status == EC_RES_SUCCESS) {
		zassert_equal(0, event.data.pwrmon_data.samples,
			      "Expected 0 samples after purge");
		zassert_equal(0, event.data.pwrmon_data.value,
			      "Expected 0 power value after purge");
		status = run_get_next_event(&event);
	}
	zassert_equal(EC_RES_UNAVAILABLE, status,
		      "Expected EC_RES_UNAVAILABLE, got %d", status);
}
