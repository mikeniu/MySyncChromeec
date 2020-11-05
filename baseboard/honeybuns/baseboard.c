/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns family-specific configuration */
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "usb_pd.h"
#include "util.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

struct hpd_mark {
	int level;
	uint64_t ts;
	enum gpio_signal signal;
};

#ifdef SECTION_IS_RW
static struct hpd_mark hpd_last_event;
static int hpd_override;
#endif
/******************************************************************************/

static void board_power_sequence(void)
{
	int i;

	for(i = 0; i < board_power_seq_count; i++) {
		gpio_set_level(board_power_seq[i].signal,
			       board_power_seq[i].level);
		if (board_power_seq[i].delay_ms)
			msleep(board_power_seq[i].delay_ms);
	}
}

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"i2c1",  I2C_PORT_I2C1,  400, GPIO_EC_I2C1_SCL, GPIO_EC_I2C1_SDA},
#ifndef QUICHE_BOARD_P1
#ifndef GINGERBREAD_BOARD_P1
	{"i2c2",  I2C_PORT_I2C2,  400, GPIO_EC_I2C2_SCL, GPIO_EC_I2C2_SDA},
#endif
#endif
	{"i2c3",  I2C_PORT_I2C3,  400, GPIO_EC_I2C3_SCL, GPIO_EC_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifndef SECTION_IS_RW
static void baseboard_set_usbc_sink_mode(void)
{
	uint32_t cr;

	/*
	 * Bare minimum code required to enable UCPD peripheral and apply Rd to
	 * the CC lines. This is only applied in RO.
	 */
	/* Ensure that clock to UCPD is enabled */
	STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_UPCD1EN;
	/* enable the peripheral */
	STM32_UCPD_CFGR1(0) |= STM32_UCPD_CFGR1_UCPDEN;

	cr = STM32_UCPD_CR(0);
	/* Apply Rd to both CC lines */
	cr |= STM32_UCPD_CR_ANAMODE | STM32_UCPD_CR_CCENABLE_MASK;
	STM32_UCPD_CR(0) = cr;

	CPRINTS("usbc: CR = 0x%x", STM32_UCPD_CR(0));
}
#endif

static void baseboard_init(void)
{
	/* Turn on power rails */
	board_power_sequence();
	CPRINTS("board: Power rails enabled");
#ifdef SECTION_IS_RW
	system_clear_reset_flags(EC_RESET_FLAG_POWER_ON);
#else
	baseboard_set_usbc_sink_mode();
#endif
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);

#ifdef SECTION_IS_RW
void board_reset_pd_mcu(void)
{

}

void baseboard_trigger_hpd_irq(void)
{
	pd_send_hpd(0, hpd_irq);
	CPRINTS("hpd: irq detected");
}
DECLARE_DEFERRED(baseboard_trigger_hpd_irq);

void baseboard_trigger_hpd_chg(void)
{
	int level = gpio_get_level(hpd_last_event.signal);

	if (level != hpd_last_event.level)
		/* It's a glitch while in deferred or canceled action */
		return;

	if (!hpd_override)
		pd_send_hpd(0, (level) ? hpd_high : hpd_low);

	CPRINTS("hpd: new level = %d", level);
}
DECLARE_DEFERRED(baseboard_trigger_hpd_chg);

void baseboard_hpd_info(void)
{

}
DECLARE_DEFERRED(baseboard_hpd_info);

void baseboard_manage_hpd_event(int signal)
{
	timestamp_t t_now = get_time();
	int level = gpio_get_level(signal);
	uint64_t t_delta = t_now.val - hpd_last_event.ts;

	/*
	 * HPD to honeybuns is from the MST hub. This is same signal that MST
	 * sends to demux chip. The MST manages glitch detection, so edges on
	 * the HPD signal to EC does not require any debouncing.
	 *
	 * There are 3 HPD events which need to be detected:
	 *    1) HPD_IRQ -> low pulse where 250 uSEc < t < 2 msec
	 *    2) HPD_LOW -> high -> low change with t > 2 msec
	 *    3) HPD_HIGH -> low -> high change with t > 2 msec
	 */

	hook_call_deferred(&baseboard_hpd_info_data, 0);

	/* All previous hpd level events need to be re-triggered */
	hook_call_deferred(&baseboard_trigger_hpd_chg_data, -1);

	if ((!hpd_last_event.level && level) &&
	    (t_delta < HPD_USTREAM_DEBOUNCE_LVL))
		/* Low -> High edge && pulse_width < 2 msec = HPD_IRQ */
		hook_call_deferred(&baseboard_trigger_hpd_irq_data, 0);
	else if (t_delta >= HPD_USTREAM_DEBOUNCE_LVL)
		hook_call_deferred(&baseboard_trigger_hpd_chg_data,
				   HPD_USTREAM_DEBOUNCE_LVL);

	/* Save current info for subsequent hpd edge */
	hpd_last_event.level = level;
	hpd_last_event.ts = t_now.val;
	/* This isn't changing but needs to be set at least once */
	hpd_last_event.signal = signal;
}


static int command_hpd(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	hpd_override = 1;
	if (!strcasecmp(argv[1], "high")) {
		pd_send_hpd(0, hpd_high);
		msleep(10);
		pd_send_hpd(0, hpd_irq);
	} else if (!strcasecmp(argv[1], "low")) {
		pd_send_hpd(0, hpd_low);
	} else if (!strcasecmp(argv[1], "irq")) {
		pd_send_hpd(0, hpd_irq);
	} else if (!strcasecmp(argv[1], "auto")) {
		hpd_override = 0;
	} else {
	}
	ccprintf("hpd: override = %d\n", hpd_override);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hpd, command_hpd,
			"[high|low|auto]",
			"Turn on/off");
#endif
