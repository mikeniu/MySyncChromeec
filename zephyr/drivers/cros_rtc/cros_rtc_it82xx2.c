/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it82xx2_cros_rtc

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
enum rtc_hour_mode {
	RTC_HRM_12,
	RTC_HRM_24,
};
enum rtc_bank_sel {
	B0,
	B1,
};

static const char *const rtc_data_mode_str[] = {
	[RTC_DAM_BCD] = "BCD",
	[RTC_DAM_BIN] = "Binary",
};
static const char *const rtc_hour_mode_str[] = {
	[RTC_HRM_12] = "12h",
	[RTC_HRM_24] = "24h",
};
static const char *const rtc_alarm_sel_str[] = {
	[B0] = "ALARM1",
	[B1] = "ALARM2",
};

struct cros_rtc_ite_config {
	mm_reg_t base;
	/* RTC alarm irq */
	void (*irq_config_func)(const struct device *dev);
	int alarm_irq[2];
	int alarm_flag[2];
	enum rtc_data_mode data_mode;
	enum rtc_hour_mode hour_mode;
	enum rtc_bank_sel alarm_sel;
	bool daylight_saving;
};

struct cros_rtc_ite_data {
	struct k_sem sem;
	cros_rtc_alarm_callback_t alarm_callback;
};

/* EC2I access index/data port */
enum ec2i_access {
	/* Bank0 */
	EC2I_ACCESS_B0_INDEX = 0,
	EC2I_ACCESS_B0_DATA = 1,
	/* Bank1 */
	EC2I_ACCESS_B1_INDEX = 2,
	EC2I_ACCESS_B1_DATA = 3,
};

/* (12xxh) EC access to host controlled modules (EC2I Bridge) */
/* 0x00: Indirect host I/O address register */
#define EC2I_IHIOA 0x00
/* 0x01: Indirect host data register */
#define EC2I_IHD 0x01
/* 0x02: Lock super I/O host access register */
#define EC2I_LSIOHA 0x02
/* 0x03: Super I/O access lock violation register */
#define EC2I_SIOLV 0x03
/* 0x04: EC to I-Bus modules access enable register */
#define EC2I_IBMAE 0x04
/* Real-time clock (RTC) EC access enable */
#define EC2I_IBMAE_RTCAE BIT(1)
/* 0x05: I-Bus control register */
#define EC2I_IBCTL 0x05
/* EC write to I-Bus */
#define EC2I_IBCTL_CWIB BIT(2)
/* EC read from I-Bus */
#define EC2I_IBCTL_CRIB BIT(1)
#define EC2I_IBCTL_CRWIB (EC2I_IBCTL_CRIB | EC2I_IBCTL_CWIB)
/* EC to I-Bus access enabled */
#define EC2I_IBCTL_CSAE BIT(0)

#define EC2I_WAIT_STATUS_TIMEOUT_US (USEC_PER_MSEC * 10)

/* (16xxh)it51xxx, it81xxx, (3exxh)it82xx2: gpiogcr registers definition */
static mm_reg_t gpiogcr_base = DT_REG_ADDR(DT_NODELABEL(gpiogcr));
#define GPIOGCR_RTC_CLKSRC 0xfa
#define RTC_CLKSRC_CRYSTAL BIT(7)
#define RTC_CLKSRC_OSCILLATOR BIT(3)
#define RTC_CLKSRC_SEL BIT(0)

/* (20xxh) gctrl registers definition */
static mm_reg_t gctrl_base = DT_REG_ADDR(DT_NODELABEL(gctrl));
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
#define CTLREGB_SM BIT(7)
#define CTLREGB_PIE BIT(6)
#define CTLREGB_A1IE BIT(5)
#define CTLREGB_UEIE BIT(4)
#define CTLREGB_A2IE BIT(3)
#define CTLREGB_DAM BIT(2)
#define CTLREGB_HRM BIT(1)
#define CTLREGB_DS BIT(0)
/* 0x0c: RTC control register C */
#define RTC_CTLREGC 0x0C
#define CTLREGC_A1IF BIT(5)
#define CTLREGC_A2IF BIT(3)
/* 0x0e: Day of week alarm 1 register */
#define RTC_DOWA1REG 0x0E
/* 0x49: Day of month alarm 1 register */
#define RTC_DOMA1REG 0x49
/* 0x4a: Month alarm 1 register */
#define RTC_MONA1REG 0x4a

/* Host view register map via index-data I/O pair, RTC bank 1 */
/* 0x00: Seconds alarm 2 register */
#define RTC_SECA2REG 0x00
/* 0x01: Minutes alarm 2 register */
#define RTC_MINA2REG 0x01
/* 0x02: Hours alarm 2 register */
#define RTC_HRA2REG 0x02
/* 0x03: Day of month alarm 2 register */
#define RTC_DOMA2REG 0x03
/* 0x04: Month alarm 2 register */
#define RTC_MONA2REG 0x04
/* 0x05: Day of week alarm 2 register */
#define RTC_DOWA2REG 0x05

#define RTC_SECAREG(sel) ((sel) == B0 ? RTC_SECA1REG : RTC_SECA2REG)
#define RTC_MINAREG(sel) ((sel) == B0 ? RTC_MINA1REG : RTC_MINA2REG)
#define RTC_HRAREG(sel) ((sel) == B0 ? RTC_HRA1REG : RTC_HRA2REG)
#define RTC_DOMAREG(sel) ((sel) == B0 ? RTC_DOMA1REG : RTC_DOMA2REG)
#define RTC_MONAREG(sel) ((sel) == B0 ? RTC_MONA1REG : RTC_MONA2REG)
#define RTC_DOWAREG(sel) ((sel) == B0 ? RTC_DOWA1REG : RTC_DOWA2REG)

/* struct tm start year: 1900 */
#define TM_YEAR_REF 1900
/* RTC ITE start year: 2000 */
#define RTC_ITE_START_YEAR 2000
/* RTC ITE end year: 2099 */
#define RTC_ITE_END_YEAR 2099
#define RTC_HOUR_PM BIT(7)

static uint8_t cros_rtc_it82xx2_ctlregb_set(const struct device *dev)
{
	const struct cros_rtc_ite_config *const config = dev->config;
	uint8_t ctlregb;

	/* Data mode configuration: data-format: 0 = BCD, 1 = BIN */
	ctlregb = (config->data_mode == RTC_DAM_BCD) ? 0 : CTLREGB_DAM;
	/* Hour mode configuration: hour-format: 0 = 12-hour, 1 = 24-hour */
	ctlregb |= (config->hour_mode == RTC_HRM_24) ? CTLREGB_HRM : 0;
	/* Daylight saving control */
	ctlregb |= config->daylight_saving ? CTLREGB_DS : 0;

	return ctlregb;
}

static void cros_rtc_it82xx2_enable_alarm_irq(const struct device *dev,
					      enum rtc_bank_sel sel)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	/* Configure interrupt polarity */
	ite_intc_irq_polarity_set(config->alarm_irq[sel],
				  config->alarm_flag[sel]);
	/* Clear pending interrupt status (W/C) */
	ite_intc_isr_clear(config->alarm_irq[sel]);
	/* Enable IRQ */
	irq_enable(config->alarm_irq[sel]);
}

static inline void ec2i_ite_wait_status_cleared(const struct device *dev,
						uint8_t mask)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	if (!WAIT_FOR(!(sys_read8(config->base + EC2I_IBCTL) & mask),
		      EC2I_WAIT_STATUS_TIMEOUT_US, )) {
		LOG_ERR("%s: Timeout waiting for status", __func__);
		return;
	}
}

static inline void ec2i_ite_write_rtc_bank(const struct device *dev,
					   enum ec2i_access sel, uint8_t data)
{
	const struct cros_rtc_ite_config *config = dev->config;

	/* Wait that both CWIB and CRIB bits in IBCTL register are cleared. */
	ec2i_ite_wait_status_cleared(dev, EC2I_IBCTL_CRWIB);

	/* EC access to the RTC registers is enabled */
	sys_write8(sys_read8(config->base + EC2I_IBMAE) | EC2I_IBMAE_RTCAE,
		   config->base + EC2I_IBMAE);
	/* Set indirect host I/O offset. */
	sys_write8(sel, config->base + EC2I_IHIOA);
	/* Write the data to IHD register */
	sys_write8(data, config->base + EC2I_IHD);
	/* bit0: EC to I-Bus access enabled. */
	sys_write8(sys_read8(config->base + EC2I_IBCTL) | EC2I_IBCTL_CSAE,
		   config->base + EC2I_IBCTL);
	/* Wait the CWIB bit in IBCTL cleared. */
	ec2i_ite_wait_status_cleared(dev, EC2I_IBCTL_CWIB);

	/* EC access to the RTC registers is disabled */
	sys_write8(sys_read8(config->base + EC2I_IBMAE) & ~EC2I_IBMAE_RTCAE,
		   config->base + EC2I_IBMAE);
	/* Disable EC to I-Bus access. */
	sys_write8(sys_read8(config->base + EC2I_IBCTL) & ~EC2I_IBCTL_CSAE,
		   config->base + EC2I_IBCTL);
}

static inline uint8_t ec2i_ite_read_rtc_bank(const struct device *dev,
					     enum ec2i_access sel)
{
	const struct cros_rtc_ite_config *config = dev->config;
	uint8_t data;

	/* Wait that both CWIB and CRIB bits in IBCTL register are cleared. */
	ec2i_ite_wait_status_cleared(dev, EC2I_IBCTL_CRWIB);

	/* Set indirect host I/O offset. */
	sys_write8(sel, config->base + EC2I_IHIOA);
	/* EC access to the RTC registers is enabled */
	sys_write8(sys_read8(config->base + EC2I_IBMAE) | EC2I_IBMAE_RTCAE,
		   config->base + EC2I_IBMAE);
	/* bit0: EC to I-Bus access enabled. */
	sys_write8(EC2I_IBCTL_CRIB | EC2I_IBCTL_CSAE,
		   config->base + EC2I_IBCTL);
	/* Wait the CWIB bit in IBCTL cleared. */
	ec2i_ite_wait_status_cleared(dev, EC2I_IBCTL_CRIB);
	/* Read the data to IHD register */
	data = sys_read8(config->base + EC2I_IHD);

	/* EC access to the RTC registers is disabled */
	sys_write8(sys_read8(config->base + EC2I_IBMAE) & ~EC2I_IBMAE_RTCAE,
		   config->base + EC2I_IBMAE);
	/* Disable EC to I-Bus access. */
	sys_write8(sys_read8(config->base + EC2I_IBCTL) & ~EC2I_IBCTL_CSAE,
		   config->base + EC2I_IBCTL);

	return data;
}

static inline void ec2i_ite_write(const struct device *dev, uint8_t index,
				  uint8_t data, enum rtc_bank_sel bank)
{
	enum ec2i_access ec2i_index, ec2i_data;

	ec2i_index = (bank == B0) ? EC2I_ACCESS_B0_INDEX : EC2I_ACCESS_B1_INDEX;
	ec2i_data = (bank == B0) ? EC2I_ACCESS_B0_DATA : EC2I_ACCESS_B1_DATA;

	/* Set index */
	ec2i_ite_write_rtc_bank(dev, ec2i_index, index);
	/* Write data */
	ec2i_ite_write_rtc_bank(dev, ec2i_data, data);
}

static inline uint8_t ec2i_ite_read(const struct device *dev, uint8_t index,
				    enum rtc_bank_sel bank)
{
	enum ec2i_access ec2i_index, ec2i_data;

	ec2i_index = (bank == B0) ? EC2I_ACCESS_B0_INDEX : EC2I_ACCESS_B1_INDEX;
	ec2i_data = (bank == B0) ? EC2I_ACCESS_B0_DATA : EC2I_ACCESS_B1_DATA;

	/* Set index */
	ec2i_ite_write_rtc_bank(dev, ec2i_index, index);
	/* Read data */
	return ec2i_ite_read_rtc_bank(dev, ec2i_data);
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

static inline uint8_t rtc_decode_hour(const struct device *dev, uint8_t raw)
{
	const struct cros_rtc_ite_config *config = dev->config;
	uint8_t hour;
	bool is_pm;

	/* 24h mode: direct decode, output 0-23 */
	if (config->hour_mode == RTC_HRM_24) {
		return rtc_decode(dev, raw);
	}

	/*
	 * 12h mode input:
	 * BCD mode:    AM 0x01-0x12, PM 0x81-0x92
	 * Binary mode: AM 0x01-0x0C, PM 0x81-0x8C
	 */
	/* PM flag in bit 7 */
	is_pm = IS_BIT_SET(raw, 7);
	/* Clear PM bit before decode */
	raw &= ~RTC_HOUR_PM;
	hour = rtc_decode(dev, raw);

	/*
	 * Convert 12h to 24h (0-23) for struct tm:
	 *
	 * 12h  flag -> 24h
	 * 12   AM   -> 0
	 * 1-11 AM   -> 1-11
	 * 12   PM   -> 12
	 * 1-11 PM   -> 13-23
	 */
	if (hour == 12) {
		hour = is_pm ? 12 : 0;
	} else if (is_pm) {
		hour += 12;
	}
	/* 0-23 */
	return hour;
}

static inline uint8_t rtc_encode_hour(const struct device *dev, uint8_t hour)
{
	const struct cros_rtc_ite_config *config = dev->config;

	/* 12h mode: need set bit7 */
	if (config->hour_mode == RTC_HRM_12) {
		/*
		 * Convert 24h (0-23) to 12h representation before encoding:
		 *
		 * 24h  -> 12h   flag
		 *  0   -> 12    AM
		 *  1-11->  1-11 AM
		 * 12   -> 12    PM
		 * 13-23->  1-11 PM
		 *
		 * BCD mode:    AM 0x01-0x12, PM 0x81-0x92
		 * Binary mode: AM 0x01-0x0C, PM 0x81-0x8C
		 */
		bool is_pm = (hour >= 12);
		uint8_t val12 = (hour == 0)  ? 12 :
				(hour <= 12) ? hour :
					       hour - 12;

		uint8_t encoded = rtc_encode(dev, val12);

		/* Set PM flag (bit 7) if PM */
		return is_pm ? (encoded | BIT(7)) : encoded;
	}

	/* 24h mode: direct encode */
	return rtc_encode(dev, hour);
}

static void rtc_it82xx2_isr(const void *arg)
{
	const struct device *dev = arg;
	struct cros_rtc_ite_data *data = dev->data;
	uint8_t ctlregc = ec2i_ite_read(dev, RTC_CTLREGC, B0);

	if (ctlregc & CTLREGC_A1IF) {
		LOG_DBG("Alarm1 %s", __func__);
		/* Call callback function */
		if (data->alarm_callback) {
			data->alarm_callback(dev);
		}
	}

	if (ctlregc & CTLREGC_A2IF) {
		LOG_DBG("Alarm2 %s", __func__);
		/* Call callback function */
		if (data->alarm_callback) {
			data->alarm_callback(dev);
		}
	}
}

static int cros_rtc_it82xx2_configure(const struct device *dev,
				      cros_rtc_alarm_callback_t callback)
{
	struct cros_rtc_ite_data *data = dev->data;

	if (callback == NULL) {
		return -EINVAL;
	}

	data->alarm_callback = callback;

	return 0;
}

static int cros_rtc_it82xx2_get_value(const struct device *dev, uint32_t *value)
{
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;

	k_sem_take(&data->sem, K_FOREVER);

	/* Seconds (0-59) */
	tm_val.tm_sec = rtc_decode(dev, ec2i_ite_read(dev, RTC_SECREG, B0));
	/* Minutes (0-59) */
	tm_val.tm_min = rtc_decode(dev, ec2i_ite_read(dev, RTC_MINREG, B0));
	/* Hours (0-23, 12 or 24-hour mode) */
	tm_val.tm_hour =
		rtc_decode_hour(dev, ec2i_ite_read(dev, RTC_HRREG, B0));
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_DOWREG, B0)) - 1;
	/* Day of month (1-31) */
	tm_val.tm_mday = rtc_decode(dev, ec2i_ite_read(dev, RTC_DOMREG, B0));
	/* Month: tm_mon is 0-11, RTC stores 1-12 */
	tm_val.tm_mon = rtc_decode(dev, ec2i_ite_read(dev, RTC_MONREG, B0)) - 1;
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099  */
	tm_val.tm_year =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_YRREG, B0)) + 100;

	k_sem_give(&data->sem);
	/*
	 * Convert struct tm to epoch time (UTC)
	 * Epoch = seconds since 1970-01-01 00:00:00 UTC
	 */
	*value = (uint32_t)timeutil_timegm(&tm_val);

	return 0;
}

static int cros_rtc_it82xx2_set_value(const struct device *dev, uint32_t value)
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
	if (year < RTC_ITE_START_YEAR || year > RTC_ITE_END_YEAR) {
		LOG_ERR("RTC year %d out of HW range (%d-%d)", year,
			RTC_ITE_START_YEAR, RTC_ITE_END_YEAR);
		return -ERANGE;
	}

	k_sem_take(&data->sem, K_FOREVER);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP), B0);

	/* Set mode enable (required before updating RTC registers) */
	ec2i_ite_write(dev, RTC_CTLREGB, CTLREGB_SM, B0);

	/* Configure RTC control register B (data/hour/DST mode) */
	ctlregb = cros_rtc_it82xx2_ctlregb_set(dev);
	ec2i_ite_write(dev, RTC_CTLREGB, ctlregb, B0);

	/* Seconds (0-59) */
	ec2i_ite_write(dev, RTC_SECREG, rtc_encode(dev, tm_val.tm_sec), B0);
	/* Minutes (0-59) */
	ec2i_ite_write(dev, RTC_MINREG, rtc_encode(dev, tm_val.tm_min), B0);
	/* Hours (0-23, 12 or 24-hour mode) */
	ec2i_ite_write(dev, RTC_HRREG, rtc_encode_hour(dev, tm_val.tm_hour),
		       B0);
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	ec2i_ite_write(dev, RTC_DOWREG, rtc_encode(dev, tm_val.tm_wday + 1),
		       B0);
	/* Day of month (1-31) */
	ec2i_ite_write(dev, RTC_DOMREG, rtc_encode(dev, tm_val.tm_mday), B0);
	/* Month: tm_mon (0-11), RTC stores (1-12) */
	ec2i_ite_write(dev, RTC_MONREG, rtc_encode(dev, tm_val.tm_mon + 1), B0);
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099 */
	ec2i_ite_write(dev, RTC_YRREG, rtc_encode(dev, tm_val.tm_year - 100),
		       B0);

	k_sem_give(&data->sem);

	LOG_DBG("RTC set time (UTC): %04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
		tm_val.tm_year + TM_YEAR_REF, tm_val.tm_mon + 1, tm_val.tm_mday,
		tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec,
		tm_val.tm_wday);

	return 0;
}

static int cros_rtc_it82xx2_get_alarm(const struct device *dev,
				      uint32_t *seconds, uint32_t *microseconds)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;
	enum rtc_bank_sel sel = config->alarm_sel;

	k_sem_take(&data->sem, K_FOREVER);
	/*
	 * Read alarm registers from RTC hardware. These registers are stored
	 * in BCD format. Convert them to binary before using struct tm
	 */
	/* Alarm seconds (0-59) */
	tm_val.tm_sec =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_SECAREG(sel), sel));
	/* Alarm minutes (0-59) */
	tm_val.tm_min =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_MINAREG(sel), sel));
	/* Alarm hours (0-23, 12 or 24-hour mode) */
	tm_val.tm_hour =
		rtc_decode_hour(dev, ec2i_ite_read(dev, RTC_HRAREG(sel), sel));
	/* Alarm day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_DOWAREG(sel), sel)) - 1;
	/* Alarm day of month (1-31) */
	tm_val.tm_mday =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_DOMAREG(sel), sel));
	/* Alarm month: tm_mon (0-11), RTC stores (1-12) */
	tm_val.tm_mon =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_MONAREG(sel), sel)) - 1;

	/*
	 * RTC alarm registers do not include a year field.
	 * The year is borrowed from the current RTC time register (RTC_YRREG)
	 * for epoch conversion only.
	 */
	tm_val.tm_year =
		rtc_decode(dev, ec2i_ite_read(dev, RTC_YRREG, B0)) + 100;

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

static int cros_rtc_it82xx2_set_alarm(const struct device *dev,
				      uint32_t seconds, uint32_t microseconds)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;
	struct tm tm_val;
	uint32_t alarm_time, current_time;
	uint8_t ctlregb;
	enum rtc_bank_sel sel = config->alarm_sel;

	alarm_time = seconds + microseconds / USEC_PER_SEC;

	/* Get current RTC time for year boundary check */
	cros_rtc_it82xx2_get_value(dev, &current_time);
	/*
	 * RTC alarm HW only matches up to month granularity (no year register).
	 * Alarm spanning more than ~1 year may fire at wrong year.
	 */
	if ((alarm_time - current_time) > SECS_PER_YEAR) {
		LOG_WRN("RTC alarm exceeds 1 year; HW has no year field, "
			"alarm may fire early");
		return -ENOTSUP;
	}

	time_t sec_val = (time_t)alarm_time;
	/* Decompose epoch seconds into UTC time fields (struct tm) */
	gmtime_r(&sec_val, &tm_val);

	k_sem_take(&data->sem, K_FOREVER);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP), B0);

	/* Alarm seconds (0-59) */
	ec2i_ite_write(dev, RTC_SECAREG(sel), rtc_encode(dev, tm_val.tm_sec),
		       sel);
	/* Alarm minutes (0-59) */
	ec2i_ite_write(dev, RTC_MINAREG(sel), rtc_encode(dev, tm_val.tm_min),
		       sel);
	/* Alarm hours (0-23, 12 or 24-hour mode) */
	ec2i_ite_write(dev, RTC_HRAREG(sel),
		       rtc_encode_hour(dev, tm_val.tm_hour), sel);
	/* Alarm weekday: tm_wday 0-6 Sunday = 0 */
	ec2i_ite_write(dev, RTC_DOWAREG(sel),
		       rtc_encode(dev, tm_val.tm_wday + 1), sel);
	/* Alarm day of month (1-31) */
	ec2i_ite_write(dev, RTC_DOMAREG(sel), rtc_encode(dev, tm_val.tm_mday),
		       sel);
	/* Alarm month: RTC: 1-12 struct tm_mon: 0-11 */
	ec2i_ite_write(dev, RTC_MONAREG(sel),
		       rtc_encode(dev, tm_val.tm_mon + 1), sel);

	/* Setting interrupt */
	cros_rtc_it82xx2_enable_alarm_irq(dev, sel);

	/* Configure RTC control register B (data/hour/DS mode) */
	ctlregb = cros_rtc_it82xx2_ctlregb_set(dev);
	/* Setting alarm1 or alarm2 interrupt */
	ctlregb |= (config->alarm_sel == B0) ? CTLREGB_A1IE : CTLREGB_A2IE;
	/* Alarm interrupt enable */
	ec2i_ite_write(dev, RTC_CTLREGB, ctlregb, B0);

	k_sem_give(&data->sem);

	return 0;
}

static int cros_rtc_it82xx2_reset_alarm(const struct device *dev)
{
	const struct cros_rtc_ite_config *config = dev->config;
	enum rtc_bank_sel sel = config->alarm_sel;
	uint8_t ctlregb;

	LOG_DBG("RTC reset alarm time.");

	/* Disable IRQ */
	irq_disable(config->alarm_irq[sel]);

	/* Divider chain control: [6:4]=11xb: divider chain reset */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(DIV_CHAIN_RST), B0);

	/* Setting alarm1 or alarm2 interrupt */
	ctlregb = ec2i_ite_read(dev, RTC_CTLREGB, B0);
	if (sel == B0) {
		ctlregb &= ~CTLREGB_A1IE;
	} else {
		ctlregb &= ~CTLREGB_A2IE;
	}
	/* Alarm interrupt disable */
	ec2i_ite_write(dev, RTC_CTLREGB, ctlregb, B0);

	return 0;
}

/* cros ec RTC driver registration */
static DEVICE_API(cros_rtc, cros_rtc_it82xx2_driver_api) = {
	.configure = cros_rtc_it82xx2_configure,
	.get_value = cros_rtc_it82xx2_get_value,
	.set_value = cros_rtc_it82xx2_set_value,
	.get_alarm = cros_rtc_it82xx2_get_alarm,
	.set_alarm = cros_rtc_it82xx2_set_alarm,
	.reset_alarm = cros_rtc_it82xx2_reset_alarm,
};

static int cros_rtc_it82xx2_init(const struct device *dev)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;

	LOG_INF("RTC init: data_mode=%s, hour_mode=%s, alarm_sel=%s",
		rtc_data_mode_str[config->data_mode],
		rtc_hour_mode_str[config->hour_mode],
		rtc_alarm_sel_str[config->alarm_sel]);

	/*
	 * bit[3]: The reset source of PNPCFG is RSTPNP bit in RSTCH
	 * register and WRST#.
	 */
	sys_write8(sys_read8(gctrl_base + GCTRL_RSTS) & ~RSTS_HGRST,
		   gctrl_base + GCTRL_RSTS);

	/*
	 * Select RTC clock source
	 * Enable external crystal oscillator for RTC
	 */
	sys_write8(RTC_CLKSRC_CRYSTAL, gpiogcr_base + GPIOGCR_RTC_CLKSRC);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP), B0);

	/* Configure RTC timer interrupt for this instance */
	config->irq_config_func(dev);

	/* Initialize mutex for RTC */
	k_sem_init(&data->sem, 1, 1);

	return 0;
}

#define CROS_RTC_ITE_IRQ_INIT(inst, idx)                                       \
	IRQ_CONNECT(DT_INST_IRQN_BY_IDX(inst, idx), 0, rtc_it82xx2_isr,        \
		    DEVICE_DT_INST_GET(inst),                                  \
		    DT_INST_IRQ_BY_IDX(inst, idx, flags));

#define RTC_ITE_INIT(inst)                                                     \
	static void cros_rtc_ite_irq_config_func_##inst(                       \
		const struct device *dev);                                     \
                                                                               \
	static const struct cros_rtc_ite_config cros_rtc_ite_cfg_##inst = {    \
		.base = DT_INST_REG_ADDR(inst),                                \
		.irq_config_func = cros_rtc_ite_irq_config_func_##inst,        \
		.alarm_irq  = {                                                \
			DT_INST_IRQN_BY_IDX(inst, 0),                          \
			DT_INST_IRQN_BY_IDX(inst, 1),                          \
		},                                                             \
		.alarm_flag = {                                                \
			DT_INST_IRQ_BY_IDX(inst, 0, flags),                    \
			DT_INST_IRQ_BY_IDX(inst, 1, flags),                    \
		},                                                             \
		.data_mode = DT_INST_ENUM_IDX(inst, data_mode),                \
		.hour_mode = DT_INST_ENUM_IDX(inst, hour_mode),                \
		.alarm_sel = DT_INST_ENUM_IDX(inst, alarm_sel),                \
		.daylight_saving = DT_INST_PROP(inst, daylight_saving),        \
	};                                                                     \
                                                                               \
	static struct cros_rtc_ite_data cros_rtc_ite_data_##inst;              \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, cros_rtc_it82xx2_init, NULL,               \
			      &cros_rtc_ite_data_##inst,                       \
			      &cros_rtc_ite_cfg_##inst, POST_KERNEL,           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,              \
			      &cros_rtc_it82xx2_driver_api);                   \
                                                                               \
	static void cros_rtc_ite_irq_config_func_##inst(                       \
		const struct device *dev)                                      \
	{                                                                      \
		CROS_RTC_ITE_IRQ_INIT(inst, 0)                                 \
		CROS_RTC_ITE_IRQ_INIT(inst, 1)                                 \
	}
DT_INST_FOREACH_STATUS_OKAY(RTC_ITE_INIT)
