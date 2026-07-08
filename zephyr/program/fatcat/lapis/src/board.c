/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "lid_switch.h"

LOG_MODULE_REGISTER(lapis_board, LOG_LEVEL_INF);

static void kb_layout_init(void)
{
	if (cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_kb_canada)))) {
		/*
		 * Canadian French keyboard (US layout),
		 *   \| (key 45):     0x0061->0x61->0x56
		 *   r-ctrl (key 64): 0xe014->0x14->0x1d
		 * move key45 (row:2,col:7) to key64 (row:3,col:14)
		 */
		set_scancode_set2(3, 14, get_scancode_set2(2, 7));
		LOG_INF("CBI USFC: FW_KB_LAYOUT_US2");
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);

static void set_chg_reg_custom(void)
{
	charger_set_frequency(808);
}
DECLARE_HOOK(HOOK_INIT, set_chg_reg_custom, HOOK_PRIO_POST_BATTERY_INIT + 1);

static void set_chg_control3(void)
{
	int reg, rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			ISL9241_REG_CONTROL3, &reg);

	if (rv || (reg & ISL9241_CONTROL3_INPUT_CURRENT_LIMIT))
		return;

	reg |= ISL9241_CONTROL3_INPUT_CURRENT_LIMIT;
	rv = i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			 ISL9241_REG_CONTROL3, reg);

	if (rv)
		return;

	LOG_INF("Disable Input Current Limit");
}

static void restore_chg_control3(void)
{
	int reg, rv;

	rv = i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			ISL9241_REG_CONTROL3, &reg);
	if (rv || (!(reg & ISL9241_CONTROL3_INPUT_CURRENT_LIMIT)))
		return;

	reg &= ~ISL9241_CONTROL3_INPUT_CURRENT_LIMIT;
	rv = i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
			 ISL9241_REG_CONTROL3, reg);

	if (rv)
		return;

	LOG_INF("Enable Input Current Limit");
}

static void detect_aconly(void)
{
	const struct batt_params *batt = charger_current_battery_params();

	if (extpower_is_present()) {
		if (batt->is_present == BP_NO) {
			set_chg_control3();
		} else {
			restore_chg_control3();
		}
	}
}
DECLARE_HOOK(HOOK_INIT, detect_aconly, HOOK_PRIO_DEFAULT + 1);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, detect_aconly, HOOK_PRIO_DEFAULT + 1);

static void tp_bl_enable(void)
{
	if (lid_is_open() && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), true);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_backoff), true);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_disable), false);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_backoff),
				false);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, tp_bl_enable, HOOK_PRIO_DEFAULT);

static void disable_sleep_bid(void)
{
	uint32_t board_id = 0;
	/* Errors will count as board_id 0 */
	cbi_get_board_version(&board_id);
	/* Force board_id to 2 to work around missing CBI board ID on this DUT */
	board_id = 2;

	if (board_id > 1)
		enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
	else
		disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
}
DECLARE_HOOK(HOOK_INIT, disable_sleep_bid, HOOK_PRIO_POST_I2C);

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

static struct uart_config saved_uart_cfg;
#define UART_IRQ DT_IRQN(DT_CHOSEN(zephyr_console))

/* Get the GPIO spec for the shared PDC interrupt pin (shared by both ports) */
static const struct gpio_dt_spec pd_int_spec =
	GPIO_DT_SPEC_GET(DT_NODELABEL(pdc_power_p0), irq_gpios);

static void board_uart_suspend(void)
{
	const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (device_is_ready(uart_dev)) {
		/* Save the current UART configuration before power loss */
		uart_config_get(uart_dev, &saved_uart_cfg);
		
		/* Disable the UART console IRQ at the NVIC level (un-bypassable CPU-level block) */
		irq_disable(UART_IRQ);
		
		/* Disable the PDC GPIO interrupt to prevent I2C transactions during sleep */
		if (gpio_is_ready_dt(&pd_int_spec)) {
			gpio_pin_interrupt_configure_dt(&pd_int_spec, GPIO_INT_MODE_DISABLED);
		}
		
		/* Disable all UART interrupts in the peripheral registers */
		uart_irq_tx_disable(uart_dev);
		uart_irq_rx_disable(uart_dev);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_uart_suspend, HOOK_PRIO_FIRST);

static void board_uart_resume(void)
{
	const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (device_is_ready(uart_dev)) {
		unsigned char c;
		
		/* Re-enable the PDC GPIO interrupt */
		if (gpio_is_ready_dt(&pd_int_spec)) {
			gpio_pin_interrupt_configure_dt(&pd_int_spec, GPIO_INT_EDGE_TO_ACTIVE);
		}
		
		/* Restore the UART configuration and registers after power-on */
		uart_configure(uart_dev, &saved_uart_cfg);
		/* Flush the hardware RX FIFO of all stale characters to prevent post-resume storms */
		while (uart_poll_in(uart_dev, &c) == 0) {
			/* Discard */
		}
		/* Re-enable RX interrupt in the peripheral registers */
		uart_irq_rx_enable(uart_dev);
		
		/* Re-enable the UART console IRQ at the NVIC level */
		irq_enable(UART_IRQ);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_uart_resume, HOOK_PRIO_LAST);
