/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_interface.h"
#include "ap_power_host_sleep.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "emul/emul_power_signals.h"
#include "host_command.h"
#include "lpc.h"
#include "power_signals.h"
#include "system.h"
#include "test_mocks.h"
#include "test_state.h"
#include "zephyr/sys/util.h"

#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>
#include <ap_power_override_functions.h>

LOG_MODULE_REGISTER(test_ap_pwrseq);

static int power_resume_count;
static int power_start_up_count;
static int power_hard_off_count;
static int power_shutdown_count;
static int power_shutdown_complete_count;
static int power_suspend_count;
static int power_s0ix_reset_tracking_count;

#define S5_INACTIVITY_TIMEOUT_MS                                               \
	COND_CODE_0(                                                           \
		AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout), (2 * MSEC_PER_SEC), \
		(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) * MSEC_PER_SEC))

#define TEST_SLEEP_TIMEOUT_MS 50

#ifdef CONFIG_AP_PWRSEQ_DRIVER
static void ap_pwrseq_wake(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	ap_pwrseq_post_event(dev, AP_PWRSEQ_EVENT_POWER_SIGNAL);
}
#endif

#define PWRSEQ_OUTPUT(nodeid)                                 \
	{                                                     \
		.signal_enum = PWR_SIGNAL_ENUM(nodeid),       \
		.signal_name = DT_PROP(nodeid, enum_name),    \
		.gpio_spec = GPIO_DT_SPEC_GET(nodeid, gpios), \
	},

#define PWRSEQ_GPIOS(nodeid) \
	IF_ENABLED(DT_PROP(nodeid, output), (PWRSEQ_OUTPUT(nodeid)))

struct ec_output {
	uint8_t signal_enum;
	char *signal_name;
	struct gpio_dt_spec gpio_spec;
};

/*
 * Get a list of power signals that are GPIO outputs.
 */
static const struct ec_output ec_outputs[] = { DT_FOREACH_STATUS_OKAY(
	intel_ap_pwrseq_gpio, PWRSEQ_GPIOS) };

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#define PWRSEQ_INPUT(nodeid)                                  \
	{                                                     \
		.signal_enum = PWR_SIGNAL_ENUM(nodeid),       \
		.signal_name = DT_PROP(nodeid, enum_name),    \
		.gpio_spec = GPIO_DT_SPEC_GET(nodeid, gpios), \
	},

#define PWRSEQ_INPUT_GPIOS(nodeid) \
	COND_CODE_1(DT_PROP(nodeid, output), (), (PWRSEQ_INPUT(nodeid)))

/*
 * Get a list of power signals that are GPIO inputs.
 */
static const struct ec_output ec_inputs[] = { DT_FOREACH_STATUS_OKAY(
	intel_ap_pwrseq_gpio, PWRSEQ_INPUT_GPIOS) };

/* Drive the logical level of a power signal GPIO input to the EC. */
static void set_ec_input(enum power_signal signal, int value)
{
	for (int i = 0; i < ARRAY_SIZE(ec_inputs); i++) {
		const struct gpio_dt_spec *spec = &ec_inputs[i].gpio_spec;

		if (ec_inputs[i].signal_enum != signal) {
			continue;
		}
		if (spec->dt_flags & GPIO_ACTIVE_LOW) {
			value = !value;
		}
		zassert_ok(gpio_emul_input_set(spec->port, spec->pin, value));
		return;
	}
	zassert_unreachable("Signal %d is not a GPIO input", signal);
}
#endif

/*
 * List of input signals to the AP that are driven by the EC. All signals
 * should start out at physical level 0 while in G3 and should end up at
 * physical level 1 when reaching S0.
 */
#ifdef CONFIG_AP_X86_INTEL_MTL
static const uint8_t ap_inputs[] = {
	PWR_EC_PCH_RSMRST,
	PWR_EC_PCH_SYS_PWROK,
};
#endif

#ifdef CONFIG_AP_X86_INTEL_ADL
/* clang-format off */
static const uint8_t ap_inputs[] = {
	PWR_EC_SOC_DSW_PWROK,
	PWR_PCH_PWROK,
	PWR_EC_PCH_RSMRST,
	PWR_VCCST_PWRGD,
	PWR_EC_PCH_SYS_PWROK,
};
/* clang-format on */
#endif

static void emul_ev_handler(struct ap_power_ev_callback *callback,
			    struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_RESUME:
		power_resume_count++;
		break;

	case AP_POWER_STARTUP:
		power_start_up_count++;
		break;

	case AP_POWER_HARD_OFF:
		power_hard_off_count++;
		break;

	case AP_POWER_SHUTDOWN:
		power_shutdown_count++;
		break;

	case AP_POWER_SHUTDOWN_COMPLETE:
		power_shutdown_complete_count++;
		break;

	case AP_POWER_SUSPEND:
		power_suspend_count++;
		break;

	case AP_POWER_S0IX_RESET_TRACKING:
		power_s0ix_reset_tracking_count++;
		break;

	default:
		break;
	};
}
AP_POWER_EVENT_CALLBACK_DEFINE(emul_ev_handler, AP_POWER_RESUME,
			       AP_POWER_STARTUP, AP_POWER_HARD_OFF,
			       AP_POWER_SUSPEND, AP_POWER_SHUTDOWN,
			       AP_POWER_SHUTDOWN_COMPLETE,
			       AP_POWER_S0IX_RESET_TRACKING);

static void ap_pwrseq_reset_ev_counters(void)
{
	power_resume_count = 0;
	power_start_up_count = 0;
	power_hard_off_count = 0;
	power_shutdown_count = 0;
	power_shutdown_complete_count = 0;
	power_suspend_count = 0;
	power_s0ix_reset_tracking_count = 0;
}

static void verify_ap_inputs(bool in_s0)
{
	int expected_level;

	if (in_s0) {
		expected_level = 1;
	} else {
		expected_level = 0;
	}

	LOG_INF("Verifying AP inputs are at physical level %d", expected_level);

	for (int ap = 0; ap < ARRAY_SIZE(ap_inputs); ap++) {
		for (int ec = 0; ec < ARRAY_SIZE(ec_outputs); ec++) {
			int phys_level;
			gpio_flags_t flags;

			if (ec_outputs[ec].signal_enum == ap_inputs[ap]) {
				gpio_emul_flags_get_dt(
					&ec_outputs[ec].gpio_spec, &flags);
				phys_level =
					gpio_emul_output_get_dt(
						&ec_outputs[ec].gpio_spec) ^
					(flags & GPIO_ACTIVE_LOW);
				zassert_equal(
					phys_level,
					expected_level ^
						(flags & GPIO_ACTIVE_LOW),
					"%s (%d) signal isn't at physical %d",
					ec_outputs[ec].signal_name,
					ec_outputs[ec].signal_enum,
					expected_level);
			}
		}
	}
}

static void power_up_test_g3_to_s0_helper(void)
{
	/* Verify all inputs to the AP start a physical level 0. */
	verify_ap_inputs(false);

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	ap_power_exit_hardoff();
	k_msleep(400);

	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_equal(1, power_resume_count,
		      "AP_POWER_RESUME event not generated");

	/* Once reaching S0, validate that all inputs to the
	 * AP are set to high level.
	 */
	verify_ap_inputs(true);
}

static bool get_cse_early_recovery_gpio_level(void)
{
	const static struct gpio_dt_spec cse_early_rec_gpio = GPIO_DT_SPEC_GET(
		DT_NODELABEL(cse_early_recovery), cse_early_rec_gpios);

	return gpio_emul_output_get_dt(&cse_early_rec_gpio);
}

ZTEST(ap_pwrseq, test_ap_pwrseq_0)
{
#ifdef CONFIG_TEST_AP_POWER_RECOVERY_MODE
	/* When recovery boot is requested, the CSE early recovery GPIO driver
	 * should assert a given GPIO pin when exiting G3.
	 */
	system_enter_manual_recovery();
#endif /* CONFIG_TEST_AP_POWER_RECOVERY_MODE */

	zassert_equal(0, get_cse_early_recovery_gpio_level());
	power_up_test_g3_to_s0_helper();

#ifdef CONFIG_TEST_AP_POWER_RECOVERY_MODE
	/* This is a recovery boot. GPIO should be asserted. */
	zassert_equal(1, get_cse_early_recovery_gpio_level());
#else
	/* Not doing a recovery boot. GPIO should be deasserted. */
	zassert_equal(0, get_cse_early_recovery_gpio_level());
#endif
}

/* Sleep hang test - this assumes the test is run after the test_ap_pwrseq_0
 * test above and that the current power state is S0.
 * At completion the power state remains in S0.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_sleep_hang)
{
	host_event_t lpc_event_mask;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT);

	/* Enable the hang detect event in the LPC event mask. */
	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, lpc_event_mask | mask);

	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params.sleep_timeout_ms = TEST_SLEEP_TIMEOUT_MS,
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);

	/* Now tell the EC that the AP is going to sleep, but don't change
	 * any of the power signals.  This causes a sleep timeout.
	 */
	zassert_ok(host_command_process(&host_sleep_ev_args));

	/* Purposely leave the SLP_S0 signal de-asserted to cause a timeout. */
	k_msleep(TEST_SLEEP_TIMEOUT_MS * 2);

	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));

	/* Retest, but this time set an infinite timeout.  Verify no event
	 * is reported.
	 */
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));
	host_sleep_ev_p.suspend_params.sleep_timeout_ms =
		EC_HOST_SLEEP_TIMEOUT_INFINITE;
	zassert_ok(host_command_process(&host_sleep_ev_args));
	k_sleep(K_SECONDS(10));

	zassert_false(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

/* Sleep success test - this assumes the current power state is S0 and
 * at completion the power state will be S0ix.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_sleep_success)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);

	struct ec_params_s0ix_cnt s0ix_cnt_ev_p = {
		.flags = EC_S0IX_COUNTER_RESET
	};
	struct ec_response_s0ix_cnt s0ix_cnt_ev_r;
	struct host_cmd_handler_args s0ix_cnt_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_S0IX_COUNTER, 0, s0ix_cnt_ev_r, s0ix_cnt_ev_p);

	/* Verify that counter is set to 0 */
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);

	/* Send host sleep event */
	zassert_ok(host_command_process(&host_sleep_ev_args));

	/* Assert SLP_S0# */
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_sleep)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	k_msleep(500);

	/*
	 * Verify that counter has been increased,
	 * clear the flag for get command
	 */
	s0ix_cnt_ev_p.flags = 0;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);
}

/* Wake from S0ix.  This test assumes the current power state is S0ix and
 * at completion the power state is S0.
 */
ZTEST(ap_pwrseq, test_ap_pwrseq_0_wake)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_RESUME,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);
	struct ec_params_s0ix_cnt s0ix_cnt_ev_p = { .flags = 0 };
	struct ec_response_s0ix_cnt s0ix_cnt_ev_r;
	struct host_cmd_handler_args s0ix_cnt_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_S0IX_COUNTER, 0, s0ix_cnt_ev_r, s0ix_cnt_ev_p);

	/* Confirm that counters keeps the same value through wakeup */
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_args.response_size, sizeof(s0ix_cnt_ev_r));
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);

	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_wake)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	k_msleep(500);
	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, sizeof(r));
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get sleep counter");
	zassert_equal(s0ix_cnt_ev_args.response_size, sizeof(s0ix_cnt_ev_r));
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);

	/* Verify the reset command sets the counter to zero */
	s0ix_cnt_ev_p.flags = EC_S0IX_COUNTER_RESET;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_args.response_size, sizeof(s0ix_cnt_ev_r));

	s0ix_cnt_ev_p.flags = 0;
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_args.response_size, sizeof(s0ix_cnt_ev_r));
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);
}

ZTEST(ap_pwrseq, test_ap_pwrseq_1)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_s0_power_fail)),
		      "Unable to load test platform `tp_sys_s0_power_fail`");

	/*
	 * Once emulated power signals are loaded, we need to wake AP power
	 * Sequence thread up to start executing new set of power signals
	 */
	ap_pwrseq_wake();
	k_msleep(500);
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(0, power_suspend_count,
		      "AP_POWER_SUSPEND event generated");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_2)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_g3_to_s0_power_down)),
		"Unable to load test platform `tp_sys_g3_to_s0_power_down`");

	ap_power_exit_hardoff();
	k_sleep(K_MSEC(S5_INACTIVITY_TIMEOUT_MS * 1.5));
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(1, power_suspend_count,
		      "AP_POWER_SUSPEND event not generated");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");
}

#if defined(CONFIG_AP_X86_INTEL_MTL)
ZTEST(ap_pwrseq, test_ap_pwrseq_3_sleep_reset)
{
	struct ec_params_host_sleep_event_v1 host_sleep_ev_p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 host_sleep_ev_r;
	struct host_cmd_handler_args host_sleep_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_HOST_SLEEP_EVENT, 1, host_sleep_ev_r, host_sleep_ev_p);

	struct ec_params_s0ix_cnt s0ix_cnt_ev_p = {
		.flags = EC_S0IX_COUNTER_RESET
	};
	struct ec_response_s0ix_cnt s0ix_cnt_ev_r;
	struct host_cmd_handler_args s0ix_cnt_ev_args = BUILD_HOST_COMMAND(
		EC_CMD_GET_S0IX_COUNTER, 0, s0ix_cnt_ev_r, s0ix_cnt_ev_p);

	/* Verify that counter is set to 0 */
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);

	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_g3_to_sleep_reset_then_power_fail)),
		      "Unable to load test platform "
		      "`tp_sys_g3_to_sleep_reset_then_power_fail`");

	ap_power_exit_hardoff();
	k_msleep(100);
	s0ix_cnt_ev_p.flags = 0;
	zassert_ok(host_command_process(&host_sleep_ev_args));

	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 0);

	k_msleep(100);
	zassert_ok(host_command_process(&s0ix_cnt_ev_args),
		   "Failed to get s0ix counter");

	/* Verify that counter has been increased */
	zassert_equal(s0ix_cnt_ev_r.s0ix_counter, 1);

	/* Trigger reset and later power fail */
	power_signal_set(PWR_SYS_RST, 1);
	k_msleep(100);
	power_signal_set(PWR_SYS_RST, 0);

	/* Verify host sleep state was reset after chipset reset */
	zassert_true(power_s0ix_reset_tracking_count > 0,
		     "AP_POWER_S0IX_RESET_TRACKING not called");
	zassert_equal(0, ap_power_sleep_get_notify(),
		      "Host sleep state not reset after power loss");

	/* Allow up to 50 ms for the force-shutdown timeout loop plus the
	 * 30 ms minimum power-down delay in board_ap_power_force_shutdown.
	 */
	k_msleep(150);

	/* Verify power down was detected */
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
}

#endif /* CONFIG_AP_X86_INTEL_MTL */

#if defined(CONFIG_AP_X86_INTEL_ADL)
ZTEST(ap_pwrseq, test_ap_pwrseq_3)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_s5_slp_sus_fail)),
		      "Unable to load test platform `tp_sys_s5_slp_sus_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	/*
	 * AP_PWRSEQ_DRIVER inhibits transition up from G3 due to slp_sus signal
	 * error, whereas the other implementation goes to G3S5 then notices the
	 * problem and goes back to G3, emitting a AP_POWER_HARD_OFF event in
	 * the process.
	 */
	if (IS_ENABLED(CONFIG_AP_PWRSEQ_DRIVER)) {
		zassert_equal(0, power_hard_off_count,
			      "AP_POWER_HARD_OFF event generated");
	} else {
		zassert_equal(1, power_hard_off_count,
			      "AP_POWER_HARD_OFF event not generated");
	}
}

ZTEST(ap_pwrseq, test_ap_pwrseq_4)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_s4_dsw_pwrok_fail)),
		"Unable to load test platform `tp_sys_s4_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_5)
{
	zassert_equal(
		0,
		power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			tp_sys_s3_dsw_pwrok_fail)),
		"Unable to load test platform `tp_sys_s3_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	k_msleep(500);

	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}
#endif /* CONFIG_AP_X86_INTEL_ADL */

ZTEST(ap_pwrseq, test_ap_pwrseq_6)
{
	zassert_equal(
		0,
		power_signal_emul_load(
			EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_s3_rsmrst_fail)),
		"Unable to load test platform `tp_sys_s3_dsw_pwrok_fail`");

	ap_power_exit_hardoff();
	/* Add 50 ms buffer to account for the 30 ms minimum power-down delay
	 * in board_ap_power_force_shutdown during the G3 re-entry.
	 */
	k_sleep(K_MSEC(S5_INACTIVITY_TIMEOUT_MS * 1.5 + 50));

#if defined(CONFIG_AP_X86_INTEL_ADL)
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");
#endif /* CONFIG_AP_X86_INTEL_ADL */
	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
}

#if !defined(CONFIG_AP_PWRSEQ_DRIVER)
ZTEST(ap_pwrseq, test_ap_pwrseq_7_all_sys_pwrgd_fail)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_g3_to_s0_all_sys_pwrgd_fail)),
		      "Unable to load test platform "
		      "`tp_sys_g3_to_s0_all_sys_pwrgd_fail`");

	/* System power fails while waiting to generate SYS_PWROK */
	ap_power_exit_hardoff();
	k_msleep(500);

	zassert_equal(0, power_signal_get(PWR_PCH_PWROK),
		      "PCH_PWROK should be deasserted");
	zassert_equal(0, power_signal_get(PWR_EC_PCH_SYS_PWROK),
		      "SYS_PWROK should not be generated");

	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(500);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be in hard off");
}
#endif /* !CONFIG_AP_PWRSEQ_DRIVER */

#if defined(CONFIG_AP_PWRSEQ_DRIVER)
ZTEST(ap_pwrseq, test_ap_pwrseq_7_force_shutdown)
{
	power_up_test_g3_to_s0_helper();

	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(500);

	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be in hard off after force shutdown");
}

/* Shut down the AP and leave it in G3 for the next test. */
static void power_down_to_g3(void)
{
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(500);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be in hard off");
}

/*
 * Power up from G3 to S0. Unlike power_up_test_g3_to_s0_helper(), this does
 * not check the initial AP input levels as not all test boards restore them
 * on a direct S0 to G3 transition.
 */
static void power_up_to_s0(void)
{
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");

	ap_power_exit_hardoff();
	k_msleep(400);
	zassert_equal(1, power_start_up_count,
		      "AP_POWER_STARTUP event not generated");
	zassert_true(chipset_in_state(CHIPSET_STATE_ON), "AP should be in S0");
}

/* Power up from G3 and wait until the AP has powered down to S5. */
static void power_up_then_down_to_s5(void)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_g3_to_s0_power_down)),
		      "Unable to load test platform "
		      "`tp_sys_g3_to_s0_power_down`");

	ap_power_exit_hardoff();
	k_msleep(300);
	zassert_true(chipset_in_state(CHIPSET_STATE_SOFT_OFF),
		     "AP should be in S5");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_8_reboot_ap_on_g3)
{
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_REBOOT_AP_ON_G3, 0);

	power_up_to_s0();

	/* The AP requests to be powered back on once it reaches G3 */
	zassert_ok(host_command_process(&args));
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(500);

	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_equal(2, power_start_up_count,
		      "AP did not restart after reaching G3");
	zassert_true(chipset_in_state(CHIPSET_STATE_ON),
		     "AP should be back in S0");

	power_down_to_g3();
}

ZTEST(ap_pwrseq, test_ap_pwrseq_8_reboot_ap_on_g3_delay)
{
	struct ec_params_reboot_ap_on_g3_v1 params = {
		.reboot_ap_at_g3_delay = 1,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_REBOOT_AP_ON_G3, 1, params);

	power_up_to_s0();

	/* The AP requests a reboot 1 second after reaching G3 */
	zassert_ok(host_command_process(&args));
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(500);

	zassert_equal(1, power_shutdown_complete_count,
		      "AP_POWER_SHUTDOWN_COMPLETE event not generated");
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should remain in G3 during the delay");

	/* A power on request while waiting does not cut the delay short */
	ap_power_exit_hardoff();
	k_msleep(100);
	zassert_equal(1, power_start_up_count,
		      "AP restarted before the G3 delay expired");
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should remain in G3 during the delay");

	k_msleep(1000);
	zassert_equal(2, power_start_up_count,
		      "AP did not restart after the G3 delay");
	zassert_true(chipset_in_state(CHIPSET_STATE_ON),
		     "AP should be back in S0");

	power_down_to_g3();
}

static bool wait_for_pwrseq_state(enum ap_pwrseq_state state, int timeout_ms)
{
	const struct device *dev = ap_pwrseq_get_instance();

	for (int i = 0; i < timeout_ms; i++) {
		if (ap_pwrseq_get_current_state(dev) == state) {
			return true;
		}
		k_msleep(1);
	}

	return false;
}

/*
 * Force a shutdown while the AP is passing through the given state on its way
 * down from S0, and verify it goes straight to G3 without waiting in S5.
 */
static void force_shutdown_during_power_down(enum ap_pwrseq_state state)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_g3_to_s0_power_down)),
		      "Unable to load test platform "
		      "`tp_sys_g3_to_s0_power_down`");

	ap_power_exit_hardoff();
	zassert_true(wait_for_pwrseq_state(state, 500), "AP did not reach %s",
		     ap_pwrseq_get_state_str(state));

	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(100);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should go to G3 immediately");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");
}

/*
 * TODO: Atria's board G3 handler does not turn the rails off on a shutdown
 * event while already in G3, so the AP is never shut down.
 */
#if defined(CONFIG_AP_X86_INTEL_MTL) && !defined(CONFIG_TEST_AP_POWER_ATRIARVP)
ZTEST(ap_pwrseq, test_ap_pwrseq_8_rsmrst_pwrgd_timeout)
{
	zassert_equal(0,
		      power_signal_emul_load(EMUL_POWER_SIGNAL_TEST_PLATFORM(
			      tp_sys_g3_to_s0_timeout)),
		      "Unable to load test platform `tp_sys_g3_to_s0_timeout`");

	/* RSMRST power good never asserts after the rails are enabled */
	ap_power_exit_hardoff();
	k_msleep(AP_PWRSEQ_DT_VALUE(wait_signal_timeout) + 500);

	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be shut down when RSMRST is not good");
	zassert_equal(0, power_start_up_count,
		      "AP_POWER_STARTUP event generated");
	zassert_equal(0, power_signal_get(PWR_EN_PP3300_A),
		      "Rails should be turned off");
}
#endif

/* Losing standby power while in S0ix shuts the AP down. */
ZTEST(ap_pwrseq, test_ap_pwrseq_9_s0ix_power_fail)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND,
		.suspend_params = { EC_HOST_SLEEP_TIMEOUT_DEFAULT },
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);

	power_up_to_s0();
#if DT_NODE_EXISTS(DT_NODELABEL(pwr_pltrst))
	/* The PCH releases PLTRST# once the AP is running */
	zassert_ok(emul_espi_host_send_vw(
		DEVICE_DT_GET_ANY(zephyr_espi_emul_controller),
		ESPI_VWIRE_SIGNAL_PLTRST, 1));
#endif

	zassert_ok(host_command_process(&args));
	power_signal_emul_unload();
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_sleep)),
		      "Unable to load test platform `tp_sys_sleep`");
	k_msleep(500);
	zassert_true(chipset_in_state(CHIPSET_STATE_STANDBY),
		     "AP should be in S0ix");

#if defined(CONFIG_AP_X86_INTEL_ADL)
	set_ec_input(PWR_SLP_SUS, 1);
#else
	set_ec_input(PWR_RSMRST_PWRGD, 0);
#endif
	k_msleep(500);

	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should be shut down after power failure");
	zassert_equal(1, power_shutdown_count,
		      "AP_POWER_SHUTDOWN event not generated");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");

#if DT_NODE_EXISTS(DT_NODELABEL(pwr_pltrst))
	zassert_ok(emul_espi_host_send_vw(
		DEVICE_DT_GET_ANY(zephyr_espi_emul_controller),
		ESPI_VWIRE_SIGNAL_PLTRST, 0));
#endif
}

ZTEST(ap_pwrseq, test_ap_pwrseq_9_s3_force_shutdown)
{
	force_shutdown_during_power_down(AP_POWER_STATE_S3);
}

ZTEST(ap_pwrseq, test_ap_pwrseq_9_s4_force_shutdown)
{
	force_shutdown_during_power_down(AP_POWER_STATE_S4);
}

ZTEST(ap_pwrseq, test_ap_pwrseq_9_s5_debug_mode)
{
	power_up_then_down_to_s5();

	/* debug_mode keeps the AP in S5 past the inactivity timeout */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "debug_mode enable"));
	k_msleep(S5_INACTIVITY_TIMEOUT_MS * 1.5);
	zassert_true(chipset_in_state(CHIPSET_STATE_SOFT_OFF),
		     "debug_mode should prevent the transition to G3");
	zassert_equal(0, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");

	zassert_ok(shell_execute_cmd(get_ec_shell(), "debug_mode disable"));
	ap_pwrseq_wake();
	k_msleep(100);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should go to G3 once debug_mode is disabled");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_9_s5_exit_hardoff_extends_timeout)
{
	power_up_then_down_to_s5();

	/*
	 * A power on request while in S5 restarts the inactivity timer to
	 * give the AP more time to respond.
	 */
	k_msleep(S5_INACTIVITY_TIMEOUT_MS / 2);
	ap_power_exit_hardoff();
	k_msleep(S5_INACTIVITY_TIMEOUT_MS * 3 / 4);
	zassert_true(chipset_in_state(CHIPSET_STATE_SOFT_OFF),
		     "S5 inactivity timer was not restarted");
	zassert_equal(0, power_hard_off_count,
		      "AP_POWER_HARD_OFF event generated");

	k_msleep(S5_INACTIVITY_TIMEOUT_MS / 2);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should go to G3 after the S5 inactivity timeout");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");
	zassert_equal(1, power_start_up_count, "AP should not restart from S5");
}

ZTEST(ap_pwrseq, test_ap_pwrseq_9_s5_force_shutdown)
{
	power_up_then_down_to_s5();

	/* Force shutdown does not wait for the S5 inactivity timeout */
	ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	k_msleep(100);
	zassert_true(chipset_in_state(CHIPSET_STATE_HARD_OFF),
		     "AP should go to G3 immediately");
	zassert_equal(1, power_hard_off_count,
		      "AP_POWER_HARD_OFF event not generated");
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

ZTEST(ap_pwrseq, test_ap_power_reset_while_in_reset)
{
	/* Toggling SYS_RESET_L is skipped when the AP is already in reset */
	power_signal_set(PWR_SYS_RST, 1);
	ap_power_reset(AP_POWER_SHUTDOWN_G3);
	zassert_equal(1, power_signal_get(PWR_SYS_RST),
		      "SYS_RST should remain asserted");
	power_signal_set(PWR_SYS_RST, 0);
}

ZTEST(ap_pwrseq, test_insufficient_power_blocks_s5)
{
	zassert_equal(0,
		      power_signal_emul_load(
			      EMUL_POWER_SIGNAL_TEST_PLATFORM(tp_sys_g3_to_s0)),
		      "Unable to load test platform `tp_sys_g3_to_s0`");
	system_can_boot_ap_fake.return_val = 0;

	ap_power_exit_hardoff();
	k_msleep(5000);

	zassert_equal(40, system_can_boot_ap_fake.call_count);
	zassert_true(
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF));
}

/* Utilities for finding a Zephyr thread by name */
static k_tid_t found_thread;
static void find_thread_by_name_cb(const struct k_thread *thread,
				   void *user_data)
{
	const char *name = (const char *)user_data;

	if (strcmp(k_thread_name_get((k_tid_t)thread), name) == 0) {
		found_thread = (k_tid_t)thread;
	}
}

static k_tid_t find_thread_by_name(const char *name)
{
	found_thread = NULL;
	k_thread_foreach_unlocked(find_thread_by_name_cb, (void *)name);
	return found_thread;
}

ZTEST(ap_pwrseq, test_get_ap_pwrseq_thread)
{
	k_tid_t pwrseq_thread;
	const char *pwrseq_name;

	if (IS_ENABLED(CONFIG_AP_PWRSEQ_DRIVER))
		pwrseq_name = "ap_pwrseq_tid";
	else
		pwrseq_name = "pwrseq_task";
	pwrseq_thread = find_thread_by_name(pwrseq_name);
	zassert_not_null(pwrseq_thread);
	zassert_equal(pwrseq_thread, get_ap_pwrseq_thread());
}

void ap_pwrseq_after_test(void *data)
{
	system_exit_manual_recovery();
	power_signal_emul_unload();
	ap_pwrseq_reset_ev_counters();
}

ZTEST_SUITE(ap_pwrseq, ap_power_predicate_post_main, NULL, NULL,
	    ap_pwrseq_after_test, NULL);
