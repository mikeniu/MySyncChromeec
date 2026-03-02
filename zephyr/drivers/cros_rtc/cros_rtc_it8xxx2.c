/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_rtc

#include "rtc.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

#include <drivers/cros_rtc.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_rtc, LOG_LEVEL_DBG);

enum rtc_data_mode {
	RTC_DAM_BCD,
	RTC_DAM_BIN,
};

static const char *const rtc_data_mode_str[] = {
	[RTC_DAM_BCD] = "BCD",
	[RTC_DAM_BIN] = "Binary",
};

struct cros_rtc_ite_config {
	mm_reg_t reg_ec2i;
	mm_reg_t reg_gpiogcr;
	mm_reg_t reg_gctrl;
	/* RTC alarm irq */
	void (*irq_config_func)(const struct device *dev);
	int alarm_irq;
	int alarm_flag;
	enum rtc_data_mode data_mode;
	bool daylight_saving;
};

struct cros_rtc_ite_data {
	struct k_sem sem;
	cros_rtc_alarm_callback_t alarm_callback;
};

/* EC2I access index/data port */
enum rtc_ec2i_access {
	/* Bank0 */
	RTC_EC2I_ACCESS_B0_INDEX = 0,
	RTC_EC2I_ACCESS_B0_DATA = 1,
};

/* (12xxh) EC access to host controlled modules (EC2I Bridge) */
/* 0x00: Indirect host I/O address register */
#define RTC_EC2I_IHIOA 0x00
/* 0x01: Indirect host data register */
#define RTC_EC2I_IHD 0x01
/* 0x02: Lock super I/O host access register */
#define RTC_EC2I_LSIOHA 0x02
/* 0x03: Super I/O access lock violation register */
#define RTC_EC2I_SIOLV 0x03
/* 0x04: EC to I-Bus modules access enable register */
#define RTC_EC2I_IBMAE 0x04
/* Real-time clock (RTC) EC access enable */
#define RTC_EC2I_IBMAE_RTCAE BIT(1)
/* 0x05: I-Bus control register */
#define RTC_EC2I_IBCTL 0x05
/* EC write to I-Bus */
#define RTC_EC2I_IBCTL_CWIB BIT(2)
/* EC read from I-Bus */
#define RTC_EC2I_IBCTL_CRIB BIT(1)
#define RTC_EC2I_IBCTL_CRWIB (RTC_EC2I_IBCTL_CRIB | RTC_EC2I_IBCTL_CWIB)
/* EC to I-Bus access enabled */
#define RTC_EC2I_IBCTL_CSAE BIT(0)

#define RTC_EC2I_WAIT_STATUS_TIMEOUT_US (USEC_PER_MSEC * 10)

/* (16xxh)it51xxx, it81xxx, (3exxh)it82xx2: gpiogcr registers definition */
#define GPIOGCR_RTC_CLKSRC 0xfa
#define RTC_CLKSRC_CRYSTAL BIT(7)
#define RTC_CLKSRC_OSCILLATOR BIT(3)
#define RTC_CLKSRC_SEL BIT(0)

/* (20xxh) gctrl registers definition */
#define GCTRL_RSTS 0x06
#define RSTS_HGRST BIT(3)

/* Host view register map via index-data I/O pair, RTC bank 0 */
/* 0x00: Seconds register */
#define RTC_SECREG 0x00
/* 0x01: Seconds alarm 1 register */
#define RTC_SECA1REG 0x01
/* 0x02: Minutes register */
#define RTC_MINREG 0x02
/* 0x03: Minutes alarm 1 register */
#define RTC_MINA1REG 0x03
/* 0x04: Hours register */
#define RTC_HRREG 0x04
/* 0x05: Hours alarm 1 register */
#define RTC_HRA1REG 0x05
/* 0x06: Day of week register */
#define RTC_DOWREG 0x06
/* 0x07: Day of month register */
#define RTC_DOMREG 0x07
/* 0x08: Month register */
#define RTC_MONREG 0x08
/* 0x09: Year register */
#define RTC_YRREG 0x09
/* 0x0a: RTC control register A */
#define RTC_CTLREGA 0x0A
#define CTLREGA_DICCTL(n) FIELD_PREP(GENMASK(6, 4), n)
enum ctlrega_dicctl {
	/* Normal operation */
	NORMAL_OP = 2,
	/* Divider chain reset */
	DIV_CHAIN_RST = 6,
};
/* 0x0b: RTC control register B */
#define RTC_CTLREGB 0x0B
#define RTC_CTLREGB_SM BIT(7)
#define RTC_CTLREGB_PIE BIT(6)
#define RTC_CTLREGB_A1IE BIT(5)
#define RTC_CTLREGB_UEIE BIT(4)
#define RTC_CTLREGB_A2IE BIT(3)
#define RTC_CTLREGB_DAM BIT(2)
#define RTC_CTLREGB_HRM BIT(1)
#define RTC_CTLREGB_DS BIT(0)
/* 0x0c: RTC control register C */
#define RTC_CTLREGC 0x0C
#define RTC_CTLREGC_A1IF BIT(5)
#define RTC_CTLREGC_A2IF BIT(3)
/* 0x0e: Day of week alarm 1 register */
#define RTC_DOWA1REG 0x0E
/* 0x49: Day of month alarm 1 register */
#define RTC_DOMA1REG 0x49
/* 0x4a: Month alarm 1 register */
#define RTC_MONA1REG 0x4a

/* struct tm start year: 1900 */
#define TM_YEAR_REF 1900U
/* RTC ITE start year: 2000 */
#define RTC_ITE_MIN_YEAR 2000U
/* RTC ITE end year: 2099 */
#define RTC_ITE_MAX_YEAR 2099U

static uint8_t rtc_it8xxx2_ctlregb_set(const struct device *dev, uint8_t ctlreg)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	/* Data mode configuration: data-format: 0 = BCD, 1 = BIN */
	ctlreg |= (config->data_mode == RTC_DAM_BCD) ? 0 : RTC_CTLREGB_DAM;
	/* Daylight saving control */
	ctlreg |= config->daylight_saving ? RTC_CTLREGB_DS : 0;

	return ctlreg;
}

static void rtc_it8xxx2_enable_alarm_irq(const struct device *dev)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	/* Configure interrupt polarity */
	ite_intc_irq_polarity_set(config->alarm_irq, config->alarm_flag);
	/* Clear pending interrupt status (W/C) */
	ite_intc_isr_clear(config->alarm_irq);
	/* Enable IRQ */
	irq_enable(config->alarm_irq);
}

static inline void ec2i_ite_wait_status_cleared(const struct device *dev,
						uint8_t mask)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	if (!WAIT_FOR(!(sys_read8(config->reg_ec2i + RTC_EC2I_IBCTL) & mask),
		      RTC_EC2I_WAIT_STATUS_TIMEOUT_US, )) {
		LOG_ERR("%s: Timeout waiting for status", __func__);
		return;
	}
}

static inline void ec2i_ite_write_rtc_bank(const struct device *dev,
					   enum rtc_ec2i_access sel,
					   uint8_t data)
{
	const struct cros_rtc_ite_config *config = dev->config;

	/* Wait that both CWIB and CRIB bits in IBCTL register are cleared. */
	ec2i_ite_wait_status_cleared(dev, RTC_EC2I_IBCTL_CRWIB);

	/* EC access to the RTC registers is enabled */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBMAE) |
			   RTC_EC2I_IBMAE_RTCAE,
		   config->reg_ec2i + RTC_EC2I_IBMAE);
	/* Set indirect host I/O offset. */
	sys_write8(sel, config->reg_ec2i + RTC_EC2I_IHIOA);
	/* Write the data to IHD register */
	sys_write8(data, config->reg_ec2i + RTC_EC2I_IHD);
	/* bit0: EC to I-Bus access enabled. */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBCTL) |
			   RTC_EC2I_IBCTL_CSAE,
		   config->reg_ec2i + RTC_EC2I_IBCTL);
	/* Wait the CWIB bit in IBCTL cleared. */
	ec2i_ite_wait_status_cleared(dev, RTC_EC2I_IBCTL_CWIB);

	/* EC access to the RTC registers is disabled */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBMAE) &
			   ~RTC_EC2I_IBMAE_RTCAE,
		   config->reg_ec2i + RTC_EC2I_IBMAE);
	/* Disable EC to I-Bus access. */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBCTL) &
			   ~RTC_EC2I_IBCTL_CSAE,
		   config->reg_ec2i + RTC_EC2I_IBCTL);
}

static inline uint8_t ec2i_ite_read_rtc_bank(const struct device *dev,
					     enum rtc_ec2i_access sel)
{
	const struct cros_rtc_ite_config *config = dev->config;
	uint8_t data;

	/* Wait that both CWIB and CRIB bits in IBCTL register are cleared. */
	ec2i_ite_wait_status_cleared(dev, RTC_EC2I_IBCTL_CRWIB);

	/* Set indirect host I/O offset. */
	sys_write8(sel, config->reg_ec2i + RTC_EC2I_IHIOA);
	/* EC access to the RTC registers is enabled */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBMAE) |
			   RTC_EC2I_IBMAE_RTCAE,
		   config->reg_ec2i + RTC_EC2I_IBMAE);
	/* bit0: EC to I-Bus access enabled. */
	sys_write8(RTC_EC2I_IBCTL_CRIB | RTC_EC2I_IBCTL_CSAE,
		   config->reg_ec2i + RTC_EC2I_IBCTL);
	/* Wait the CWIB bit in IBCTL cleared. */
	ec2i_ite_wait_status_cleared(dev, RTC_EC2I_IBCTL_CRIB);
	/* Read the data to IHD register */
	data = sys_read8(config->reg_ec2i + RTC_EC2I_IHD);

	/* EC access to the RTC registers is disabled */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBMAE) &
			   ~RTC_EC2I_IBMAE_RTCAE,
		   config->reg_ec2i + RTC_EC2I_IBMAE);
	/* Disable EC to I-Bus access. */
	sys_write8(sys_read8(config->reg_ec2i + RTC_EC2I_IBCTL) &
			   ~RTC_EC2I_IBCTL_CSAE,
		   config->reg_ec2i + RTC_EC2I_IBCTL);

	return data;
}

static inline void rtc_ec2i_ite_write(const struct device *dev, uint8_t index,
				      uint8_t data)
{
	/* Set index */
	ec2i_ite_write_rtc_bank(dev, RTC_EC2I_ACCESS_B0_INDEX, index);
	/* Write data */
	ec2i_ite_write_rtc_bank(dev, RTC_EC2I_ACCESS_B0_DATA, data);
}

static inline uint8_t rtc_ec2i_ite_read(const struct device *dev, uint8_t index)
{
	/* Set index */
	ec2i_ite_write_rtc_bank(dev, RTC_EC2I_ACCESS_B0_INDEX, index);
	/* Read data */
	return ec2i_ite_read_rtc_bank(dev, RTC_EC2I_ACCESS_B0_DATA);
}

static inline uint8_t rtc_decode(const struct device *dev, uint8_t raw)
{
	const struct cros_rtc_ite_config *config = dev->config;

	/* Convert value according to BCD data format mode */
	if (config->data_mode == RTC_DAM_BCD) {
		return bcd2bin(raw);
	} else {
		return raw;
	}
}

static inline uint8_t rtc_encode(const struct device *dev, uint8_t val)
{
	const struct cros_rtc_ite_config *config = dev->config;

	/* Convert value according to BCD data format mode */
	if (config->data_mode == RTC_DAM_BCD) {
		return bin2bcd(val);
	} else {
		return val;
	}
}

static void rtc_it8xxx2_isr(const void *arg)
{
	const struct device *dev = arg;
	struct cros_rtc_ite_data *data = dev->data;
	uint8_t ctlregc = rtc_ec2i_ite_read(dev, RTC_CTLREGC);

	if (ctlregc & RTC_CTLREGC_A1IF) {
		LOG_DBG("Alarm1 %s", __func__);
		/* Call callback function */
		if (data->alarm_callback) {
			data->alarm_callback(dev);
		}
	}
}

static int cros_rtc_it8xxx2_configure(const struct device *dev,
				      cros_rtc_alarm_callback_t callback)
{
	struct cros_rtc_ite_data *data = dev->data;

	if (callback == NULL) {
		return -EINVAL;
	}

	data->alarm_callback = callback;

	return 0;
}

static int cros_rtc_it8xxx2_get_value(const struct device *dev, uint32_t *value)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;

	k_sem_take(&data->sem, K_FOREVER);

	/* Seconds (0-59) */
	tm_val.tm_sec = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_SECREG));
	/* Minutes (0-59) */
	tm_val.tm_min = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_MINREG));
	/* Hours (0-23, 12 or 24-hour mode) */
	tm_val.tm_hour = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_HRREG));
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday =
		rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_DOWREG)) - 1;
	/* Day of month (1-31) */
	tm_val.tm_mday = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_DOMREG));
	/* Month: tm_mon is 0-11, RTC stores 1-12 */
	tm_val.tm_mon = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_MONREG)) - 1;
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099  */
	tm_val.tm_year =
		rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_YRREG)) + 100;
	/* Set tm_isdst based on RTC daylight saving configuration */
	tm_val.tm_isdst = config->daylight_saving ? 1 : 0;
	/* There is no day of the year support for RTC */
	tm_val.tm_yday = -1;

	k_sem_give(&data->sem);
	/*
	 * Convert struct tm to epoch time (UTC)
	 * Epoch = seconds since 1970-01-01 00:00:00 UTC
	 */
	*value = (uint32_t)timeutil_timegm(&tm_val);

	LOG_DBG("RTC get current time (UTC): %04d-%02d-%02d %02d:%02d:%02d "
		"(wday=%d, epoch=%u, DST=%sable)",
		tm_val.tm_year + TM_YEAR_REF, tm_val.tm_mon + 1, tm_val.tm_mday,
		tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, tm_val.tm_wday,
		*value, config->daylight_saving ? "en" : "dis");

	return 0;
}

static int cros_rtc_it8xxx2_set_value(const struct device *dev, uint32_t value)
{
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;
	int year;
	uint8_t ctlregb;
	time_t sec_val = (time_t)value;

	/* Decompose epoch seconds into UTC time fields (struct tm) */
	gmtime_r(&sec_val, &tm_val);

	year = tm_val.tm_year + TM_YEAR_REF;
	/* RTC supports only years 2000-2099 */
	if (year < RTC_ITE_MIN_YEAR || year > RTC_ITE_MAX_YEAR) {
		LOG_ERR("RTC year %d out of HW range (%d-%d)", year,
			RTC_ITE_MIN_YEAR, RTC_ITE_MAX_YEAR);
		return -ERANGE;
	}

	k_sem_take(&data->sem, K_FOREVER);

	/* Divider chain control: [6:4]=010b: normal operation */
	rtc_ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	ctlregb = rtc_ec2i_ite_read(dev, RTC_CTLREGB);
	/* Set mode enable (required before updating RTC registers) */
	rtc_ec2i_ite_write(dev, RTC_CTLREGB, RTC_CTLREGB_SM | ctlregb);

	/* Configure RTC control register B (data/hour/DST mode) */
	ctlregb = rtc_it8xxx2_ctlregb_set(dev, ctlregb);
	rtc_ec2i_ite_write(dev, RTC_CTLREGB, ctlregb);

	/* Seconds (0-59) */
	rtc_ec2i_ite_write(dev, RTC_SECREG, rtc_encode(dev, tm_val.tm_sec));
	/* Minutes (0-59) */
	rtc_ec2i_ite_write(dev, RTC_MINREG, rtc_encode(dev, tm_val.tm_min));
	/* Hours (0-23, 12 or 24-hour mode) */
	rtc_ec2i_ite_write(dev, RTC_HRREG, rtc_encode(dev, tm_val.tm_hour));
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	rtc_ec2i_ite_write(dev, RTC_DOWREG,
			   rtc_encode(dev, tm_val.tm_wday + 1));
	/* Day of month (1-31) */
	rtc_ec2i_ite_write(dev, RTC_DOMREG, rtc_encode(dev, tm_val.tm_mday));
	/* Month: tm_mon (0-11), RTC stores (1-12) */
	rtc_ec2i_ite_write(dev, RTC_MONREG, rtc_encode(dev, tm_val.tm_mon + 1));
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099 */
	rtc_ec2i_ite_write(dev, RTC_YRREG,
			   rtc_encode(dev, tm_val.tm_year - 100));

	k_sem_give(&data->sem);

	LOG_DBG("RTC set time (UTC): %04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
		tm_val.tm_year + TM_YEAR_REF, tm_val.tm_mon + 1, tm_val.tm_mday,
		tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, tm_val.tm_wday);

	return 0;
}

static int cros_rtc_it8xxx2_get_alarm(const struct device *dev,
				      uint32_t *seconds, uint32_t *microseconds)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;

	k_sem_take(&data->sem, K_FOREVER);
	/*
	 * Read alarm registers from RTC hardware. These registers are stored
	 * in BCD format. Convert them to binary before using struct tm
	 */
	/* Alarm seconds (0-59) */
	tm_val.tm_sec = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_SECA1REG));
	/* Alarm minutes (0-59) */
	tm_val.tm_min = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_MINA1REG));
	/* Alarm hours (0-23, 12 or 24-hour mode) */
	tm_val.tm_hour = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_HRA1REG));
	/* Alarm day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday =
		rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_DOWA1REG)) - 1;
	/* Alarm day of month (1-31) */
	tm_val.tm_mday = rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_DOMA1REG));
	/* Alarm month: tm_mon (0-11), RTC stores (1-12) */
	tm_val.tm_mon =
		rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_MONA1REG)) - 1;
	/* Set tm_isdst based on RTC daylight saving configuration */
	tm_val.tm_isdst = config->daylight_saving ? 1 : 0;
	/* There is no day of the year support for RTC */
	tm_val.tm_yday = -1;
	/*
	 * RTC alarm registers do not include a year field.
	 * The year is borrowed from the current RTC time register (RTC_YRREG)
	 * for epoch conversion only.
	 */
	tm_val.tm_year =
		rtc_decode(dev, rtc_ec2i_ite_read(dev, RTC_YRREG)) + 100;

	k_sem_give(&data->sem);
	/*
	 * Convert struct tm to epoch time (UTC)
	 * Epoch = seconds since 1970-01-01 00:00:00 UTC
	 */
	*seconds = (uint32_t)timeutil_timegm(&tm_val);

	/* RTC alarm does not support sub-second resolution */
	*microseconds = 0;

	LOG_DBG("RTC get alarm time (UTC): %02d-%02d %02d:%02d:%02d "
		"(wday=%d, epoch=%u)",
		tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_hour,
		tm_val.tm_min, tm_val.tm_sec, tm_val.tm_wday, *seconds);

	return 0;
}

static int cros_rtc_it8xxx2_set_alarm(const struct device *dev,
				      uint32_t seconds, uint32_t microseconds)
{
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;
	time_t alarm_time;
	uint32_t sec_time, current_time;
	uint8_t ctlregb;

	/* Convert input seconds + microseconds into total seconds */
	sec_time = seconds + microseconds / USEC_PER_SEC;

	/* Get current RTC time for year boundary check */
	cros_rtc_it8xxx2_get_value(dev, &current_time);
	/*
	 * RTC alarm HW only matches up to month granularity (no year register).
	 * Alarm spanning more than 1 year may fire at wrong year.
	 */
	if ((sec_time - current_time) >= SECS_PER_YEAR) {
		LOG_WRN("RTC alarm exceeds 1 year; HW has no year field, "
			"alarm may fire early");
		return -ENOTSUP;
	}

	alarm_time = (time_t)sec_time;
	/* Decompose epoch seconds into UTC time fields (struct tm) */
	gmtime_r(&alarm_time, &tm_val);

	k_sem_take(&data->sem, K_FOREVER);

	/* Divider chain control: [6:4]=010b: normal operation */
	rtc_ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	/* Alarm seconds (0-59) */
	rtc_ec2i_ite_write(dev, RTC_SECA1REG, rtc_encode(dev, tm_val.tm_sec));
	/* Alarm minutes (0-59) */
	rtc_ec2i_ite_write(dev, RTC_MINA1REG, rtc_encode(dev, tm_val.tm_min));
	/* Alarm hours (0-23, 12 or 24-hour mode) */
	rtc_ec2i_ite_write(dev, RTC_HRA1REG, rtc_encode(dev, tm_val.tm_hour));
	/* Alarm weekday: tm_wday 0-6 Sunday = 0 */
	rtc_ec2i_ite_write(dev, RTC_DOWA1REG,
			   rtc_encode(dev, tm_val.tm_wday + 1));
	/* Alarm day of month (1-31) */
	rtc_ec2i_ite_write(dev, RTC_DOMA1REG, rtc_encode(dev, tm_val.tm_mday));
	/* Alarm month: RTC: 1-12 struct tm_mon: 0-11 */
	rtc_ec2i_ite_write(dev, RTC_MONA1REG,
			   rtc_encode(dev, tm_val.tm_mon + 1));

	/* Setting interrupt */
	rtc_it8xxx2_enable_alarm_irq(dev);

	ctlregb = rtc_ec2i_ite_read(dev, RTC_CTLREGB);
	/* Configure RTC control register B (data/hour/DS mode) */
	ctlregb = rtc_it8xxx2_ctlregb_set(dev, ctlregb);
	/* Setting alarm1 interrupt */
	ctlregb |= RTC_CTLREGB_A1IE;
	/* Alarm interrupt enable */
	rtc_ec2i_ite_write(dev, RTC_CTLREGB, ctlregb);

	k_sem_give(&data->sem);

	return 0;
}

static int cros_rtc_it8xxx2_reset_alarm(const struct device *dev)
{
	const struct cros_rtc_ite_config *config = dev->config;
	uint8_t ctlregb;

	LOG_DBG("RTC reset alarm time.");

	/* Disable IRQ */
	irq_disable(config->alarm_irq);

	/* Divider chain control: [6:4]=11xb: divider chain reset */
	rtc_ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(DIV_CHAIN_RST));

	/* Setting alarm1 or alarm2 interrupt */
	ctlregb = rtc_ec2i_ite_read(dev, RTC_CTLREGB);
	ctlregb &= ~RTC_CTLREGB_A1IE;
	/* Alarm interrupt disable */
	rtc_ec2i_ite_write(dev, RTC_CTLREGB, ctlregb);

	return 0;
}

/* cros ec RTC driver registration */
static DEVICE_API(cros_rtc, cros_rtc_it8xxx2_driver_api) = {
	.configure = cros_rtc_it8xxx2_configure,
	.get_value = cros_rtc_it8xxx2_get_value,
	.set_value = cros_rtc_it8xxx2_set_value,
	.get_alarm = cros_rtc_it8xxx2_get_alarm,
	.set_alarm = cros_rtc_it8xxx2_set_alarm,
	.reset_alarm = cros_rtc_it8xxx2_reset_alarm,
};

static int cros_rtc_it8xxx2_init(const struct device *dev)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;

	LOG_INF("RTC init: Data mode=%s, Daylight saving=%d",
		rtc_data_mode_str[config->data_mode], config->daylight_saving);

	/*
	 * bit[3]: The reset source of PNPCFG is RSTPNP bit in RSTCH
	 * register and WRST#.
	 */
	sys_write8(sys_read8(config->reg_gctrl + GCTRL_RSTS) & ~RSTS_HGRST,
		   config->reg_gctrl + GCTRL_RSTS);

	/*
	 * Select RTC clock source
	 * Enable external crystal oscillator for RTC
	 */
	sys_write8(RTC_CLKSRC_CRYSTAL,
		   config->reg_gpiogcr + GPIOGCR_RTC_CLKSRC);

	/* Divider chain control: [6:4]=010b: normal operation */
	rtc_ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	/* Configure RTC timer interrupt for this instance */
	config->irq_config_func(dev);

	/* Initialize mutex for RTC */
	k_sem_init(&data->sem, 1, 1);

	return 0;
}

#define RTC_ITE_INIT(inst)                                                  \
	static void cros_rtc_ite_irq_config_func_##inst(                    \
		const struct device *dev);                                  \
                                                                            \
	static const struct cros_rtc_ite_config cros_rtc_ite_cfg_##inst = { \
		.reg_ec2i = DT_INST_REG_ADDR_BY_IDX(inst, 0),               \
		.reg_gpiogcr = DT_INST_REG_ADDR_BY_IDX(inst, 1),            \
		.reg_gctrl = DT_INST_REG_ADDR_BY_IDX(inst, 2),              \
		.irq_config_func = cros_rtc_ite_irq_config_func_##inst,     \
		.alarm_irq = DT_INST_IRQN(inst),                            \
		.alarm_flag = DT_INST_IRQ(inst, flags),                     \
		.data_mode = DT_INST_ENUM_IDX(inst, data_mode),             \
		.daylight_saving = DT_INST_PROP(inst, daylight_saving),     \
	};                                                                  \
                                                                            \
	static struct cros_rtc_ite_data cros_rtc_ite_data_##inst;           \
                                                                            \
	DEVICE_DT_INST_DEFINE(inst, cros_rtc_it8xxx2_init, NULL,            \
			      &cros_rtc_ite_data_##inst,                    \
			      &cros_rtc_ite_cfg_##inst, POST_KERNEL,        \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,           \
			      &cros_rtc_it8xxx2_driver_api);                \
                                                                            \
	static void cros_rtc_ite_irq_config_func_##inst(                    \
		const struct device *dev)                                   \
	{                                                                   \
		IRQ_CONNECT(DT_INST_IRQN(inst), 0, rtc_it8xxx2_isr,         \
			    DEVICE_DT_INST_GET(inst),                       \
			    DT_INST_IRQ(inst, flags));                      \
	}
DT_INST_FOREACH_STATUS_OKAY(RTC_ITE_INIT)
