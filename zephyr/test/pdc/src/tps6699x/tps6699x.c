/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_tps6699x.h"
#include "pdc_trace_msg.h"
#include "tps6699x_cmd.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/minmax.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_tps6699x, LOG_LEVEL_DBG);
#define SLEEP_MS 200

/* Copy of driver retries for init. */
#define TPS6699X_INIT_RETRY_MAX 3

#define TPS6699X_NODE DT_NODELABEL(pdc_emul1)
#define TPS6699X_NODE2 DT_NODELABEL(pdc_emul2)

enum port_control_access {
	ACCESS_OK,
	ACCESS_READ_FAIL,
	ACCESS_WRITE_FAIL,
};

FAKE_VALUE_FUNC(int, tps_rw_port_control, const struct i2c_dt_spec *,
		union reg_port_control *, int);
test_mockable_static int tps_xfer_reg(const struct i2c_dt_spec *i2c,
				      enum tps6699x_reg reg, uint8_t *buf,
				      uint8_t len, int flag);

static const struct emul *emul = EMUL_DT_GET(TPS6699X_NODE);
static const struct emul *emul2 = EMUL_DT_GET(TPS6699X_NODE2);
static const struct device *dev = DEVICE_DT_GET(TPS6699X_NODE);
static const struct device *dev2 = DEVICE_DT_GET(TPS6699X_NODE2);
static enum port_control_access access;
static bool test_cc_cb_called;
static union cci_event_t test_cc_cb_cci;

static void test_cc_cb(const struct device *dev,
		       const struct pdc_callback *callback,
		       union cci_event_t cci_event)
{
	test_cc_cb_called = true;
	test_cc_cb_cci = cci_event;
}

static void tps6699x_before_test(void *data)
{
	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	zassert_ok(emul_pdc_idle_wait(emul));

	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;
}

static int custom_fake_tps_rw_port_control(const struct i2c_dt_spec *i2c,
					   union reg_port_control *buf,
					   int flag)
{
	if (access == ACCESS_OK) {
		return tps_xfer_reg(i2c, REG_PORT_CONTROL, buf->raw_value,
				    sizeof(union reg_port_control), flag);
	} else if (access == ACCESS_READ_FAIL && (flag & I2C_MSG_READ)) {
		return -EIO;
	} else if (access == ACCESS_WRITE_FAIL && !(flag & I2C_MSG_READ)) {
		return -EIO;
	}

	return 0;
}

ZTEST_SUITE(tps6699x, NULL, NULL, tps6699x_before_test, NULL, NULL);

/* Driver should keep returning cached connector status bits until they are
 * acked via ACK_CC_CI.
 */
ZTEST_USER(tps6699x, test_connector_status_caching)
{
	union connector_status_t in = { 0 }, out = { 0 };
	union conn_status_change_bits_t in_status_change_bits = { 0 },
					out_status_change_bits = { 0 };

	in_status_change_bits.raw_value = 0;
	out_status_change_bits.raw_value = 0;

	/* First check that connector status change bits are seen. */
	in_status_change_bits.connect_change = 1;
	in.raw_conn_status_change_bits = in_status_change_bits.raw_value;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));

	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_equal(out_status_change_bits.connect_change,
		      in_status_change_bits.connect_change);
	zassert_equal(out_status_change_bits.external_supply_change,
		      in_status_change_bits.external_supply_change);

	/* Now make sure that the change bits are cached until acked. */
	in_status_change_bits.connect_change = 0;
	in_status_change_bits.external_supply_change = 1;
	in.raw_conn_status_change_bits = in_status_change_bits.raw_value;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));
	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_not_equal(out_status_change_bits.connect_change,
			  in_status_change_bits.connect_change);
	zassert_equal(out_status_change_bits.external_supply_change,
		      in_status_change_bits.external_supply_change);

	/* Ack away the change bits and confirm they're zero'd. */
	in_status_change_bits.connect_change = 1;
	in_status_change_bits.external_supply_change = 1;

	zassert_ok(pdc_ack_cc_ci(dev, in_status_change_bits, /*cc=*/false,
				 /*vendor_defined=*/0));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));
	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_equal(out_status_change_bits.connect_change, 0);
	zassert_equal(out_status_change_bits.external_supply_change, 0);
}

ZTEST_USER(tps6699x, test_get_hw_config)
{
	struct pdc_hw_config_t config;
	struct i2c_dt_spec i2c_spec = I2C_DT_SPEC_GET(TPS6699X_NODE);

	zassert_not_ok(pdc_get_hw_config(dev, NULL));

	zassert_ok(pdc_get_hw_config(dev, &config));
	zassert_equal(config.bus_type, PDC_BUS_TYPE_I2C);
	zassert_equal(config.i2c.bus, i2c_spec.bus);
	zassert_equal(config.i2c.addr, i2c_spec.addr);
}

ZTEST_USER(tps6699x, test_set_uor_tps)
{
	union uor_t in, out;
	int swap_to_dfp;
	int swap_to_ufp;

	in.raw_value = 0;
	out.raw_value = 0;

	in.accept_dr_swap = 1;
	in.swap_to_ufp = 1;
	in.connector_number = 1;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Test that data role preference is correctly set to swap_to_ufp */
	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_uor(emul, &out));
	zassert_equal(out.swap_to_dfp, 0);
	zassert_equal(out.swap_to_ufp, 1);
	zassert_equal(out.accept_dr_swap, 1);
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 1);
	zassert_equal(swap_to_dfp, 0);

	/* Test that data role preference is correctly set to swap_to_dfp */
	in.swap_to_ufp = 0;
	in.swap_to_dfp = 1;
	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_uor(emul, &out));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);

	/* Exercise tps_rw_port_control read failure */
	in.swap_to_ufp = 1;
	in.swap_to_dfp = 0;
	access = ACCESS_READ_FAIL;
	pdc_set_uor(dev, in);
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);

	/* Exercise tps_rw_port_control write failure */
	access = ACCESS_WRITE_FAIL;
	pdc_set_uor(dev, in);
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);
}

#define INIT_SLEEP_MS 1000

/* Poll interval/timeout when waiting for the driver to enter ST_DISABLE. */
#define ST_DISABLE_POLL_MS 100
#define ST_DISABLE_POLL_TIMEOUT_US (5 * USEC_PER_SEC)

/*
 * Wait until the port under test enters ST_DISABLE.
 *
 * A port in ST_DISABLE reports its cached error status synchronously with
 * port_disabled set. In every other state pdc_get_error_status() either
 * fails with -EBUSY or returns an emulator-provided status with
 * port_disabled clear, making this a race-free indicator.
 */
static void wait_for_st_disable(void)
{
	union error_status_t es;

	zassert_true(WAIT_FOR((memset(&es, 0, sizeof(es)),
			       pdc_get_error_status(dev, &es) == 0 &&
				       es.port_disabled),
			      ST_DISABLE_POLL_TIMEOUT_US,
			      k_sleep(K_MSEC(ST_DISABLE_POLL_MS))),
		     "PDC did not enter ST_DISABLE");
}

/* Verify the externally visible behavior of a port in ST_DISABLE. */
static void verify_st_disable_behavior(void)
{
	union error_status_t es = { 0 };
	union connector_status_t cs;
	struct pdc_callback callback;

	/* Cached error status is returned with the port marked disabled. */
	zassert_ok(pdc_get_error_status(dev, &es));
	zassert_true(es.port_disabled);

	/* Commands fast-fail with -ENOSYS so the upper layer never blocks
	 * on a dead port.
	 */
	zassert_equal(-ENOSYS, pdc_set_rdo(dev, 0));

	/* A synthesized "not connected" status is returned and the CCI
	 * callback fires so the upper layer can move to PDC_UNATTACHED.
	 */
	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;

	memset(&cs, 0xff, sizeof(cs));
	zassert_ok(pdc_get_connector_status(dev, &cs));
	zassert_false(cs.connect_status);
	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_false(test_cc_cb_cci.error);

	pdc_set_cc_callback(dev, NULL);
}

/*
 * Restart init on all ports by pulsing a patch_loaded IRQ on the second
 * (healthy) emulator. Its driver's IRQ handling restarts init on all
 * ports, including a port stuck in ST_DISABLE.
 */
static void restart_init_all_ports(void)
{
	zassert_ok(emul_pdc_set_interrupt_patch_loaded(emul2));
	zassert_ok(emul_pdc_pulse_irq(emul2));
	k_sleep(K_MSEC(INIT_SLEEP_MS));
}

/*
 * A disabled port never leaves ST_DISABLE on its own. Restart init on all
 * ports and verify that both come back up.
 */
static void recover_from_st_disable(void)
{
	restart_init_all_ports();

	zassert_true(pdc_is_init_done(dev));
	zassert_true(pdc_is_init_done(dev2));
}

/* ST_INIT is being used to initialize critical registers. A port that
 * exhausts its SET_NOTIFICATION retries (or fails another init step)
 * transitions to ST_DISABLE until the next patch_loaded IRQ restarts init
 * on all ports. Test the retry, disable and recovery mechanisms.
 */
ZTEST_USER(tps6699x, test_init_state_sequence)
{
	/* Make sure we started in an initialized state. */
	zassert_true(pdc_is_init_done(dev));

	/* Fail all SET_NOTIFICATION attempts as part of init so that the
	 * retry limit is exceeded and the port enters ST_DISABLE.
	 */
	emul_pdc_fail_next_ucsi_command(emul, UCSI_SET_NOTIFICATION_ENABLE,
					TASK_REJECTED, TPS6699X_INIT_RETRY_MAX);

	/* Do a reset which will trigger GAID and restart init. This takes
	 * longer than normal to complete since GAID takes >1s.
	 */
	zassert_ok(pdc_reset(dev));
	k_sleep(K_MSEC(INIT_SLEEP_MS * 2));

	/* Init retries are exhausted: the port is now in ST_DISABLE, which
	 * still reports init done so the upper layer can proceed past it.
	 */
	wait_for_st_disable();
	verify_st_disable_behavior();

	/* A disabled port fast-fails further commands with -ENOSYS. */
	zassert_equal(-ENOSYS, pdc_reset(dev));

	/* Recover both ports by re-initializing all of them. */
	recover_from_st_disable();

	/* Fail register read/writes for the init tasks below. Each injected
	 * failure is one-shot and sends the port to ST_DISABLE; restart init
	 * to consume it, then recover before exercising the next one. (The
	 * REG_VERSION init failure is covered by
	 * test_st_disable_on_init_failure.)
	 */

	/* A REG_INTERRUPT_CLEAR_FOR_I2C1 write failure only logs an error,
	 * so init continues and fails on the interrupt mask write instead.
	 * Use persistent failures so init exhausts TPS6699X_INIT_RETRY_MAX.
	 */
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		REG_INTERRUPT_CLEAR_FOR_I2C1);
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		REG_INTERRUPT_MASK_FOR_I2C1);
	restart_init_all_ports();
	wait_for_st_disable();
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	recover_from_st_disable();

	/* Fail the REG_PORT_CONTROL write in pdc_port_control_init(). Route
	 * the port control access through to the emulator so the injected
	 * failure is seen by the driver. Use a persistent failure so init
	 * exhausts TPS6699X_INIT_RETRY_MAX.
	 */
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul), REG_PORT_CONTROL);
	restart_init_all_ports();
	wait_for_st_disable();
	i2c_common_emul_set_write_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	recover_from_st_disable();

	/* Fail the REG_BOOT_FLAG read in pdc_exit_dead_battery(). Use a
	 * persistent failure so init exhausts TPS6699X_INIT_RETRY_MAX.
	 */
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul), REG_BOOT_FLAG);
	restart_init_all_ports();
	wait_for_st_disable();
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	recover_from_st_disable();
}

/* Cover various branches of handle irq including failures. */
ZTEST_USER(tps6699x, test_handle_irq)
{
	zassert_true(pdc_is_init_done(dev));

	/* Set up some failures to read/write interrupt registers and make sure
	 * that the irq handling is eventually retried.
	 */
	emul_pdc_fail_reg_read(emul, REG_INTERRUPT_EVENT_FOR_I2C1);
	emul_pdc_fail_reg_write(emul, REG_INTERRUPT_CLEAR_FOR_I2C1);

	zassert_ok(emul_pdc_pulse_irq(emul));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Fail all SET_NOTIFICATION attempts as part of init. One failure will
	 * be due to attempting to read REG_VERSION. */
	emul_pdc_fail_next_ucsi_command(emul, UCSI_SET_NOTIFICATION_ENABLE,
					TASK_REJECTED, TPS6699X_INIT_RETRY_MAX);

	emul_pdc_set_interrupt_patch_loaded(emul);
	zassert_ok(emul_pdc_pulse_irq(emul));

	/* Init retries are exhausted: the port is now in ST_DISABLE. */
	wait_for_st_disable();

	/* Recover both ports to idle by re-initializing all of them. */
	recover_from_st_disable();
}

ZTEST_USER(tps6699x, test_set_rdo)
{
	uint32_t rdo;
	uint32_t cached_pdos;
	int max_voltage, max_current;
	union connector_status_t conn_status = { 0 };
	uint32_t pdos[PDO_MAX_OBJECTS] = { 0 };

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Set connector status to allow the PDC driver to set an RDO */
	conn_status.connect_status = 1;
	conn_status.power_direction = 0;
	emul_pdc_set_connector_status(emul, &conn_status);
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Test Fixed PDO selection */
	pdos[PDO_OFFSET_0] = PDO_FIXED(20000, 5000, 0);
	emul_pdc_set_pdos(emul, SOURCE_PDO, PDO_OFFSET_0, ARRAY_SIZE(pdos),
			  PARTNER_PDO, pdos);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Read back PDO for the driver to cache them */
	zassert_ok(pdc_get_pdos(dev, SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				&cached_pdos));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Set RDO with PDC driver */
	rdo = RDO_FIXED(1, CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
			CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA, 0);
	zassert_ok(pdc_set_rdo(dev, rdo));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify voltage and current limits from PDC emulator
	 * autoneg_sink max voltage should be PDO voltage / 50.
	 * autoneg_sink max current should be the min of PDO current and device
	 * current / 10.
	 */
	emul_pdc_get_autoneg_sink(emul, &max_voltage, &max_current);
	zassert_equal(max_voltage, 20000 / 50);
	zassert_equal(max_current,
		      min(CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA, 5000) / 10);

	/* Test Battery PDO selection */
	pdos[PDO_OFFSET_0] = PDO_BATT(5000, 20000, 45000);
	emul_pdc_set_pdos(emul, SOURCE_PDO, PDO_OFFSET_0, ARRAY_SIZE(pdos),
			  PARTNER_PDO, pdos);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Read back PDO for the driver to cache them */
	zassert_ok(pdc_get_pdos(dev, SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				&cached_pdos));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Set RDO with PDC driver */
	rdo = RDO_BATT(1, 45000, 45000, 0);
	zassert_ok(pdc_set_rdo(dev, rdo));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify voltage and current limits from PDC emulator
	 * autoneg_sink max voltage should be max PDO voltage / 50.
	 * autoneg_sink max current should be the device current / 10.
	 */
	emul_pdc_get_autoneg_sink(emul, &max_voltage, &max_current);
	zassert_equal(max_voltage, 20000 / 50);
	zassert_equal(max_current,
		      CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA / 10);
}

ZTEST_USER(tps6699x, test_set_bbr_cts)
{
	struct pdc_callback callback;

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	emul_pdc_fail_reg_write(emul, REG_THUNDERBOLT_CONFIGURATION);
	emul_pdc_fail_reg_write(emul, REG_COMMAND_FOR_I2C1);
	for (int i = 0; i < 2; i++) {
		test_cc_cb_cci.raw_value = 0;
		test_cc_cb_called = false;
		pdc_set_bbr_cts(dev, true);
		k_sleep(K_MSEC(SLEEP_MS));
		zassert_true(test_cc_cb_called);
		zassert_true(test_cc_cb_cci.command_completed);
		zassert_true(test_cc_cb_cci.error);
	}
	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(tps6699x, test_get_attention_vdo)
{
	union get_attention_vdo_t get_attention_vdo = { 0 };

	zassert_ok(pdc_get_attention_vdo(dev, &get_attention_vdo));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(get_attention_vdo.num_vdos, 2);
}

/* Cover the tps6699x driver returning discover identity with GET_PD_MESSAGE */
ZTEST_USER(tps6699x, test_get_pd_message_identity)
{
	struct ucsi_memory_region ucsi_data;
	struct ucsi_control_t *control = &ucsi_data.control;
	union get_pd_message_t get_pd_message_cmd = { 0 };
	uint32_t disc_in[PDC_DISC_IDENTITY_VDO_COUNT] = { 0x0, 0x2, 0x3, 0x4,
							  0x5, 0x6, 0x7 };
	uint32_t disc_out[PDC_DISC_IDENTITY_VDO_COUNT] = { 0 };
	struct pdc_callback callback;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Verify command_specific == NULL returns -EINVAL */
	zassert_equal(pdc_execute_ucsi_cmd(dev, UCSI_GET_PD_MESSAGE, 0, NULL,
					   ucsi_data.message_in, NULL),
		      -EINVAL);

	/* Verify unsupported response_message_type returns -ENOSYS */
	get_pd_message_cmd.connector_number = 0;
	get_pd_message_cmd.recipient = VDO_ORIGIN_SOP;
	get_pd_message_cmd.response_message_type = GET_PD_MESSAGE_REVISION;
	memcpy(&control->command_specific, &get_pd_message_cmd.raw_value,
	       sizeof(union get_pd_message_t));
	zassert_equal(pdc_execute_ucsi_cmd(dev, UCSI_GET_PD_MESSAGE,
					   sizeof(union get_pd_message_t),
					   control->command_specific,
					   ucsi_data.message_in, NULL),
		      -ENOSYS);

	/* Verify unsupported recipient returns -ENOSYS */
	get_pd_message_cmd.recipient = 0;
	get_pd_message_cmd.response_message_type = GET_PD_MESSAGE_DISC_ID;
	memcpy(&control->command_specific, &get_pd_message_cmd.raw_value,
	       sizeof(union get_pd_message_t));
	zassert_equal(pdc_execute_ucsi_cmd(dev, UCSI_GET_PD_MESSAGE,
					   sizeof(union get_pd_message_t),
					   control->command_specific,
					   ucsi_data.message_in, NULL),
		      -ENOSYS);

	/* Test case where response_type != 1 (identity not discovered) */
	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);

	get_pd_message_cmd.connector_number = 0;
	get_pd_message_cmd.recipient = VDO_ORIGIN_SOP;
	get_pd_message_cmd.response_message_type = GET_PD_MESSAGE_DISC_ID;
	memcpy(&control->command_specific, &get_pd_message_cmd.raw_value,
	       sizeof(union get_pd_message_t));

	test_cc_cb_cci.raw_value = 0;
	test_cc_cb_called = false;
	memset(ucsi_data.message_in, 0x5a, sizeof(ucsi_data.message_in));

	/* Send GET_PD_MESSAGE for SOP before identity is discovered */
	zassert_ok(pdc_execute_ucsi_cmd(
		dev, UCSI_GET_PD_MESSAGE, sizeof(union get_pd_message_t),
		control->command_specific, ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify error in CCI event */
	zassert_true(test_cc_cb_cci.error);

	memcpy(disc_out, &ucsi_data.message_in,
	       sizeof(uint32_t) * PDC_DISC_IDENTITY_VDO_COUNT);
	for (int i = 0; i < PDC_DISC_IDENTITY_VDO_COUNT; i++) {
		zassert_equal(0, disc_out[i]);
	}

	pdc_set_cc_callback(dev, NULL);

	/* Set fake Disc ID response in PDC emulator */
	emul_pdc_set_identity(emul, disc_in);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Send GET_PD_MESSAGE for SOP once identity is discovered */
	memset(ucsi_data.message_in, 0, sizeof(ucsi_data.message_in));
	zassert_ok(pdc_execute_ucsi_cmd(
		dev, UCSI_GET_PD_MESSAGE, sizeof(union get_pd_message_t),
		control->command_specific, ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify returned Discover Identity matches the emulator's Discover
	 * Identity */
	memcpy(disc_out, &ucsi_data.message_in,
	       sizeof(uint32_t) * PDC_DISC_IDENTITY_VDO_COUNT);
	for (int i = 0; i < PDC_DISC_IDENTITY_VDO_COUNT; i++) {
		zassert_equal(disc_in[i], disc_out[i]);
	}
}

/* Cover the tps6699x driver returning discover identity with GET_CURRENT_CAM */
ZTEST_USER(tps6699x, test_get_current_cam)
{
	struct ucsi_memory_region ucsi_data;
	struct ucsi_control_t *control = &ucsi_data.control;
	uint32_t current_cam = 0;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Set fake CAM in PDC emulator */
	emul_pdc_set_current_cam(emul, 0);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Send GET_CURRENT_CAM */
	zassert_ok(pdc_execute_ucsi_cmd(
		dev, UCSI_GET_CURRENT_CAM, sizeof(uint32_t),
		control->command_specific, ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify returned CAM matches emulator. */
	memcpy(&current_cam, &ucsi_data.message_in, sizeof(uint32_t));
	zassert_equal(current_cam, 0);

	/* Set fake CAM in PDC emulator */
	emul_pdc_set_current_cam(emul, 0x1);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Send GET_CURRENT_CAM */
	zassert_ok(pdc_execute_ucsi_cmd(
		dev, UCSI_GET_CURRENT_CAM, sizeof(uint32_t),
		control->command_specific, ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify returned CAM matches emulator. */
	memcpy(&current_cam, &ucsi_data.message_in, sizeof(uint32_t));
	zassert_equal(current_cam, 0x1);
}

/* Cover the tps6699x driver mapping SET_UOR from the PPM to CMD_SET_DRS */
ZTEST_USER(tps6699x, test_ppm_set_uor)
{
	union uor_t in, out;
	in.raw_value = 0;
	out.raw_value = 0;
	in.connector_number = 1;
	in.accept_dr_swap = 1;

	struct ucsi_memory_region ucsi_data;
	struct ucsi_control_t *control = &ucsi_data.control;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Send SET_UOR to UFP through pdc_execute_ucsi_cmd() */
	in.swap_to_ufp = 1;
	in.swap_to_dfp = 0;
	memcpy(&control->command_specific, &in.raw_value, sizeof(in.raw_value));
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_SET_UOR, sizeof(uint32_t),
					control->command_specific,
					ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify UOR has swap_to_ufp set. */
	zassert_ok(emul_pdc_get_uor(emul, &out));
	zassert_equal(out.swap_to_ufp, 1);
	zassert_equal(out.swap_to_dfp, 0);
	zassert_equal(out.accept_dr_swap, 1);

	in.swap_to_ufp = 0;
	in.swap_to_dfp = 1;
	memcpy(&control->command_specific, &in.raw_value, sizeof(in.raw_value));
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_SET_UOR, sizeof(uint32_t),
					control->command_specific,
					ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify UOR has swap_to_dfp set. */
	zassert_ok(emul_pdc_get_uor(emul, &out));
	zassert_equal(out.swap_to_ufp, 0);
	zassert_equal(out.swap_to_dfp, 1);
	zassert_equal(out.accept_dr_swap, 1);
}

ZTEST_USER(tps6699x, test_ap_mode_override_off)
{
	struct capability_t caps_in, caps_out;
	struct ucsi_memory_region ucsi_data;
	struct ucsi_control_t *control = &ucsi_data.control;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Set alt mode override to 1 in emulator */
	caps_in.bmOptionalFeatures.alt_mode_override = 1;
	emul_pdc_set_capability(emul, &caps_in);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Use UCSI command to get capabilities */
	zassert_ok(pdc_execute_ucsi_cmd(
		dev, UCSI_GET_CAPABILITY, sizeof(struct capability_t),
		control->command_specific, ucsi_data.message_in, NULL));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify alt mode override is cleared when AP mode entry is disabled */
	memcpy(&caps_out, &ucsi_data.message_in, sizeof(struct capability_t));
	if (IS_ENABLED(CONFIG_USBC_PDC_DISABLE_AP_MODE_ENTRY))
		zassert_false(caps_out.bmOptionalFeatures.alt_mode_override);
	else
		zassert_true(caps_out.bmOptionalFeatures.alt_mode_override);
}

ZTEST_USER(tps6699x, test_usb_comm_capable_as_device_config)
{
	struct pdc_info_t info;

	/* usb_comm_capable_as_device should be false as it is not supported */
	zassert_ok(pdc_get_info(dev, &info, true));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_false(info.usb_comm_capable_as_device);

	zassert_ok(pdc_get_info(dev2, &info, true));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_false(info.usb_comm_capable_as_device);
}

ZTEST_USER(tps6699x, test_set_battery_capability)
{
	struct pdc_callback callback;
	union battery_capability_t bc = { 0 };

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	emul_pdc_fail_reg_write(emul, REG_TX_BATTERY_CAPABILITIES);
	test_cc_cb_cci.raw_value = 0;
	test_cc_cb_called = false;
	pdc_set_battery_capability(dev, &bc);
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_true(test_cc_cb_cci.error);
	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(tps6699x, test_set_battery_status)
{
	struct pdc_callback callback;
	union battery_status_t bs = { 0 };

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	emul_pdc_fail_reg_write(emul,
				REG_TRANSMITTED_BATTERY_STATUS_DATA_OBJECT);
	test_cc_cb_cci.raw_value = 0;
	test_cc_cb_called = false;
	pdc_set_battery_status(dev, &bs);
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_true(test_cc_cb_cci.error);
}

ZTEST_USER(tps6699x, test_update_retimer_enable)
{
	struct pdc_callback callback;

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;

	zassert_ok(pdc_update_retimer_fw(dev, true));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_false(test_cc_cb_cci.error);

	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(tps6699x, test_update_retimer_disable)
{
	struct pdc_callback callback;

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);
	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;

	zassert_ok(pdc_update_retimer_fw(dev, false));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_false(test_cc_cb_cci.error);

	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(tps6699x, test_update_retimer_fail)
{
	struct pdc_callback callback;

	callback.handler = test_cc_cb;
	pdc_set_cc_callback(dev, &callback);

	emul_pdc_fail_reg_write(emul, REG_COMMAND_FOR_I2C1);
	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;

	zassert_ok(pdc_update_retimer_fw(dev, true));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);
	zassert_true(test_cc_cb_cci.error);

	pdc_set_cc_callback(dev, NULL);
}

ZTEST_USER(tps6699x, test_st_disable_on_init_failure)
{
	/* NULL argument checks. */
	zassert_equal(-EINVAL, pdc_get_connector_status(dev, NULL));
	zassert_equal(-EINVAL, pdc_get_error_status(dev, NULL));

	zassert_true(pdc_is_init_done(dev));

	/* Fail the next REG_VERSION read so the next init attempt fails in
	 * st_init_run() and the port transitions to ST_DISABLE. Use a
	 * persistent failure so init exhausts TPS6699X_INIT_RETRY_MAX.
	 */
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul), REG_VERSION);

	/* Restart init on all ports via the patch_loaded IRQ. */
	zassert_ok(emul_pdc_set_interrupt_patch_loaded(emul));
	zassert_ok(emul_pdc_pulse_irq(emul));

	wait_for_st_disable();
	verify_st_disable_behavior();
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	recover_from_st_disable();
}

/* Cover NULL / range / state validation paths and simple success paths for
 * API wrapper functions that are not exercised by other test cases.
 */
ZTEST_USER(tps6699x, test_api_validation)
{
	uint16_t ucsi_ver;
	union connector_status_t cs = { 0 };
	struct pdc_info_t info;
	struct pdc_hw_config_t hw_cfg;

	/* --- tps_read_power_level with power_direction == 0 --- */
	cs.power_direction = 0;
	zassert_ok(emul_pdc_set_connector_status(emul, &cs));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(-ENOSYS, pdc_read_power_level(dev));

	/* --- tps_get_info live=false cached read --- */
	zassert_ok(pdc_get_info(dev, &info, false));

	/* --- tps_get_info live=false with no cached value --- */
	/* Force a fresh init to clear the cached event by failing one init
	 * step, entering ST_DISABLE. In ST_DISABLE, PDC_CHIP_INFO_AVAIL_EVENT
	 * is still set (it is not in PDC_ALL_THREAD_WAKE_EVENTS), so
	 * live=false still returns cached info successfully. Use a persistent
	 * failure so init exhausts TPS6699X_INIT_RETRY_MAX.
	 */
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul), REG_VERSION);
	zassert_ok(emul_pdc_set_interrupt_patch_loaded(emul));
	zassert_ok(emul_pdc_pulse_irq(emul));
	wait_for_st_disable();
	zassert_ok(pdc_get_info(dev, &info, false));
	i2c_common_emul_set_read_fail_reg(
		emul_tps6699x_get_i2c_common_data(emul),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	recover_from_st_disable();

	/* --- tps_set_comms_state(false) suspend path --- */
	zassert_ok(pdc_set_comms_state(dev, false));
	/* Driver is now suspended. Verify suspend then resume. */
	zassert_ok(pdc_set_comms_state(dev, true));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_true(pdc_is_init_done(dev));

	/* --- Simple success paths for API wrappers --- */
	/* Set connector status to power_direction=1 for read_power_level. */
	cs.power_direction = 1;
	cs.connect_status = 1;
	zassert_ok(emul_pdc_set_connector_status(emul, &cs));
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Synchronous API wrappers */
	zassert_ok(pdc_get_ucsi_version(dev, &ucsi_ver));
	zassert_equal(ucsi_ver, UCSI_VERSION);
	zassert_ok(pdc_get_hw_config(dev, &hw_cfg));
	zassert_equal(hw_cfg.bus_type, PDC_BUS_TYPE_I2C);
}

/* Cover tps_ack_cc_ci and tps_set_battery_capability/status when not in
 * ST_IDLE (driver is suspended).
 */
ZTEST_USER(tps6699x, test_busy_state_rejections)
{
	union conn_status_change_bits_t ci = { 0 };
	union battery_capability_t bc = { 0 };
	union battery_status_t bs = { 0 };

	/* Suspend the driver so it's not in ST_IDLE */
	zassert_ok(pdc_set_comms_state(dev, false));

	/* These should all return -EBUSY because state != ST_IDLE */
	zassert_equal(-EBUSY, pdc_ack_cc_ci(dev, ci, false, 0));
	zassert_equal(-EBUSY, pdc_set_battery_capability(dev, &bc));
	zassert_equal(-EBUSY, pdc_set_battery_status(dev, &bs));

	/* Resume */
	zassert_ok(pdc_set_comms_state(dev, true));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_true(pdc_is_init_done(dev));
}

/* Cover tps_check_data_ready by setting a response delay so the driver
 * polls the command register while the emulator hasn't completed it yet.
 */
ZTEST_USER(tps6699x, test_delayed_response)
{
	union error_status_t es;

	/* Set a small response delay so the command isn't immediately complete
	 */
	emul_pdc_set_response_delay(emul, 50);

	zassert_ok(pdc_get_error_status(dev, &es));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Reset delay for subsequent tests */
	emul_pdc_set_response_delay(emul, 0);
	zassert_true(pdc_is_init_done(dev));
}
