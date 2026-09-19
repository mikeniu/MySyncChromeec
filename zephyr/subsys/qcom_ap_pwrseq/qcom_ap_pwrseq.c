/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_qcom_ap_pwrseq

#include "ap_power/ap_power_interface.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power_button.h"
#include "system.h"
#include "util.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_switchcap.h>

LOG_MODULE_REGISTER(qcom_ap_pwrseq, LOG_LEVEL_INF);

BUILD_ASSERT(
	DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	"Exactly one instance of cros-ec,qcom-ap-pwrseq should be defined.");

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_INST_PHANDLE(0, switchcap),
				cros_ec_switchcap),
	     "switchcap property must reference a cros-ec,switchcap device");

/* Timing constants from power/qcom.c */
#define DELAY_FORCE_SHUTDOWN_MS (8 * MSEC_PER_SEC)
#define PMIC_POWER_AP_RESPONSE_TIMEOUT_US (350 * USEC_PER_MSEC)
#define FORCE_OFF_RESPONSE_TIMEOUT_US (4 * USEC_PER_SEC)
#define PMIC_POWER_AP_WAIT_US (1 * USEC_PER_MSEC)
#define PMIC_RESIN_PULSE_LENGTH_US (20 * USEC_PER_MSEC)
#define WARM_RESET_CHECK_TIMEOUT_US (350 * USEC_PER_MSEC)
#define WARM_RESET_CHECK_WAIT_US (1 * USEC_PER_MSEC)
#define CAN_BOOT_AP_CHECK_TIMEOUT_US (1500 * USEC_PER_MSEC)
#define CAN_BOOT_AP_CHECK_WAIT_US (200 * USEC_PER_MSEC)
#define SWITCHCAP_PG_CHECK_TIMEOUT_US (100 * USEC_PER_MSEC)
#define SWITCHCAP_PG_CHECK_WAIT_US (6 * USEC_PER_MSEC)
#define SYSTEM_POWER_CHANGE_DELAY_US (300 * USEC_PER_MSEC)
#define PMIC_POWER_OFF_DELAY_US (150 * USEC_PER_MSEC)
#define EXPECTED_AP_RST_TRANSITIONS 3
#define AP_RST_TRANSITION_TIMEOUT_MS 450
#define HARD_RESET_POWER_GOOD_LOST_DELAY_MS 200

#define PWRSEQ_MSGQ_SIZE 16

enum ap_power_state {
	AP_POWER_STATE_G3, /* Hard off / G3 */
	AP_POWER_STATE_G3S0, /* Transitioning G3 -> S0 */
	AP_POWER_STATE_S0, /* Fully powered on / S0 */
	AP_POWER_STATE_S0S3, /* Transitioning S0 -> S3 */
	AP_POWER_STATE_S3, /* Suspend / S3 */
	AP_POWER_STATE_S3S0, /* Transitioning S3 -> S0 */
	AP_POWER_STATE_S0G3, /* Transitioning S0 -> G3 */
	AP_POWER_STATE_S3G3, /* Transitioning S3 -> G3 */
};

enum pwr_event {
	PWR_EVENT_POWER_ON,
	PWR_EVENT_FORCE_SHUTDOWN,
	PWR_EVENT_RESET,
	PWR_EVENT_POWER_GOOD_LOST,
	PWR_EVENT_POWER_BUTTON_PRESSED,
	PWR_EVENT_POWER_BUTTON_RELEASED,
	PWR_EVENT_LID_OPENED,
	PWR_EVENT_SUSPEND_ASSERTED,
	PWR_EVENT_SUSPEND_DEASSERTED,
};

struct pwr_msg {
	enum pwr_event event;
	enum chipset_shutdown_reason shutdown_reason;
};

struct qcom_ap_pwrseq_config {
	struct gpio_dt_spec ap_rst_gpio;
	struct gpio_dt_spec ap_suspend_gpio;
	struct gpio_dt_spec power_good_gpio;
	struct gpio_dt_spec pmic_kpd_pwr_gpio;
	struct gpio_dt_spec pmic_resin_gpio;
	const struct device *const switchcap_dev;
};

struct qcom_ap_pwrseq_data {
	struct gpio_callback power_good_cb;
	struct gpio_callback ap_suspend_cb;
	struct gpio_callback ap_rst_cb;
	struct k_work_delayable rst_debounce_work;
	struct k_work_delayable pg_lost_work;
	atomic_t ap_rst_transitions;
	struct k_msgq msgq;
	char msgq_buffer[PWRSEQ_MSGQ_SIZE * sizeof(struct pwr_msg)];
	enum ap_power_state ap_state;
};

static const struct qcom_ap_pwrseq_config qcom_cfg = {
	.ap_rst_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), ap_rst_gpios),
	.ap_suspend_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), ap_suspend_gpios),
	.power_good_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), power_good_gpios),
	.pmic_kpd_pwr_gpio =
		GPIO_DT_SPEC_GET(DT_DRV_INST(0), pmic_kpd_pwr_odl_gpios),
	.pmic_resin_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), pmic_resin_gpios),
	.switchcap_dev = DEVICE_DT_GET(DT_INST_PHANDLE(0, switchcap)),
};

static struct qcom_ap_pwrseq_data qcom_data;

/**
 * Check if the PMIC power-good line is high.
 */
static bool is_pmic_pwron(void)
{
	return gpio_pin_get_dt(&qcom_cfg.power_good_gpio) == 1;
}

/**
 * Poll until PMIC power-good matches the requested state or times out.
 */
static int wait_pmic_pwron(bool enable, unsigned int timeout_us)
{
	if (!WAIT_FOR(is_pmic_pwron() == enable, timeout_us,
		      k_usleep(PMIC_POWER_AP_WAIT_US))) {
		LOG_ERR("PMIC power state %d timeout", enable);
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/**
 * Poll until switchcap power-good matches expected state.
 */
static int wait_switchcap_power_good(bool enable)
{
	if (!WAIT_FOR(cros_switchcap_is_power_good(qcom_cfg.switchcap_dev) ==
			      enable,
		      SWITCHCAP_PG_CHECK_TIMEOUT_US,
		      k_usleep(SWITCHCAP_PG_CHECK_WAIT_US))) {
		LOG_ERR("Switchcap power-good timeout (enable=%d)", enable);
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/**
 * Turn on/off switchcap power rail and wait for power-good.
 */
static int set_switchcap_power(bool enable)
{
	int ret;

	LOG_INF("set_switchcap_power(%d)", enable);
	ret = cros_switchcap_set_power(qcom_cfg.switchcap_dev, enable);
	if (ret < 0) {
		LOG_ERR("Failed to set switchcap power: %d", ret);
		return ret;
	}

	ret = wait_switchcap_power_good(enable);
	if (ret != EC_SUCCESS) {
		return ret;
	}

	if (!enable) {
		return wait_pmic_pwron(false, FORCE_OFF_RESPONSE_TIMEOUT_US);
	}

	return EC_SUCCESS;
}

/**
 * Trigger PMIC power-on or power-off via PMIC lines.
 */
static int set_pmic_pwron(bool enable)
{
	int ret;

	LOG_INF("set_pmic_pwron(%d)", enable);

	if (enable == is_pmic_pwron())
		return EC_SUCCESS;

	if (gpio_pin_get_dt(&qcom_cfg.pmic_resin_gpio) == 0) {
		LOG_ERR("PMIC_RESIN_L not pulled up by PMIC; cancel pwron");
		return EC_ERROR_UNKNOWN;
	}

	/* Power-on: assert PMIC_KPD_PWR_ODL (drive low)
	 * Power-off: assert both PMIC_KPD_PWR_ODL and PMIC_RESIN_L (drive low)
	 */
	gpio_pin_set_dt(&qcom_cfg.pmic_kpd_pwr_gpio, 0);
	if (!enable)
		gpio_pin_set_dt(&qcom_cfg.pmic_resin_gpio, 0);

	ret = wait_pmic_pwron(enable, PMIC_POWER_AP_RESPONSE_TIMEOUT_US);

	/* Release open-drain lines back to high-Z */
	gpio_pin_set_dt(&qcom_cfg.pmic_kpd_pwr_gpio, 1);
	if (!enable)
		gpio_pin_set_dt(&qcom_cfg.pmic_resin_gpio, 1);

	return ret;
}

/**
 * Warm reset AP via PMIC_RESIN_L pulse.
 */
static int warm_reset_seq(void)
{
	LOG_INF("Initiating warm reset");

	/* Pulse PMIC_RESIN_L low for 20ms */
	gpio_pin_set_dt(&qcom_cfg.pmic_resin_gpio, 0);
	k_usleep(PMIC_RESIN_PULSE_LENGTH_US);
	gpio_pin_set_dt(&qcom_cfg.pmic_resin_gpio, 1);

	/* Wait for AP_RST_L to be asserted (line pulled low) */
	if (!WAIT_FOR(gpio_pin_get_dt(&qcom_cfg.ap_rst_gpio) == 0,
		      WARM_RESET_CHECK_TIMEOUT_US,
		      k_usleep(WARM_RESET_CHECK_WAIT_US))) {
		LOG_ERR("AP warm reset pulse not detected on AP_RST_L");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/**
 * Check if the power rails are sufficient to boot AP.
 */
static bool power_is_enough(void)
{
	WAIT_FOR(system_can_boot_ap() || charge_want_shutdown(),
		 CAN_BOOT_AP_CHECK_TIMEOUT_US,
		 k_usleep(CAN_BOOT_AP_CHECK_WAIT_US));

	return system_can_boot_ap() && !charge_want_shutdown();
}

/**
 * Post an event message to the power sequence queue.
 */
static void pwrseq_post_event_with_reason(enum pwr_event event,
					  enum chipset_shutdown_reason reason)
{
	struct pwr_msg msg = {
		.event = event,
		.shutdown_reason = reason,
	};

	k_msgq_put(&qcom_data.msgq, &msg, K_NO_WAIT);
}

static void pwrseq_post_event(enum pwr_event event)
{
	pwrseq_post_event_with_reason(event, 0);
}

/**
 * Transition from G3 (off) to S0 (on).
 */
static void transition_g3_to_s0(void)
{
	int ret;

	if (!power_is_enough()) {
		LOG_WRN("Power is not enough to boot AP");
		return;
	}

	LOG_INF("Powering on AP: G3 -> S0");
	qcom_data.ap_state = AP_POWER_STATE_G3S0;

	hook_notify(HOOK_CHIPSET_PRE_INIT);

	ret = set_switchcap_power(true);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Failed to enable switchcap: %d", ret);
		qcom_data.ap_state = AP_POWER_STATE_G3;
		return;
	}

	k_usleep(SYSTEM_POWER_CHANGE_DELAY_US);

	ret = set_pmic_pwron(true);
	if (ret != EC_SUCCESS) {
		LOG_ERR("POWER_GOOD not seen in time");
		set_switchcap_power(false);
		k_usleep(SYSTEM_POWER_CHANGE_DELAY_US);
		qcom_data.ap_state = AP_POWER_STATE_G3;
		return;
	}

	qcom_data.ap_state = AP_POWER_STATE_S0;
	disable_sleep(SLEEP_MASK_AP_RUN);
	hook_notify(HOOK_CHIPSET_STARTUP);
	hook_notify(HOOK_CHIPSET_RESUME);
	LOG_INF("AP powered on to S0 successfully");
}

/**
 * Transition from S0/S3 to G3 (off).
 */
static void transition_to_g3(bool power_good_lost,
			     enum chipset_shutdown_reason reason)
{
	enum ap_power_state prev_state = qcom_data.ap_state;

	LOG_INF("Powering off AP: state %d -> G3 (reason: %d)", prev_state,
		reason);

	if (prev_state == AP_POWER_STATE_S3) {
		qcom_data.ap_state = AP_POWER_STATE_S3G3;
	} else {
		qcom_data.ap_state = AP_POWER_STATE_S0G3;
	}

	/*
	 * Cancel any pending AP_RST_L debounce work and clear transition count
	 * so power-down rail drops or cold reboots do not trigger reset hooks.
	 */
	k_work_cancel_delayable(&qcom_data.rst_debounce_work);
	atomic_set(&qcom_data.ap_rst_transitions, 0);
	k_work_cancel_delayable(&qcom_data.pg_lost_work);

	if (prev_state != AP_POWER_STATE_S3) {
		hook_notify(HOOK_CHIPSET_SUSPEND);
	}

	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	if (is_pmic_pwron()) {
		if (!power_good_lost) {
			set_pmic_pwron(false);
			k_usleep(PMIC_POWER_OFF_DELAY_US);
		}
	}

	if (cros_switchcap_is_enabled(qcom_cfg.switchcap_dev)) {
		set_switchcap_power(false);
		k_usleep(SYSTEM_POWER_CHANGE_DELAY_US);
	}

	qcom_data.ap_state = AP_POWER_STATE_G3;
	enable_sleep(SLEEP_MASK_AP_RUN);
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
	LOG_INF("AP power off complete");
}

/**
 * Transition S0 -> S3 (suspend).
 */
static void transition_s0_to_s3(void)
{
	LOG_INF("AP entering S3 (suspend)");
	qcom_data.ap_state = AP_POWER_STATE_S0S3;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	enable_sleep(SLEEP_MASK_AP_RUN);
	qcom_data.ap_state = AP_POWER_STATE_S3;
}

/**
 * Transition S3 -> S0 (resume).
 */
static void transition_s3_to_s0(void)
{
	LOG_INF("AP resuming S3 -> S0");
	qcom_data.ap_state = AP_POWER_STATE_S3S0;
	disable_sleep(SLEEP_MASK_AP_RUN);
	hook_notify(HOOK_CHIPSET_RESUME);
	qcom_data.ap_state = AP_POWER_STATE_S0;
}

/**
 * Handle warm/cold reset requests.
 */
static void handle_reset_event(enum chipset_shutdown_reason reason)
{
	LOG_INF("Handling reset event (reason: %d)", reason);

	if (warm_reset_seq() != EC_SUCCESS) {
		LOG_WRN("Warm reset failed, doing cold reset");
		transition_to_g3(false, reason);
		transition_g3_to_s0();
		return;
	}
}

/**
 * Event state handling.
 */

static void handle_event_g3(struct pwr_msg msg, bool *power_btn_pressed_in_g3)
{
	switch (msg.event) {
	case PWR_EVENT_POWER_ON:
	case PWR_EVENT_LID_OPENED:
		*power_btn_pressed_in_g3 = false;
		transition_g3_to_s0();
		break;
	case PWR_EVENT_POWER_BUTTON_PRESSED:
		*power_btn_pressed_in_g3 = true;
		break;
	case PWR_EVENT_POWER_BUTTON_RELEASED:
		if (*power_btn_pressed_in_g3) {
			*power_btn_pressed_in_g3 = false;
			transition_g3_to_s0();
		}
		break;
	default:
		break;
	}
}

static void handle_event_s0(struct pwr_msg msg,
			    k_timepoint_t *power_button_deadline)
{
	switch (msg.event) {
	case PWR_EVENT_FORCE_SHUTDOWN:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		transition_to_g3(false, msg.shutdown_reason);
		break;
	case PWR_EVENT_POWER_GOOD_LOST:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		LOG_WRN("POWER_GOOD dropped in S0; shutting down");
		transition_to_g3(true, CHIPSET_SHUTDOWN_G3);
		break;
	case PWR_EVENT_RESET:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		handle_reset_event(msg.shutdown_reason);
		break;
	case PWR_EVENT_SUSPEND_ASSERTED:
		transition_s0_to_s3();
		break;
	case PWR_EVENT_POWER_BUTTON_PRESSED:
		if (K_TIMEOUT_EQ(sys_timepoint_timeout(*power_button_deadline),
				 K_FOREVER)) {
			*power_button_deadline = sys_timepoint_calc(
				K_MSEC(DELAY_FORCE_SHUTDOWN_MS));
		}
		break;
	case PWR_EVENT_POWER_BUTTON_RELEASED:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		break;
	default:
		break;
	}
}

static void handle_event_s3(struct pwr_msg msg,
			    k_timepoint_t *power_button_deadline)
{
	switch (msg.event) {
	case PWR_EVENT_SUSPEND_DEASSERTED:
	case PWR_EVENT_LID_OPENED:
		transition_s3_to_s0();
		break;
	case PWR_EVENT_POWER_BUTTON_PRESSED:
		if (K_TIMEOUT_EQ(sys_timepoint_timeout(*power_button_deadline),
				 K_FOREVER)) {
			*power_button_deadline = sys_timepoint_calc(
				K_MSEC(DELAY_FORCE_SHUTDOWN_MS));
		}
		transition_s3_to_s0();
		break;
	case PWR_EVENT_POWER_BUTTON_RELEASED:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		break;
	case PWR_EVENT_FORCE_SHUTDOWN:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		transition_to_g3(false, msg.shutdown_reason);
		break;
	case PWR_EVENT_POWER_GOOD_LOST:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		LOG_WRN("POWER_GOOD dropped in S3; shutting down");
		transition_to_g3(true, CHIPSET_SHUTDOWN_G3);
		break;
	case PWR_EVENT_RESET:
		*power_button_deadline = sys_timepoint_calc(K_FOREVER);
		handle_reset_event(msg.shutdown_reason);
		break;
	default:
		break;
	}
}

/**
 * Power sequence thread event processing loop.
 */
static void pwrseq_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct pwr_msg msg;
	k_timepoint_t power_button_deadline = sys_timepoint_calc(K_FOREVER);
	bool power_btn_pressed_in_g3 = false;
	int ret;

	LOG_INF("Qualcomm AP power sequence thread started");

	while (1) {
		ret = k_msgq_get(&qcom_data.msgq, &msg,
				 sys_timepoint_timeout(power_button_deadline));
		if (ret == -EAGAIN) {
			/* Timeout waiting on msgq: check power button hold */
			if (!K_TIMEOUT_EQ(sys_timepoint_timeout(
						  power_button_deadline),
					  K_FOREVER) &&
			    power_button_is_pressed()) {
				LOG_WRN("Power button held for 8s; forcing shutdown");
				report_ap_reset(CHIPSET_SHUTDOWN_BUTTON);
				transition_to_g3(false,
						 CHIPSET_SHUTDOWN_BUTTON);
			}
			power_button_deadline = sys_timepoint_calc(K_FOREVER);
			continue;
		}

		switch (qcom_data.ap_state) {
		case AP_POWER_STATE_G3:
			handle_event_g3(msg, &power_btn_pressed_in_g3);
			break;

		case AP_POWER_STATE_S0:
			handle_event_s0(msg, &power_button_deadline);
			break;

		case AP_POWER_STATE_S3:
			handle_event_s3(msg, &power_button_deadline);
			break;

		default:
			break;
		}
	}
}

K_THREAD_DEFINE(qcom_pwrseq_tid, CONFIG_QCOM_AP_PWRSEQ_STACK_SIZE,
		pwrseq_thread_fn, NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_QCOM_AP_PWRSEQ_THREAD_PRIORITY), 0,
		SYS_FOREVER_MS);

/* Chipset API implementations */

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	LOG_INF("chipset_force_shutdown(%d)", reason);
	report_ap_reset(reason);
	pwrseq_post_event_with_reason(PWR_EVENT_FORCE_SHUTDOWN, reason);
}

void chipset_power_on(void)
{
	pwrseq_post_event(PWR_EVENT_POWER_ON);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	LOG_INF("chipset_reset(%d)", reason);
	report_ap_reset(reason);
	pwrseq_post_event_with_reason(PWR_EVENT_RESET, reason);
}

int chipset_in_state(int state_mask)
{
	switch (qcom_data.ap_state) {
	case AP_POWER_STATE_G3:
		return (state_mask &
			(CHIPSET_STATE_HARD_OFF | CHIPSET_STATE_SOFT_OFF));
	case AP_POWER_STATE_S0:
		return (state_mask & CHIPSET_STATE_ON);
	case AP_POWER_STATE_S3:
		return (state_mask & CHIPSET_STATE_SUSPEND);
	default:
		return 0;
	}
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	if (chipset_in_state(state_mask)) {
		return 1;
	}

	switch (qcom_data.ap_state) {
	case AP_POWER_STATE_G3S0:
	case AP_POWER_STATE_S3S0:
		return !!(state_mask & CHIPSET_STATE_ON);
	case AP_POWER_STATE_S0G3:
	case AP_POWER_STATE_S3G3:
		return !!(state_mask &
			  (CHIPSET_STATE_HARD_OFF | CHIPSET_STATE_SOFT_OFF));
	case AP_POWER_STATE_S0S3:
		return !!(state_mask & CHIPSET_STATE_SUSPEND);
	default:
		return 0;
	}
}

static void qcom_power_button_changed(void)
{
	if (power_button_is_pressed()) {
		pwrseq_post_event(PWR_EVENT_POWER_BUTTON_PRESSED);
	} else {
		pwrseq_post_event(PWR_EVENT_POWER_BUTTON_RELEASED);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, qcom_power_button_changed,
	     HOOK_PRIO_DEFAULT);

static void lid_state_change(void)
{
	if (lid_is_open()) {
		pwrseq_post_event(PWR_EVENT_LID_OPENED);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_state_change, HOOK_PRIO_DEFAULT);

test_mockable void ap_pwrseq_task_start(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	bool auto_power_on = true;

	if (reset_flags & EC_RESET_FLAG_AP_OFF) {
		auto_power_on = false;
	} else if (!(reset_flags & EC_RESET_FLAG_EFS) &&
		   (reset_flags & EC_RESET_FLAG_SYSJUMP)) {
		auto_power_on = false;
	}

	if (auto_power_on && !chipset_in_state(CHIPSET_STATE_ON)) {
		LOG_INF("Auto power-on requested by reset flags");
		chipset_power_on();
	}

	k_thread_start(qcom_pwrseq_tid);
}

k_tid_t get_ap_pwrseq_thread(void)
{
	return qcom_pwrseq_tid;
}

/**
 * Handler for POWER_GOOD loss debounce. When the AP performs a PSCI hard reset,
 * the PMIC drops POWER_GOOD (S4C rail) for ~70-175ms before bringing it back
 * up. Waiting 200ms ensures the EC distinguishes between a transient hard reset
 * and a permanent PSCI shutdown to G3.
 */
static void power_good_lost_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* If POWER_GOOD is still low after 200ms, initiate shutdown */
	if (!is_pmic_pwron() &&
	    chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		LOG_WRN("POWER_GOOD dropped for > %dms; shutting down",
			HARD_RESET_POWER_GOOD_LOST_DELAY_MS);
		pwrseq_post_event(PWR_EVENT_POWER_GOOD_LOST);
	}
}

static void power_good_interrupt(const struct device *port,
				 struct gpio_callback *cb,
				 gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (!chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		return;
	}

	if (!is_pmic_pwron()) {
		/* POWER_GOOD dropped: schedule 200ms debounce for hard reset
		 * recovery */
		k_work_schedule(&qcom_data.pg_lost_work,
				K_MSEC(HARD_RESET_POWER_GOOD_LOST_DELAY_MS));
	} else {
		/*
		 * If POWER_GOOD dropped and recovered before the 200ms timeout,
		 * it indicates a transient drop during a PSCI hard reset rather
		 * than a shutdown. Cancel the pending shutdown work if it was
		 * running.
		 */
		k_work_cancel_delayable(&qcom_data.pg_lost_work);
	}
}

static void ap_suspend_interrupt(const struct device *port,
				 struct gpio_callback *cb,
				 gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (!chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		return;
	}

	if (gpio_pin_get_dt(&qcom_cfg.ap_suspend_gpio) == 1) {
		pwrseq_post_event(PWR_EVENT_SUSPEND_ASSERTED);
	} else {
		pwrseq_post_event(PWR_EVENT_SUSPEND_DEASSERTED);
	}
}

/**
 * Handler for AP abnormal/autonomous reset detection (e.g. SoC watchdog or
 * kernel crash). When the AP resets unexpectedly, hardware pulses AP_RST_L
 * three times. This debounce handler fires HOOK_CHIPSET_RESET once all pulses
 * arrive or the 450ms timeout expires, and wakes the system if reset occurred
 * during S3 suspend.
 */
static void ap_rst_debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int count = atomic_set(&qcom_data.ap_rst_transitions, 0);

	if (count == 0 ||
	    !chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		return;
	}

	if (count != EXPECTED_AP_RST_TRANSITIONS) {
		LOG_WRN("AP_RST_L transitions not expected: %d", count);
	}

	hook_notify(HOOK_CHIPSET_RESET);

	/* If AP resets while in S3, exit suspend and resume to S0 */
	if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
		LOG_INF("Chipset reset: exit S3");
		pwrseq_post_event(PWR_EVENT_SUSPEND_DEASSERTED);
	}
}

static void ap_rst_interrupt(const struct device *port,
			     struct gpio_callback *cb, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/*
	 * Track falling edges (active-low reset assertion) when AP is in S0 or
	 * S3.
	 */
	if (gpio_pin_get_dt(&qcom_cfg.ap_rst_gpio) == 0 &&
	    chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND)) {
		int count = atomic_inc(&qcom_data.ap_rst_transitions) + 1;
		int delay_ms;

		if (count >= EXPECTED_AP_RST_TRANSITIONS) {
			delay_ms = 0;
		} else {
			delay_ms = AP_RST_TRANSITION_TIMEOUT_MS;
		}
		k_work_reschedule(&qcom_data.rst_debounce_work,
				  K_MSEC(delay_ms));
	}
}

#define LOG_ERR_GPIO(name, suffix, ret) \
	LOG_ERR("Failed to configure %s%s: %d", name, suffix, ret)
#define LOG_ERR_GPIO_CFG(name, ret) LOG_ERR_GPIO(name, "", ret)
#define LOG_ERR_GPIO_CB(name, ret) LOG_ERR_GPIO(name, " callback", ret)
#define LOG_ERR_GPIO_INT(name, ret) LOG_ERR_GPIO(name, " interrupt", ret)

static int check_gpio_ready(const struct gpio_dt_spec *gpio)
{
	if (!gpio_is_ready_dt(gpio)) {
		LOG_ERR_DEVICE_NOT_READY(gpio->port);
		return -ENODEV;
	}
	return 0;
}

static int gpio_readiness_check(void)
{
	if (check_gpio_ready(&qcom_cfg.ap_rst_gpio) ||
	    check_gpio_ready(&qcom_cfg.ap_suspend_gpio) ||
	    check_gpio_ready(&qcom_cfg.power_good_gpio) ||
	    check_gpio_ready(&qcom_cfg.pmic_kpd_pwr_gpio) ||
	    check_gpio_ready(&qcom_cfg.pmic_resin_gpio)) {
		return -ENODEV;
	}
	return 0;
}

/**
 * Initialize Qualcomm AP power sequence driver.
 */
static int qcom_ap_pwrseq_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	int ret;

	LOG_INF("Initializing Qualcomm AP power sequence driver");

	if (gpio_readiness_check()) {
		return -ENODEV;
	}

	if (!device_is_ready(qcom_cfg.switchcap_dev)) {
		LOG_ERR_DEVICE_NOT_READY(qcom_cfg.switchcap_dev);
		return -ENODEV;
	}

	/* Initialize reset debounce work */
	k_work_init_delayable(&qcom_data.rst_debounce_work,
			      ap_rst_debounce_handler);
	atomic_set(&qcom_data.ap_rst_transitions, 0);

	/* Initialize power-good lost debounce work */
	k_work_init_delayable(&qcom_data.pg_lost_work, power_good_lost_handler);

	/* Configure inputs */
	ret = gpio_pin_configure_dt(&qcom_cfg.ap_rst_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR_GPIO_CFG("AP_RST", ret);
		return ret;
	}
	ret = gpio_pin_configure_dt(&qcom_cfg.ap_suspend_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR_GPIO_CFG("AP_SUSPEND", ret);
		return ret;
	}
	ret = gpio_pin_configure_dt(&qcom_cfg.power_good_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR_GPIO_CFG("POWER_GOOD", ret);
		return ret;
	}

	/* Configure open-drain outputs to initial released (high-Z) state */
	ret = gpio_pin_configure_dt(&qcom_cfg.pmic_kpd_pwr_gpio,
				    GPIO_OUTPUT_HIGH);
	if (ret < 0) {
		LOG_ERR_GPIO_CFG("PMIC_KPD_PWR", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&qcom_cfg.pmic_resin_gpio,
				    GPIO_OUTPUT_HIGH);
	if (ret < 0) {
		LOG_ERR_GPIO_CFG("PMIC_RESIN", ret);
		return ret;
	}

	/* Configure interrupt on POWER_GOOD both edges */
	gpio_init_callback(&qcom_data.power_good_cb, power_good_interrupt,
			   BIT(qcom_cfg.power_good_gpio.pin));
	ret = gpio_add_callback(qcom_cfg.power_good_gpio.port,
				&qcom_data.power_good_cb);
	if (ret < 0) {
		LOG_ERR_GPIO_CB("POWER_GOOD", ret);
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&qcom_cfg.power_good_gpio,
					      GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR_GPIO_INT("POWER_GOOD", ret);
		return ret;
	}

	/* Configure interrupt on AP_SUSPEND both edges */
	gpio_init_callback(&qcom_data.ap_suspend_cb, ap_suspend_interrupt,
			   BIT(qcom_cfg.ap_suspend_gpio.pin));
	ret = gpio_add_callback(qcom_cfg.ap_suspend_gpio.port,
				&qcom_data.ap_suspend_cb);
	if (ret < 0) {
		LOG_ERR_GPIO_CB("AP_SUSPEND", ret);
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&qcom_cfg.ap_suspend_gpio,
					      GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR_GPIO_INT("AP_SUSPEND", ret);
		return ret;
	}

	/* Configure interrupt on AP_RST falling edge (reset asserted) */
	gpio_init_callback(&qcom_data.ap_rst_cb, ap_rst_interrupt,
			   BIT(qcom_cfg.ap_rst_gpio.pin));
	ret = gpio_add_callback(qcom_cfg.ap_rst_gpio.port,
				&qcom_data.ap_rst_cb);
	if (ret < 0) {
		LOG_ERR_GPIO_CB("AP_RST", ret);
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&qcom_cfg.ap_rst_gpio,
					      GPIO_INT_EDGE_FALLING);
	if (ret < 0) {
		LOG_ERR_GPIO_INT("AP_RST", ret);
		return ret;
	}

	/* Initialize initial power state based on hardware */
	uint32_t reset_flags = system_get_reset_flags();

	if (!(reset_flags & EC_RESET_FLAG_SYSJUMP)) {
		LOG_INF("Cold boot (non-sysjump); ensuring power rails off");
		set_switchcap_power(false);
		qcom_data.ap_state = AP_POWER_STATE_G3;
		enable_sleep(SLEEP_MASK_AP_RUN);
	} else if (is_pmic_pwron()) {
		if (gpio_pin_get_dt(&qcom_cfg.ap_suspend_gpio) == 1) {
			qcom_data.ap_state = AP_POWER_STATE_S3;
			enable_sleep(SLEEP_MASK_AP_RUN);
		} else {
			qcom_data.ap_state = AP_POWER_STATE_S0;
			disable_sleep(SLEEP_MASK_AP_RUN);
		}
	} else {
		qcom_data.ap_state = AP_POWER_STATE_G3;
		enable_sleep(SLEEP_MASK_AP_RUN);
	}

	/* Initialize message queue */
	k_msgq_init(&qcom_data.msgq, qcom_data.msgq_buffer,
		    sizeof(struct pwr_msg), PWRSEQ_MSGQ_SIZE);

	LOG_INF("Qualcomm AP power sequence driver ready (initial state: %s)",
		qcom_data.ap_state == AP_POWER_STATE_S0 ?
			"S0" :
			(qcom_data.ap_state == AP_POWER_STATE_S3 ? "S3" :
								   "G3"));
	return 0;
}

DEVICE_DT_INST_DEFINE(0, qcom_ap_pwrseq_init, NULL, &qcom_data, &qcom_cfg,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
