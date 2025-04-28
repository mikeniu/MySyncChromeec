/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "gpio.h"
#include "power/qcom.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Threshold for VPH PWR (mV) */
#define VPH_PWR_THRESHOLD 1000

void board_set_switchcap_power(int enable)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_vph_pwr_1a), enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_vph_pwr_1a));
}

int board_is_switchcap_power_good(void)
{
	int adc_value = adc_read_channel(ADC_VPH_PWR);
	CPRINTS("switchcap VPH power good ADC value=%d", adc_value);
	return adc_value > VPH_PWR_THRESHOLD;
}
