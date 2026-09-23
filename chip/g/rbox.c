/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "console.h"
#include "ec_commands.h"
#include "extension.h"
#include "hooks.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define RBOX_LOCKOUT_MAX_SEC 600

void rbox_clear_wakeup(void)
{
	int i = 0;

	/* Clear the wakeup interrupt */
	GREG32(RBOX, WAKEUP) = GC_RBOX_WAKEUP_CLEAR_MASK;
	/*
	 * Wait until the interrupt status register is cleared, since RBOX runs
	 * off of RTC instead of the core clock. Wait a max of 50 iterations.
	 * Experimentally, 15 iterations is usually sufficient. We don't want to
	 * wait here forever.
	 */
	while (GREAD(RBOX, WAKEUP_INTR) && i < 50)
		i++;
}

int rbox_powerbtn_is_pressed(void)
{
	return !GREAD_FIELD(RBOX, CHECK_INPUT, PWRB_IN);
}

/*
 * This is 4X as RDD_MAX_WAIT_TIME_COUNTER default value, which should be
 *  long enough for rdd_is_detected() to represent a stable RDD status
 */
#define RDD_WAIT_TIME		(40 * MSEC)

/*
 * Delay EC_RST_L release if RDD cable is connected, or release EC_RST_L
 * otherwise.
 */
static void rbox_check_rdd(void)
{
#ifdef CR50_DEV
	print_rdd_state();
#endif
	if (rbox_powerbtn_is_pressed() && rdd_is_detected()) {
		power_button_release_enable_interrupt(1);
		return;
	}

	deassert_ec_rst();
}
DECLARE_DEFERRED(rbox_check_rdd);

static void rbox_release_ec_reset(void)
{
	/* Unfreeze the PINMUX */
	GREG32(PINMUX, HOLD) = 0;

	/*
	 * If the board uses closed loop reset, the short EC_RST_L pulse may
	 * not actually put the system in reset. Don't release EC_RST_L here.
	 * Let ap_state.c handle it once it sees the system is reset.
	 *
	 * Release PINMUX HOLD, so the board can detect changes on TPM_RST_L.
	 */
	if (!(system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE) &&
	    board_uses_closed_loop_reset()) {
		return;
	}

	/*
	 * After a POR, if the power button is held, then delay releasing
	 * EC_RST_L.
	 */
	if ((system_get_reset_flags() & EC_RESET_FLAG_POWER_ON) &&
	    rbox_powerbtn_is_pressed()) {
		hook_call_deferred(&rbox_check_rdd_data, RDD_WAIT_TIME);
		return;
	}

	/* Allow some time for outputs to stabilize. */
	usleep(500);

	/* Let the EC go (the RO bootloader asserts it ASAP after POR) */
	deassert_ec_rst();
}
DECLARE_HOOK(HOOK_INIT, rbox_release_ec_reset, HOOK_PRIO_LAST);

static void rbox_init(void)
{
	/* Enable RBOX */
	clock_enable_module(MODULE_RBOX, 1);

	/* Clear any interrupt bits (write 1's to clear) */
	GREG32(RBOX, INT_STATE) = 0xffffffff;

	/* Clear any wakeup bits */
	rbox_clear_wakeup();

	/* Disable rbox wakeup. It will be reenabled before entering sleep. */
	GREG32(RBOX, WAKEUP) = 0;

	/* Override rbox fuses and setup correct behavior */
	GWRITE(RBOX, DEBUG_CLK10HZ_COUNT, 0x63ff);
	GWRITE(RBOX, DEBUG_SHORT_DELAY_COUNT, 0x4ff);
	GWRITE(RBOX, DEBUG_LONG_DELAY_COUNT, 0x31);
	GWRITE(RBOX, DEBUG_DEBOUNCE, 0x4);
	GWRITE(RBOX, DEBUG_KEY_COMBO0, 0xC0);
	GWRITE(RBOX, DEBUG_KEY_COMBO1, 0x0);
	GWRITE(RBOX, DEBUG_KEY_COMBO2, 0x0);
	/* DEBUG_BLOCK_OUTPUT value should be 0x7 */
	GWRITE(RBOX, DEBUG_BLOCK_OUTPUT,
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY0_SEL_MASK |
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY1_SEL_MASK |
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY0_VAL_MASK);
	/* DEBUG_POL value should be 0x21 */
	GWRITE(RBOX, DEBUG_POL,
	       0x1 << GC_RBOX_DEBUG_POL_AC_PRESENT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_PWRB_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_PWRB_OUT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY0_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_POL_KEY1_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY1_OUT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_EC_RST_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_BATT_DISABLE_LSB);
	/* DEBUG_TERM value should be 0x1204 */
	GWRITE(RBOX, DEBUG_TERM,
	       0x0 << GC_RBOX_DEBUG_TERM_AC_PRESENT_LSB |
	       0x1 << GC_RBOX_DEBUG_TERM_ENTERING_RW_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_PWRB_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_PWRB_OUT_LSB |
	       0x2 << GC_RBOX_DEBUG_TERM_KEY0_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_TERM_KEY1_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_KEY1_OUT_LSB);
	/* DEBUG_DRIVE value should be 0x157 */
	GWRITE(RBOX, DEBUG_DRIVE,
	       0x3 << GC_RBOX_DEBUG_DRIVE_PWRB_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_KEY1_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_EC_RST_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_BATT_DISABLE_LSB);
	/* FUSE_CTRL value should be 0x3 */
	GWRITE(RBOX, FUSE_CTRL,
	       GC_RBOX_FUSE_CTRL_OVERRIDE_FUSE_MASK |
	       GC_RBOX_FUSE_CTRL_OVERRIDE_FUSE_READY_MASK);
}
DECLARE_HOOK(HOOK_INIT, rbox_init, HOOK_PRIO_DEFAULT - 1);

static void rbox_lockout_set_hw(int enable)
{
	uint32_t mask = GC_RBOX_CHECK_OUTPUT_PWRB_OUT_MASK; /* Bit 2 (0x4) */

	if (enable) {
		/* 1. Disable Refresh+Power (COMBO0) hardware EC reset */
		GWRITE(RBOX, DEBUG_KEY_COMBO0, 0x0);

		/* 2. Force PWRB_OUT (bit 2) output-enabled HIGH (1 = unpressed) */
		GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, VAL,
			     GREAD_FIELD(RBOX, OVERRIDE_OUTPUT, VAL) | mask);
		GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, OEN,
			     GREAD_FIELD(RBOX, OVERRIDE_OUTPUT, OEN) | mask);
		GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, EN,
			     GREAD_FIELD(RBOX, OVERRIDE_OUTPUT, EN) | mask);
	} else {
		/* 1. Return PWRB_OUT (bit 2) to hardware passthrough */
		GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, EN,
			     GREAD_FIELD(RBOX, OVERRIDE_OUTPUT, EN) & ~mask);
		GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, OEN,
			     GREAD_FIELD(RBOX, OVERRIDE_OUTPUT, OEN) & ~mask);

		/* 2. Re-enable Refresh+Power (COMBO0 = 0xC0) */
		GWRITE(RBOX, DEBUG_KEY_COMBO0, 0xC0);
	}
}

static void rbox_lockout_expire(void)
{
	CPRINTS("RBOX lockout expired; restoring passthrough");
	rbox_lockout_set_hw(0);
}
DECLARE_DEFERRED(rbox_lockout_expire);

static void rbox_set_lockout(uint16_t timeout_sec)
{
	if (timeout_sec > RBOX_LOCKOUT_MAX_SEC)
		timeout_sec = RBOX_LOCKOUT_MAX_SEC;

	if (timeout_sec == 0) {
		hook_call_deferred(&rbox_lockout_expire_data, -1);
		rbox_lockout_set_hw(0);
		CPRINTS("RBOX lockout disabled");
		return;
	}

	rbox_lockout_set_hw(1);
	/* Keep Cr50 awake while timer is active and schedule auto-revert */
	delay_sleep_by((uint32_t)timeout_sec * SECOND);
	hook_call_deferred(&rbox_lockout_expire_data,
			   (uint32_t)timeout_sec * SECOND);
	CPRINTS("RBOX lockout enabled for %d s", timeout_sec);
}

static int command_rbox_lockout(int argc, char **argv)
{
	char *e;
	int sec = 0;

	if (argc > 1) {
		sec = strtoi(argv[1], &e, 10);
		if (*e || sec < 0)
			return EC_ERROR_PARAM1;
		if (sec > RBOX_LOCKOUT_MAX_SEC)
			sec = RBOX_LOCKOUT_MAX_SEC;
		rbox_set_lockout((uint16_t)sec);
	}
	ccprintf("OVERRIDE_OUTPUT=0x%08x DEBUG_KEY_COMBO0=0x%02x\n",
		 GREG32(RBOX, OVERRIDE_OUTPUT), GREAD(RBOX, DEBUG_KEY_COMBO0));
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rboxlockout, command_rbox_lockout, "[timeout_sec]",
			     "Lock out PWRB_OUT and Refresh+Power");

static enum vendor_cmd_rc vc_rbox_lockout(enum vendor_cmd_cc code,
					  void *buf, size_t input_size,
					  size_t *response_size)
{
	uint16_t timeout_sec = 0;

	*response_size = 0;
	if (input_size >= sizeof(uint16_t)) {
		const uint8_t *in = buf;
		timeout_sec = ((uint16_t)in[0] << 8) | in[1];
	}
	rbox_set_lockout(timeout_sec);
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_RBOX_LOCKOUT, vc_rbox_lockout);
