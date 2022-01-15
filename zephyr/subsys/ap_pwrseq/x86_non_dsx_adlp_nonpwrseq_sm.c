/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/atomic.h>
#include <x86_non_dsx_adlp_pwrseq_sm.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>
#include "drivers/sensor.h"

LOG_MODULE_REGISTER(adlp_nonpwseq, LOG_LEVEL_INF);

#define X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_STHUTDOWN_TO		5

#define pp3300_s5_on_dev DEVICE_DT_GET(DT_NODELABEL(cmp_pp3300_s5_high))
#define pp3300_s5_off_dev DEVICE_DT_GET(DT_NODELABEL(cmp_pp3300_s5_low))

#define pp1p05_on_dev DEVICE_DT_GET(DT_NODELABEL(cmp_pp1p05_high))
#define pp1p05_off_dev DEVICE_DT_GET(DT_NODELABEL(cmp_pp1p05_low))

atomic_t pp3300_a_pgood = 0;
atomic_t pp1p05_pgood = 0;

int intel_x86_get_pg_ec_dsw_pwrok(void)
{
	/*
	 * The PP3300_A rail is an input to generate DPWROK.  Assuming that
	 * power is good if voltage is at least 80% of nominal level.  We cannot
	 * read the ADC values during an interrupt, therefore, this power good
	 * value is updated via ADC threshold interrupts.
	 */
	return pp3300_a_pgood;
}

void generate_ec_soc_dsw_pwrok_handler(const struct common_pwrseq_config
				       *com_cfg)
{
	int in_sig_val =  intel_x86_get_pg_ec_dsw_pwrok();

	if (in_sig_val != gpio_pin_get_dt(&com_cfg->ec_soc_dsw_pwrok)) {
		if (in_sig_val)
			k_msleep(com_cfg->pch_dsw_pwrok_delay_ms);
		gpio_pin_set_dt(&com_cfg->ec_soc_dsw_pwrok, 1);
	}
}

void chipset_force_shutdown(enum pwrseq_chipset_shutdown_reason reason,
			    const struct common_pwrseq_config *com_cfg)
{
	int timeout_ms = X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_STHUTDOWN_TO;

	gpio_pin_set_dt(&com_cfg->ec_pch_rsmrst_odl, 0);
	gpio_pin_set_dt(&com_cfg->ec_soc_dsw_pwrok, 0);

	while(gpio_pin_get_dt(&com_cfg->pg_ec_rsmrst_odl) == 1 &&
	      gpio_pin_get_dt(&com_cfg->slp_sus_l) == 1 && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	}
	if (gpio_pin_get_dt(&com_cfg->slp_sus_l) == 1) {
		LOG_WRN("SLP_SUS is not deasserted! Assuming G3");
	}

	if (gpio_pin_get_dt(&com_cfg->pg_ec_rsmrst_odl) == 1) {
		LOG_WRN("RSMRST is not deasserted! Assuming G3");
	}

	gpio_pin_set_dt(&com_cfg->enable_pp3300_a, 0);

	gpio_pin_set_dt(&com_cfg->enable_pp5000_a, 0);

	timeout_ms = X86_NON_DSX_ADLP_NONPWRSEQ_FORCE_STHUTDOWN_TO;
	while (intel_x86_get_pg_ec_dsw_pwrok() && timeout_ms > 0) {
		k_msleep(1);
		timeout_ms--;
	};

	if (intel_x86_get_pg_ec_dsw_pwrok())
		LOG_WRN("DSW_PWROK didn't go low!  Assuming G3.");
}

void g3s5_action_handler(const struct common_pwrseq_config *com_cfg)
{
	gpio_pin_set_dt(&com_cfg->enable_pp5000_a, 1);

	gpio_pin_set_dt(&com_cfg->enable_pp3300_a, 1);

	power_wait_signals(IN_PGOOD_ALL_CORE);

	generate_ec_soc_dsw_pwrok_handler(com_cfg);
}

int generate_pch_pwrok_handler(const struct chipset_pwrseq_config *chip_cfg)
{
	/* Pass though PCH_PWROK */
	if (gpio_pin_get_dt(&chip_cfg->pch_pwrok) == 0) {
		k_msleep(chip_cfg->pch_pwrok_delay_ms);
		gpio_pin_set_dt(&chip_cfg->pch_pwrok, 1);
		LOG_DBG("Set PCH_PWROK\n");
	}

	return 0;
}

int intel_x86_get_pg_ec_all_sys_pwrgd(const struct common_pwrseq_config
				      *com_cfg)
{
	if (power_has_signals(IN_PCH_SLP_S3_DEASSERTED) == 0) {
		LOG_WRN("SLP_S3 is 0");
		return 0;
	}
	if (gpio_pin_get_dt(&com_cfg->all_sys_pwrgd) == 0) {
		LOG_WRN("ALL_SYS_PWRGD is 0");
		return 0;
	}
	if (pp1p05_pgood == 0){
		LOG_WRN("PP1050_PROC is 0");
		return 0;
	}
	return 1;
}

int power_signal_is_asserted_override(enum power_signal signal)
{
	if (signal == X86_DSW_PWROK)
		return intel_x86_get_pg_ec_dsw_pwrok();

	return 0;
}

static void pp3300_a_pgood_cb(const struct device *dev,
			      const struct sensor_trigger *trigger)
{
	struct sensor_value val;

	if (dev == pp3300_s5_on_dev) {
		atomic_set_bit(&pp3300_a_pgood, 0);

		/* Disable this interrupt while it's asserted. */
		val.val1 = false;
		sensor_attr_set(pp3300_s5_on_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);

		/* Enable the voltage low interrupt. */
		val.val1 = true;
		sensor_attr_set(pp3300_s5_off_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);
		LOG_DBG("PP3300 HIGH");
	}
	else if (dev == pp3300_s5_off_dev) {
		atomic_clear_bit(&pp3300_a_pgood, 0);

		/* Disable this interrupt while it's asserted. */
		val.val1 = false;
		sensor_attr_set(pp3300_s5_off_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);

		/* Enable the voltage high interrupt. */
		val.val1 = true;
		sensor_attr_set(pp3300_s5_on_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);
		LOG_DBG("PP3300 LOW");
	} else {
		LOG_WRN("PP3300 Spurious");
	}
	power_update_signals();
}

static void pp1p05_good_cb(const struct device *dev,
			   const struct sensor_trigger *trigger)
{
	struct sensor_value val;

	if (dev == pp1p05_on_dev) {
		atomic_set_bit(&pp1p05_pgood, 0);

		/* Disable this interrupt while it's asserted. */
		val.val1 = false;
		sensor_attr_set(pp1p05_on_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);

		/* Enable the voltage low interrupt. */
		val.val1 = true;
		sensor_attr_set(pp1p05_off_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);
		LOG_DBG("PP1P05 HIGH");
	}
	else if (dev == pp1p05_off_dev) {
		atomic_clear_bit(&pp1p05_pgood, 0);

		/* Disable this interrupt while it's asserted. */
		val.val1 = false;
		sensor_attr_set(pp1p05_off_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);

		/* Enable the voltage high interrupt. */
		val.val1 = true;
		sensor_attr_set(pp1p05_on_dev, SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT, &val);
		LOG_DBG("PP1P05 LOW");
	} else {
		LOG_WRN("PP1P05 Spurious");
	}
	power_update_signals();
}

static int baseboard_init(const struct device *device)
{

	struct sensor_value val;
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_THRESHOLD,
		.chan = SENSOR_CHAN_VOLTAGE
	};

	sensor_trigger_set(pp3300_s5_on_dev, &trig, pp3300_a_pgood_cb);
	val.val1 = true;
	sensor_attr_set(pp3300_s5_on_dev, SENSOR_CHAN_VOLTAGE,
		SENSOR_ATTR_ALERT, &val);
	sensor_trigger_set(pp3300_s5_off_dev, &trig, pp3300_a_pgood_cb);

	sensor_trigger_set(pp1p05_on_dev, &trig, pp1p05_good_cb);
	sensor_attr_set(pp1p05_on_dev, SENSOR_CHAN_VOLTAGE,
		SENSOR_ATTR_ALERT, &val);
	sensor_trigger_set(pp1p05_off_dev, &trig, pp1p05_good_cb);
	return 0;
}
SYS_INIT(baseboard_init, POST_KERNEL, 55);
