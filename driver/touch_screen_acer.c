/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_ZEPHYR
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#else
#include "byteorder.h"
#endif

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "sha256.h"
#include "shared_mem.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usb_api.h"
#include "usb_hid_touch_screen.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ##args)

#ifdef CONFIG_ZEPHYR
#if DT_HAS_COMPAT_STATUS_OKAY(acer_kl1400h)
#define TOUCH_SCREEN_NODE DT_INST(0, acer_kl1400h)
#define GPIO_TOUCH_SCREEN_INT \
	GPIO_SIGNAL(DT_PROP(DT_PROP(TOUCH_SCREEN_NODE, irq), irq_pin))
#define CONFIG_TOUCH_SCREEN_I2C_ADDR_FLAGS DT_REG_ADDR(TOUCH_SCREEN_NODE)
#define CONFIG_TOUCH_SCREEN_I2C_PORT I2C_PORT_BY_DEV(TOUCH_SCREEN_NODE)
#endif /* DT_HAS_COMPAT_STATUS_OKAY(acer_kl1400h) */
#endif /* CONFIG_ZEPHYR */

static void touch_screen_read_report(void)
{
	struct usb_hid_touch_screen_report report;
	uint8_t buf[sizeof(struct usb_hid_touch_screen_report)];
	int ret;

	ret = i2c_xfer(CONFIG_TOUCH_SCREEN_I2C_PORT,
		       CONFIG_TOUCH_SCREEN_I2C_ADDR_FLAGS, NULL, 0, buf,
		       sizeof(buf));
	if (ret) {
		CPRINTS("failed to read i2c data from touch screen, %d\n", ret);
		return;
	}

	memcpy(&report, buf, sizeof(report));

	set_touch_screen_report(&report);
}

void touch_screen_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_TOUCH_SCREEN);
}

static void touch_screen_init(void)
{
	CPRINTS("%s", __func__);

	/* enable interrupt to fetch reports */
	gpio_enable_interrupt(GPIO_TOUCH_SCREEN_INT);
}

DECLARE_DEFERRED(touch_screen_init);

void touch_screen_task(void *u)
{
	uint32_t event;

	touch_screen_init();

	while (1) {
		event = task_wait_event(-1);

		if (event & TASK_EVENT_WAKE) {
			touch_screen_read_report();
		}
	}
}
