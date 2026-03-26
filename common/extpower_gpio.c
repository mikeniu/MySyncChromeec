/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pure GPIO-based external power detection */

#include "extpower.h"

#include <zephyr/devicetree.h>
#include <drivers/cros_irq_gpio.h>

const struct device *acok_dev = DEVICE_DT_GET(DT_NODELABEL(acok_gpio));

__overridable int board_extpower_is_present(void)
{
	return 0;
}

__overridable void board_extpower_enable_interrupt(void)
{
}

__overridable void board_extpower_disable_interrupt(void)
{
}

static int debounced_extpower_presence;

void extpower_enable_interrupt(void)
{
#ifdef CONFIG_EXTPOWER_GPIO_CUSTOM
	board_extpower_enable_interrupt();
#else
	irq_gpio_enable(acok_dev);
#endif
}

void extpower_disable_interrupt(void)
{
#ifdef CONFIG_EXTPOWER_GPIO_CUSTOM
	board_extpower_disable_interrupt();
#else
	irq_gpio_disable(acok_dev);
#endif
}

test_mockable int extpower_is_present(void)
{
	return debounced_extpower_presence;
}

void extpower_callback(const struct device *dev, void *data)
{
	debounced_extpower_presence = irq_gpio_pin_get(dev);
}
CROS_IRQ_GPIO_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(acok_gpio)), extpower_callback, NULL);
