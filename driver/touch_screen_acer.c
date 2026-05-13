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
#include "driver/touchpad_elan.h"
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
#include "usb_hid_touchpad.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ##args)

void touch_screen_interrupt(enum gpio_signal signal)
{
	/* TODO: do something */
	CPRINTF("%s ite debug %d: touch screen interrupt is triggered\n",
		__func__, __LINE__);
}
