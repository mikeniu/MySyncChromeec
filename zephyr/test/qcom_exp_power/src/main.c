/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "console.h"
#include "ec_app_main.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power/qcom.h"
#include "power_button.h"
#include "system.h"
#include "task.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(qcom_exp_power, LOG_LEVEL_INF);

#define GPIO_DEVICE \
	DEVICE_DT_GET(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(ap_rst_l), gpios))

#define PIN_AP_RST_L DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_rst_l), gpios)
#define PIN_PS_HOLD DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ps_hold), gpios)
#define PIN_POWER_GOOD DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(power_good), gpios)
#define PIN_AP_SUSPEND DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ap_suspend), gpios)
#define PIN_WARM_RESET_L DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(warm_reset_l), gpios)
#define PIN_PMIC_KPD_PWR DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pmic_kpd_pwr), gpios)
#define PIN_PMIC_RESIN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(pmic_resin), gpios)
#define PIN_LID_OPEN DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(lid_open_ec), gpios)
#define PIN_PWR_BTN_L DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(ec_pwr_btn_odl), gpios)
#define PIN_AC_PRESENT DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(acok_od), gpios)

#define HEARTBEAT_WAKE_INTERVAL_SEC (45 * 60)
#define EXTPOWER_WAKE_INTERVAL_SEC 30

DEFINE_FFF_GLOBALS;

/* Forward declarations of functions in qcom_exp.c */
void chipset_sys_rst_interrupt(enum gpio_signal signal);
void board_chipset_set_heartbeat_alarm_on_shutdown(void);
void board_chipset_clear_heartbeat_alarm_on_poweron(void);
void rtc_callback(const struct device *dev);
void notify_ac_irq_re_enable_and_check(void);

/* Mock / stub state variables */
static int mock_switchcap_enabled = 1;
static int mock_switchcap_pg = 1;
static int mock_switchcap_reset = 1;
static int mock_passthru_acok;
static int mock_passthru_lid;
static enum hibernate_wake_source mock_hib_wake_source = WAKE_SOURCE_ACOK;
static int mock_hib_wake_source_ret;
static uint32_t mock_rtc_alarm_seconds;
static uint32_t mock_rtc_alarm_useconds;
static int chipset_reset_count;
static bool set_power_good_on_reset;

void board_set_switchcap_power(int enable)
{
	mock_switchcap_enabled = enable;
}

int board_is_switchcap_enabled(void)
{
	return mock_switchcap_enabled;
}

int board_is_switchcap_power_good(void)
{
	return mock_switchcap_pg;
}

int board_is_switchcap_power_reset(void)
{
	return mock_switchcap_reset;
}

void passthru_lid_open_to_pmic(void)
{
	mock_passthru_lid = 1;
}

void passthru_ac_on_to_pmic(void)
{
	mock_passthru_acok = 1;
}

void reset_all_passthru_pmic_signal(void)
{
	mock_passthru_acok = 0;
	mock_passthru_lid = 0;
}

int system_get_hibernate_wake_source(enum hibernate_wake_source *wake_source)
{
	if (wake_source)
		*wake_source = mock_hib_wake_source;
	return mock_hib_wake_source_ret;
}

void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	mock_rtc_alarm_seconds = seconds;
	mock_rtc_alarm_useconds = microseconds;
}

static void do_chipset_reset(void)
{
	chipset_reset_count++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, do_chipset_reset, HOOK_PRIO_DEFAULT);

static void do_chipset_shutdown(void)
{
	if (set_power_good_on_reset) {
		const struct device *gpio_dev = GPIO_DEVICE;

		gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, do_chipset_shutdown, HOOK_PRIO_DEFAULT);

static struct gpio_callback gpio_callback_power;
static struct gpio_callback gpio_callback_resin;

static void set_power_good(struct k_work *work)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 1);
}
K_WORK_DEFINE(set_power_good_work, set_power_good);

static void power_good_callback(const struct device *gpio_dev,
				struct gpio_callback *callback_struct,
				gpio_port_pins_t pins)
{
	if ((pins & BIT(PIN_PMIC_KPD_PWR)) == 0)
		return;
	if (gpio_emul_output_get(gpio_dev, PIN_PMIC_KPD_PWR))
		k_work_submit(&set_power_good_work);
}

static void warm_reset_callback(const struct device *gpio_dev,
				struct gpio_callback *callback_struct,
				gpio_port_pins_t pins)
{
	if ((pins & BIT(PIN_PMIC_RESIN)) == 0)
		return;
	if (gpio_emul_output_get(gpio_dev, PIN_PMIC_RESIN))
		gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 0);
	else
		gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 1);
}

/* Fixture setup: put system into steady S0 state */
static void start_in_s0(void *fixture)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	mock_switchcap_enabled = 1;
	mock_switchcap_pg = 1;
	mock_switchcap_reset = 1;
	mock_passthru_acok = 0;
	mock_passthru_lid = 0;
	mock_hib_wake_source = WAKE_SOURCE_ACOK;
	mock_hib_wake_source_ret = 0;
	mock_rtc_alarm_seconds = 0;
	mock_rtc_alarm_useconds = 0;
	chipset_reset_count = 0;
	set_power_good_on_reset = false;

	/* Setup power good callback so AP power-on always succeeds */
	gpio_init_callback(&gpio_callback_power, power_good_callback,
			   BIT(PIN_PMIC_KPD_PWR));
	gpio_add_callback(gpio_dev, &gpio_callback_power);
	gpio_pin_interrupt_configure(gpio_dev, PIN_PMIC_KPD_PWR,
				     GPIO_INT_EDGE_BOTH);

	power_signal_disable_interrupt(GPIO_AP_SUSPEND);
	power_signal_enable_interrupt(GPIO_AP_RST_L);
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 1);
	gpio_emul_input_set(gpio_dev, PIN_PS_HOLD, 1);
	gpio_emul_input_set(gpio_dev, PIN_AP_SUSPEND, 0);
	gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 1);
	gpio_emul_input_set(gpio_dev, PIN_WARM_RESET_L, 1);
	gpio_emul_input_set(gpio_dev, PIN_LID_OPEN, 1);
	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 1);
	gpio_emul_input_set(gpio_dev, PIN_AC_PRESENT, 0);

	power_set_state(POWER_S0);
	power_signal_interrupt(GPIO_AP_SUSPEND);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0,
		      "Failed to initialize to S0: %d", power_get_state());
}

static void *qcom_setup(void)
{
	ec_app_main();
	/* Sleep to allow initial startup sequencing to complete */
	k_sleep(K_SECONDS(11));

	return NULL;
}

static void qcom_cleanup(void *fixture)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	if (gpio_callback_power.handler != NULL) {
		gpio_remove_callback(gpio_dev, &gpio_callback_power);
		gpio_callback_power.handler = NULL;
	}
	if (gpio_callback_resin.handler != NULL) {
		gpio_remove_callback(gpio_dev, &gpio_callback_resin);
		gpio_callback_resin.handler = NULL;
	}
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC));
	system_clear_reset_flags(0xFFFFFFFF);
}

ZTEST_SUITE(qcom_exp_power, NULL, qcom_setup, start_in_s0, qcom_cleanup, NULL);

/* Test power_chipset_init under various reset flags */
ZTEST(qcom_exp_power, test_power_chipset_init)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	/* 1. Normal boot (not sysjump) -> G3 */
	mock_switchcap_reset = 1;
	system_clear_reset_flags(0xFFFFFFFF);
	zassert_equal(power_chipset_init(), POWER_G3);

	/* 2. Sysjump with AP on (POWER_GOOD = 1) -> S0 */
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 1);
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP | EC_RESET_FLAG_AP_OFF);
	zassert_equal(power_chipset_init(), POWER_S0);

	/* 3. Sysjump with AP off (POWER_GOOD = 0) -> G3 */
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 0);
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);
	zassert_equal(power_chipset_init(), POWER_G3);

	/* 4. Hibernate wake with ACOK */
	system_clear_reset_flags(0xFFFFFFFF);
	system_set_reset_flags(EC_RESET_FLAG_HIBERNATE);
	mock_hib_wake_source = WAKE_SOURCE_ACOK;
	mock_hib_wake_source_ret = 0;
	zassert_equal(power_chipset_init(), POWER_G3);

	/* 5. Check power_button_is_eating_release */
#ifdef CONFIG_POWER_BUTTON
	zassert_equal(power_button_is_eating_release(), 1);
	zassert_equal(power_button_is_eating_release(), 0);
#endif
}

/* Test forced shutdown to G3 */
ZTEST(qcom_exp_power, test_chipset_force_shutdown)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Test power on via chipset_power_on() */
ZTEST(qcom_exp_power, test_chipset_power_on)
{
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_G3);

	chipset_power_on();
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
}

/* Test power on via lid open and lid event handling */
ZTEST(qcom_exp_power, test_lid_open_power_on)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_emul_input_set(gpio_dev, PIN_LID_OPEN, 0);
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_G3);

	/* 1. Lid open triggers power-on from G3 */
	gpio_emul_input_set(gpio_dev, PIN_LID_OPEN, 1);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0, "power_state=%d",
		      power_get_state());
	zassert_equal(chipset_get_power_on_reason(), POWER_ON_BY_LID_OPEN);

	/* 2. Lid events in S0 verify PMIC passthrough */
	mock_passthru_lid = 0;
	gpio_emul_input_set(gpio_dev, PIN_LID_OPEN, 0);
	hook_notify(HOOK_LID_CHANGE);
	zassert_equal(mock_passthru_lid, 1);

	gpio_emul_input_set(gpio_dev, PIN_LID_OPEN, 1);
	hook_notify(HOOK_LID_CHANGE);
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test power on via power button */
ZTEST(qcom_exp_power, test_power_button_power_on)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	power_set_state(POWER_G3);
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 0);
	k_sleep(K_MSEC(10));
	zassert_equal(power_get_state(), POWER_G3);

	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(power_button_signal_asserted(), 1);
	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 1);
	k_sleep(K_MSEC(500));
	zassert_equal(power_button_signal_asserted(), 0);
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test offmode charging via AC connection */
ZTEST(qcom_exp_power, test_offmode_charging)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	power_set_state(POWER_G3);
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(power_get_state(), POWER_G3);

	gpio_emul_input_set(gpio_dev, PIN_AC_PRESENT, 1);
	hook_notify(HOOK_AC_CHANGE);
	k_sleep(K_MSEC(500));

	/* In offmode charging, it stays in S5 */
	zassert_equal(power_get_state(), POWER_S5,
		      "Offmode charging should gate in S5");
	zassert_equal(chipset_is_offmode_charging_wake(), 1);
	zassert_equal(chipset_get_power_on_reason(), POWER_ON_BY_AC_ON);
}

/* Test power button long press shutdown */
ZTEST(qcom_exp_power, test_power_button_off)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 0);
	k_sleep(K_SECONDS(9));
	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 1);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Test power button short press in S0 does not power off */
ZTEST(qcom_exp_power, test_power_button_off_cancel)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 0);
	k_sleep(K_SECONDS(2));
	gpio_emul_input_set(gpio_dev, PIN_PWR_BTN_L, 1);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test POWER_GOOD loss in S0 */
ZTEST(qcom_exp_power, test_no_power_good)
{
	const char *buffer;
	size_t buffer_size;
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_remove_callback(gpio_dev, &gpio_callback_power);

	zassert_equal(power_get_state(), POWER_S0);
	shell_backend_dummy_clear_output(get_ec_shell());
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 0);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "POWER_GOOD is lost") != NULL,
		     "Invalid console output %s", buffer);
}

/* Test POWER_GOOD lost then reappearing */
ZTEST(qcom_exp_power, test_no_power_good_then_good)
{
	const char *buffer;
	size_t buffer_size;
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_remove_callback(gpio_dev, &gpio_callback_power);

	shell_backend_dummy_clear_output(get_ec_shell());
	gpio_emul_input_set(gpio_dev, PIN_POWER_GOOD, 0);
	set_power_good_on_reset = true;
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(11));
	zassert_equal(power_get_state(), POWER_G3, "power_state=%d",
		      power_get_state());
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer, "POWER_GOOD is lost") != NULL,
		     "Invalid console output %s", buffer);
	zassert_true(strstr(buffer, "POWER_GOOD up again after lost") != NULL,
		     "Invalid console output %s", buffer);
}

/* Test host sleep S3 suspend and resume */
ZTEST(qcom_exp_power, test_request_sleep_and_resume)
{
	const struct device *gpio_dev = GPIO_DEVICE;
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};

	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	gpio_emul_input_set(gpio_dev, PIN_AP_SUSPEND, 1);
	k_sleep(K_SECONDS(16));
	zassert_equal(power_get_state(), POWER_S3);
	zassert_false(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));

	/* Resume from S3 */
	params.sleep_event = HOST_SLEEP_EVENT_S3_RESUME;
	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	gpio_emul_input_set(gpio_dev, PIN_AP_SUSPEND, 0);
	power_signal_interrupt(GPIO_AP_SUSPEND);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test host sleep S3 suspend hang */
ZTEST(qcom_exp_power, test_request_sleep_hang)
{
	struct ec_params_host_sleep_event params = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};

	zassert_ok(ec_cmd_host_sleep_event(NULL, &params));
	k_sleep(K_SECONDS(16));
	zassert_equal(power_get_state(), POWER_S0);

	/* Explicit call to sleep hang handler */
	power_chipset_handle_sleep_hang(SLEEP_HANG_S0IX_SUSPEND);
	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

/* Test AP_RST_L interrupt in S0 */
ZTEST(qcom_exp_power, test_ap_rst_interrupt_s0)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	chipset_reset_count = 0;
	/* Pulse AP_RST_L 3 times */
	for (int i = 0; i < 3; i++) {
		gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 0);
		chipset_ap_rst_interrupt(GPIO_AP_RST_L);
		gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 1);
		chipset_ap_rst_interrupt(GPIO_AP_RST_L);
	}
	k_sleep(K_MSEC(500));
	zassert_equal(chipset_reset_count, 1);
}

/* Test AP_RST_L interrupt in S3 */
ZTEST(qcom_exp_power, test_ap_rst_interrupt_s3)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	power_signal_enable_interrupt(GPIO_AP_SUSPEND);
	gpio_emul_input_set(gpio_dev, PIN_AP_SUSPEND, 1);
	power_set_state(POWER_S3);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S3);

	chipset_reset_count = 0;
	/* Pulse AP_RST_L once to trigger deferred timeout */
	gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 0);
	chipset_ap_rst_interrupt(GPIO_AP_RST_L);
	gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 1);
	chipset_ap_rst_interrupt(GPIO_AP_RST_L);

	k_sleep(K_MSEC(500));
	zassert_equal(chipset_reset_count, 1);
}

/* Test warm reset success */
ZTEST(qcom_exp_power, test_chipset_reset_success)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	gpio_init_callback(&gpio_callback_resin, warm_reset_callback,
			   BIT(PIN_PMIC_RESIN));
	zassert_ok(gpio_add_callback(gpio_dev, &gpio_callback_resin));
	zassert_ok(gpio_pin_interrupt_configure(gpio_dev, PIN_PMIC_RESIN,
						GPIO_INT_EDGE_BOTH));

	chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
	k_sleep(K_MSEC(100));
	gpio_emul_input_set(gpio_dev, PIN_AP_RST_L, 1);
	k_sleep(K_MSEC(500));
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test warm reset timeout fallback to cold reset */
ZTEST(qcom_exp_power, test_chipset_reset_timeout)
{
	const char *buffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(get_ec_shell());
	chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
	k_sleep(K_SECONDS(10));

	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strstr(buffer,
			    "AP refuses to warm reset. Cold resetting") != NULL,
		     "Invalid console output %s", buffer);
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test SYS_RST interrupt for short and long pulses */
ZTEST(qcom_exp_power, test_sys_rst_interrupt)
{
	const struct device *gpio_dev = GPIO_DEVICE;

	/* 1. Short pulse: cancel timer and request warm reset */
	gpio_emul_input_set(gpio_dev, PIN_WARM_RESET_L, 0);
	chipset_sys_rst_interrupt(GPIO_WARM_RESET_L);
	gpio_emul_input_set(gpio_dev, PIN_WARM_RESET_L, 1);
	chipset_sys_rst_interrupt(GPIO_WARM_RESET_L);
	k_sleep(K_SECONDS(10));
	zassert_equal(power_get_state(), POWER_S0);

	/* 2. Long pulse: let timer expire for long warm reset */
	gpio_emul_input_set(gpio_dev, PIN_WARM_RESET_L, 0);
	chipset_sys_rst_interrupt(GPIO_WARM_RESET_L);
	k_sleep(K_SECONDS(10));
	gpio_emul_input_set(gpio_dev, PIN_WARM_RESET_L, 1);
	chipset_sys_rst_interrupt(GPIO_WARM_RESET_L);
	k_sleep(K_SECONDS(2));
	zassert_equal(power_get_state(), POWER_S0);
}

/* Test heartbeat alarm hooks and RTC callback */
ZTEST(qcom_exp_power, test_heartbeat_mode_and_rtc)
{
	const struct device *gpio_dev = GPIO_DEVICE;
	struct host_cmd_handler_args args;

	/* 1. Clear heartbeat alarm on power-on */
	mock_rtc_alarm_seconds = 100;
	board_chipset_clear_heartbeat_alarm_on_poweron();
	zassert_equal(mock_rtc_alarm_seconds, EC_RTC_ALARM_CLEAR);

	/* 2. Shutdown heartbeat alarm hook with no AC and heartbeat_mode = 0 */
	gpio_emul_input_set(gpio_dev, PIN_AC_PRESENT, 0);
	extpower_interrupt(GPIO_AC_PRESENT);
	k_sleep(K_MSEC(50));
	mock_rtc_alarm_seconds = 0xFFFF;
	board_chipset_set_heartbeat_alarm_on_shutdown();
	zassert_equal(mock_rtc_alarm_seconds, 0xFFFF);

	/* 3. Shutdown hook with AC present sets 30s wake alarm */
	gpio_emul_input_set(gpio_dev, PIN_AC_PRESENT, 1);
	extpower_interrupt(GPIO_AC_PRESENT);
	k_sleep(K_MSEC(50));
	zassert_equal(extpower_is_present(), 1);
	board_chipset_set_heartbeat_alarm_on_shutdown();
	zassert_equal(mock_rtc_alarm_seconds, EXTPOWER_WAKE_INTERVAL_SEC);

	/* 4. Enable offmode heartbeat via host command and verify 45min alarm
	 */
	memset(&args, 0, sizeof(args));
	args.version = 0;
	args.command = EC_CMD_ENABLE_OFFMODE_HEARTBEAT;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	board_chipset_set_heartbeat_alarm_on_shutdown();
	zassert_equal(mock_rtc_alarm_seconds, HEARTBEAT_WAKE_INTERVAL_SEC);

	/* 5. Trigger RTC callback and verify EC_HOST_EVENT_RTC is set */
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC));
	zassert_false(host_is_event_set(EC_HOST_EVENT_RTC));
	rtc_callback(NULL);
	k_sleep(K_MSEC(10));
	zassert_true(host_is_event_set(EC_HOST_EVENT_RTC));

	/* 6. Trigger AC IRQ re-enable deferred hook and verify AC state */
	notify_ac_irq_re_enable_and_check();
	zassert_equal(extpower_is_present(), 1);
}

/* Test console commands and host commands */
ZTEST(qcom_exp_power, test_host_and_console_commands)
{
	/* Power console command via EC shell */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "power"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "power on"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "power off"));
	zassert_not_equal(shell_execute_cmd(get_ec_shell(), "power invalid"),
			  0);

	/* Restore S0 after power off command */
	chipset_power_on();
	k_sleep(K_SECONDS(1));

	/* Scheduled AP reset host command with immediate reset */
	struct ec_params_ap_reset_scheduled p_imm = { .delay_ms = 0 };
	struct host_cmd_handler_args args;
	memset(&args, 0, sizeof(args));
	args.version = 0;
	args.command = EC_CMD_AP_RESET_SCHEDULED;
	args.params = &p_imm;
	args.params_size = sizeof(p_imm);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	k_sleep(K_SECONDS(2));

	/* Scheduled AP reset host command with delayed reset */
	struct ec_params_ap_reset_scheduled p_del = { .delay_ms = 100 };
	memset(&args, 0, sizeof(args));
	args.version = 0;
	args.command = EC_CMD_AP_RESET_SCHEDULED;
	args.params = &p_del;
	args.params_size = sizeof(p_del);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	k_sleep(K_SECONDS(2));

	/* Offmode heartbeat host command */
	memset(&args, 0, sizeof(args));
	args.version = 0;
	args.command = EC_CMD_ENABLE_OFFMODE_HEARTBEAT;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Restore S0 state after testing power commands */
	chipset_power_on();
	k_sleep(K_SECONDS(1));
}
