/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it82xx2_cros_rtc

#include <assert.h>
#include <drivers/cros_rtc.h>
#include <soc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(cros_rtc, LOG_LEVEL_DBG);

struct cros_rtc_ite_config {
	mm_reg_t base;
	/* alarm timer irq */
	void (*irq_config_func)(const struct device *dev);
	int alarm_irq;
	int alarm_flag;
	uint8_t data_mode;
	uint8_t hour_mode;
};

struct cros_rtc_ite_data {
	cros_rtc_alarm_callback_t alarm_callback;
};

/* EC2I access index/data port */
enum ec2i_access {
	/* index port */
	EC2I_ACCESS_INDEX = 0,
	/* data port */
	EC2I_ACCESS_DATA = 1,
};

/* (12xxh) EC access to host controlled modules (EC2I Bridge) */
/* 0x00: Indirect host I/O address register */
#define EC2I_IHIOA       0x00
/* 0x01: Indirect host data register */
#define EC2I_IHD         0x01
/* 0x02: Lock super I/O host access register */
#define EC2I_LSIOHA      0x02
/* 0x03: Super I/O access lock violation register */
#define EC2I_SIOLV       0x03
/* 0x04: EC to I-Bus modules access enable register */
#define EC2I_IBMAE       0x04
/* Real-time clock (RTC) EC access enable */
#define EC2I_IBMAE_RTCAE BIT(1)
/* 0x05: I-Bus control register */
#define EC2I_IBCTL       0x05
/* EC write to I-Bus */
#define EC2I_IBCTL_CWIB  BIT(2)
/* EC read from I-Bus */
#define EC2I_IBCTL_CRIB  BIT(1)
#define EC2I_IBCTL_CRWIB (EC2I_IBCTL_CRIB | EC2I_IBCTL_CWIB)
/* EC to I-Bus access enabled */
#define EC2I_IBCTL_CSAE  BIT(0)

/* (16xxh)it51xxx, it81xxx, (3exxh)it82xx2: gpiogcr registers definition */
static mm_reg_t gpiogcr_base = DT_REG_ADDR(DT_NODELABEL(gpiogcr));
#define GPIOGCR_RTC_CLKSRC    0xfa
#define RTC_CLKSRC_CRYSTAL    BIT(7)
#define RTC_CLKSRC_OSCILLATOR BIT(3)
#define RTC_CLKSRC_SEL        BIT(0)

/* (20xxh) gctrl registers definition */
static mm_reg_t gctrl_base = DT_REG_ADDR(DT_NODELABEL(gctrl));
#define GCTRL_RSTS 0x06
#define RSTS_HGRST BIT(3)

/* Host view register map via index-data I/O pair, RTC bank 0 */
/* 0x00: Seconds register */
#define RTC_SECREG   0x00
/* 0x01: Seconds alarm 1 register */
#define RTC_SECA1DAT 0x01
/* 0x02: Minutes register */
#define RTC_MINREG   0x02
/* 0x03: Minutes alarm 1 register */
#define RTC_MINA1REG 0x03
/* 0x04: Hours register */
#define RTC_HRREG    0x04
/* 0x05: Hours alarm 1 register */
#define RTC_HRA1REG  0x05
/* 0x06: Day of week register */
#define RTC_DOWREG   0x06
/* 0x07: Day of month register */
#define RTC_DOMREG   0x07
/* 0x08: Month register */
#define RTC_MONREG   0x08
/* 0x09: Year register */
#define RTC_YRREG    0x09
/* 0x0a: RTC control register A */
#define RTC_CTLREGA  0x0A
#define CTLREGA_DICCTL(n) FIELD_PREP(GENMASK(6, 4), n)
enum ctlrega_dicctl {
	/* Normal operation */
	NORMAL_OP = 2,
	/* Divider chain reset */
	DIV_CHAIN_RST = 6,
};
/* 0x0b: RTC control register B */
#define RTC_CTLREGB  0x0B
#define CTLREGB_SM   BIT(7)
#define CTLREGB_PIE  BIT(6)
#define CTLREGB_A1IE BIT(5)
#define CTLREGB_UEIE BIT(4)
#define CTLREGB_A2IE BIT(3)
#define CTLREGB_DAM  BIT(2)
#define CTLREGB_HRM  BIT(1)
#define CTLREGB_DS   BIT(0)
enum rtc_data_mode {
	RTC_DAM_BCD = 0,
	RTC_DAM_BIN = 1,
};
enum rtc_hour_mode {
	RTC_HRM_12 = 0,
	RTC_HRM_24 = 1,
};
/* 0x0c: RTC control register C */
#define RTC_CTLREGC  0x0C
#define CTLREGC_A1IF BIT(5)
/* 0x0e: Day of week alarm 1 register */
#define RTC_DOWA1REG 0x0E
/* 0x49: Day of month alarm 1 register */
#define RTC_DOMA1REG 0x49
/* 0x4a: Month alarm 1 register */
#define RTC_MONA1REG 0x4a

static inline void ec2i_ite_wait_status_cleared(const struct device *dev,
						uint8_t mask)
{
	const struct cros_rtc_ite_config *const config = dev->config;

	while (sys_read8(config->base + EC2I_IBCTL) & mask) {
		;
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

static inline void ec2i_ite_write(const struct device *dev,
				  uint8_t index, uint8_t data)
{
	/* Set index */
	ec2i_ite_write_rtc_bank(dev, EC2I_ACCESS_INDEX, index);
	/* Set data */
	ec2i_ite_write_rtc_bank(dev, EC2I_ACCESS_DATA, data);
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

static inline uint8_t ec2i_ite_read(const struct device *dev, uint8_t index)
{
	/* Set index */
	ec2i_ite_write_rtc_bank(dev, EC2I_ACCESS_INDEX, index);
	/* Read data */
	return ec2i_ite_read_rtc_bank(dev, EC2I_ACCESS_DATA);
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
	is_pm = (raw & 0x80) != 0;
	/* Clear PM bit before decode */
	raw &= 0x7F;
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

	if (config->hour_mode == 0) {
		/*
		 * Convert 24h (0-23) to 12h representation before encoding:
		 *
		 * 24h  -> 12h  flag
		 *  0   -> 12   AM
		 *  1-11->  1-11 AM
		 * 12   -> 12   PM
		 * 13-23->  1-11 PM
		 *
		 * BCD mode:    AM 0x01-0x12, PM 0x81-0x92
		 * Binary mode: AM 0x01-0x0C, PM 0x81-0x8C
		 */
		bool is_pm = (hour >= 12);
		uint8_t val12 = (hour == 0) ? 12 :
				(hour <= 12) ? hour :
					       hour - 12;

		uint8_t encoded = rtc_encode(dev, val12);

		/* Set PM flag (bit 7) if PM */
		return is_pm ? (encoded | 0x80) : encoded;
	}

	/* 24h mode: direct encode */
	return rtc_encode(dev, hour);
}

static void rtc_it82xx2_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct cros_rtc_ite_config *config = dev->config;
	struct cros_rtc_ite_data *data = dev->data;

	ite_intc_isr_clear(config->alarm_irq);

	printk("%s\n", __func__);

	/* Call callback function */
	if (data->alarm_callback) {
		data->alarm_callback(dev);
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
	struct tm tm_val;

	/* Seconds (0–59) */
	tm_val.tm_sec = rtc_decode(dev, ec2i_ite_read(dev, RTC_SECREG));
	/* Minutes (0–59) */
	tm_val.tm_min  = rtc_decode(dev, ec2i_ite_read(dev, RTC_MINREG));
	/* Hours (0-23, 12 or 24-hour mode) */
	tm_val.tm_hour = rtc_decode_hour(dev, ec2i_ite_read(dev, RTC_HRREG));
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday = rtc_decode(dev, ec2i_ite_read(dev, RTC_DOWREG)) - 1;
	/* Day of month (1–31) */
	tm_val.tm_mday = rtc_decode(dev, ec2i_ite_read(dev, RTC_DOMREG));
	/* Month: tm_mon is 0-11, RTC stores 1-12 */
	tm_val.tm_mon  = rtc_decode(dev, ec2i_ite_read(dev, RTC_MONREG)) - 1;
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099  */
	tm_val.tm_year = rtc_decode(dev, ec2i_ite_read(dev, RTC_YRREG)) + 100;

	/*
	 * Convert struct tm to epoch time (UTC)
	 * Epoch = seconds since 1970-01-01 00:00:00 UTC
	 */
	*value = (uint32_t)timeutil_timegm(&tm_val);

	LOG_DBG("RTC GET TIME (UTC): %04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
		tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
		tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, tm_val.tm_wday);

	return 0;
}

static int cros_rtc_it82xx2_set_value(const struct device *dev, uint32_t value)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct tm tm_val;
	uint8_t ctlregb;

	printk("[set value]value=%d\n",value);

	time_t sec_val = (time_t)value;
	/* Convert epoch seconds to UTC struct tm */
	gmtime_r(&sec_val, &tm_val);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	/* Set mode enable (required before updating RTC registers) */
	ec2i_ite_write(dev, RTC_CTLREGB, CTLREGB_SM);

	/* Seconds (0-59) */
	ec2i_ite_write(dev, RTC_SECREG, rtc_encode(dev, tm_val.tm_sec));
	/* Minutes (0-59) */
	ec2i_ite_write(dev, RTC_MINREG, rtc_encode(dev, tm_val.tm_min));
	/* Hours (0-23, 12 or 24-hour mode) */
	ec2i_ite_write(dev, RTC_HRREG, rtc_encode_hour(dev, tm_val.tm_hour));
	/* Day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	ec2i_ite_write(dev, RTC_DOWREG, rtc_encode(dev, tm_val.tm_wday + 1));
	/* Day of month (1-31) */
	ec2i_ite_write(dev, RTC_DOMREG, rtc_encode(dev, tm_val.tm_mday));
	/* Month: tm_mon (0-11), RTC stores (1-12) */
	ec2i_ite_write(dev, RTC_MONREG, rtc_encode(dev, tm_val.tm_mon + 1));

	/* RTC supports only years 2000–2099 */
	if (tm_val.tm_year < 100 || tm_val.tm_year > 199) {
		LOG_ERR("RTC year out of range: %d", tm_val.tm_year + 1900);

		tm_val.tm_year = 100;
	}
	/* Year: tm_year since 1900, RTC stores (0-99) for 2000-2099 */
	ec2i_ite_write(dev, RTC_YRREG, rtc_encode(dev, tm_val.tm_year - 100));

	LOG_DBG("RTC SET TIME (UTC): %04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
		tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
		tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, tm_val.tm_wday);

	/* Data mode configuration: data-format: 0 = BCD, 1 = BIN */
	ctlregb = (config->data_mode == RTC_DAM_BCD) ? 0 : CTLREGB_DAM;
	/* Hour mode configuration: hour-format: 0 = 12-hour, 1 = 24-hour */
	ctlregb |= (config->hour_mode == RTC_HRM_24) ? CTLREGB_HRM : 0;

	ec2i_ite_write(dev, RTC_CTLREGB, ctlregb);

	return 0;
}

static int cros_rtc_it82xx2_get_alarm(const struct device *dev,
				      uint32_t *seconds, uint32_t *microseconds)
{
	struct tm tm_val;

	/*
	 * Read alarm registers from RTC hardware. These registers are stored
	 * in BCD format. Convert them to binary before using struct tm
	 */
	/* Alarm seconds (0–59) */
	tm_val.tm_sec  = rtc_decode(dev, ec2i_ite_read(dev, RTC_SECA1DAT));
	/* Alarm minutes (0–59) */
	tm_val.tm_min  = rtc_decode(dev, ec2i_ite_read(dev, RTC_MINA1REG));
	/* Alarm hours (0–23, 12 or 24-hour mode) */
	tm_val.tm_hour = rtc_decode_hour(dev, ec2i_ite_read(dev, RTC_HRA1REG));
	/* Alarm day of week: tm_wday (0-6, Sunday=0), RTC stores (1-7) */
	tm_val.tm_wday = rtc_decode(dev, ec2i_ite_read(dev, RTC_DOWA1REG)) - 1;
	/* Alarm day of month (1-31) */
	tm_val.tm_mday = rtc_decode(dev, ec2i_ite_read(dev, RTC_DOMA1REG));
	/* Alarm month: tm_mon (0-11), RTC stores (1-12) */
	tm_val.tm_mon  = rtc_decode(dev, ec2i_ite_read(dev, RTC_MONA1REG)) - 1;

	/*
	 * Convert struct tm to epoch time (UTC)
	 * Epoch = seconds since 1970-01-01 00:00:00 UTC
	 */
	*seconds = (uint32_t)timeutil_timegm(&tm_val);

	/* RTC alarm does not support sub-second resolution ??*/
	*microseconds = 0;

	printk("[RTC get_alarm] epoch=%u\n", *seconds);

	return 0;
}

static int cros_rtc_it82xx2_set_alarm(const struct device *dev,
				      uint32_t seconds, uint32_t microseconds)
{
	const struct cros_rtc_ite_config *config = dev->config;
	struct tm tm_val;//, cur_val;
	uint32_t cur_time, alarm_time;
	int ret;
	uint8_t ctlregb;

	ret = cros_rtc_get_value(dev, &cur_time);
	if (ret) {
		return ret;
	}

	seconds += microseconds / 1000000;
	alarm_time = seconds;

	printk("[set alarm]cur_time=%d, alarm_time=%d\n",cur_time,alarm_time);

	time_t sec_val = (time_t)alarm_time;
	/* Convert epoch seconds to UTC struct tm */
	gmtime_r(&sec_val, &tm_val);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	/* Set mode enable (required before updating RTC registers) */
	//ec2i_ite_write(dev, RTC_CTLREGB, CTLREGB_SM);

	/* Alarm seconds (0-59) */
	ec2i_ite_write(dev, RTC_SECA1DAT, rtc_encode(dev, tm_val.tm_sec));
	/* Alarm minutes (0-59) */
	ec2i_ite_write(dev, RTC_MINA1REG, rtc_encode(dev, tm_val.tm_min));
	/* Alarm hours (0-23, 12 or 24-hour mode) */
	ec2i_ite_write(dev, RTC_HRA1REG, rtc_encode_hour(dev, tm_val.tm_hour));
	/* Alarm weekday: tm_wday 0-6 Sunday = 0 */
	ec2i_ite_write(dev, RTC_DOWA1REG, rtc_encode(dev, tm_val.tm_wday + 1));
	/* Alarm day of month (1-31) */
	ec2i_ite_write(dev, RTC_DOMA1REG, rtc_encode(dev, tm_val.tm_mday));
	/* Alarm month: RTC: 1–12 struct tm_mon: 0–11 */
	ec2i_ite_write(dev, RTC_MONA1REG, rtc_encode(dev, tm_val.tm_mon + 1));

	/* Set rising edge triggered of external timer x */
	ite_intc_irq_polarity_set(config->alarm_irq, config->alarm_flag);
	// clear int56, int57
	ite_intc_isr_clear(config->alarm_irq);

	//irq_enable(config->alarm_irq);

	/* Data mode configuration: data-format: 0 = BCD, 1 = BIN */
	ctlregb = (config->data_mode == RTC_DAM_BCD) ? 0 : CTLREGB_DAM;
	/* Hour mode configuration: hour-format: 0 = 12-hour, 1 = 24-hour */
	ctlregb |= (config->hour_mode == RTC_HRM_24) ? CTLREGB_HRM : 0;
	/* Alarm 1 interrupt enable */
	ec2i_ite_write(dev, RTC_CTLREGB, ctlregb | CTLREGB_A1IE);

	//uint8_t ctlregc_val;
	//while(1) {
	//	ec2i_ite_read(dev, RTC_CTLREGC, &ctlregc_val);
	//	if(ctlregc_val & CTLREGC_A1IF) {
	//		printk("[set alarm]wait Alarm1 issue\n");
	//		break;
	//	}
	//}
	return 0;
}

static int cros_rtc_it82xx2_reset_alarm(const struct device *dev)
{
	//const struct cros_rtc_ite_config *config = dev->config;

	/* Check? */
	//irq_enable(config->alarm_irq);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(DIV_CHAIN_RST));

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

static int cros_rtc_ite_init(const struct device *dev)
{
	const struct cros_rtc_ite_config *config = dev->config;

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

	/* Check? */
	ec2i_ite_write(dev, 0x07, 0x10);
	//ec2i_ite_write(dev, 0x70, 0x00);
	//ec2i_ite_write(dev, 0x71, 0x00);
	//ec2i_ite_write(dev, 0x30, 0x01);

	/* Divider chain control: [6:4]=010b: normal operation */
	ec2i_ite_write(dev, RTC_CTLREGA, CTLREGA_DICCTL(NORMAL_OP));

	/* Configure RTC timer interrupt for this instance */
	config->irq_config_func(dev);

	return 0;
}

#define RTC_ITE_INIT(inst)                                                     \
	static void cros_rtc_ite_irq_config_func_##inst(const struct device *dev); \
                                                                               \
	static const struct cros_rtc_ite_config cros_rtc_ite_cfg_##inst = {    \
		.base = DT_INST_REG_ADDR(inst),                                \
		.irq_config_func = cros_rtc_ite_irq_config_func_##inst,        \
		.alarm_irq = DT_INST_IRQN_BY_IDX(inst, 0),                     \
		.alarm_flag = DT_INST_IRQ_BY_IDX(inst, 0, flags),              \
		.data_mode = DT_INST_ENUM_IDX(inst, data_mode),                \
		.hour_mode = DT_INST_ENUM_IDX(inst, hour_mode),                \
	};                                                                     \
                                                                               \
	static struct cros_rtc_ite_data cros_rtc_ite_data_##inst;              \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, cros_rtc_ite_init, NULL,                   \
			      &cros_rtc_ite_data_##inst,                       \
			      &cros_rtc_ite_cfg_##inst, POST_KERNEL,           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,              \
			      &cros_rtc_it82xx2_driver_api);                   \
                                                                               \
	static void cros_rtc_ite_irq_config_func_##inst(const struct device *dev) \
	{                                                                      \
		IRQ_CONNECT(DT_INST_IRQN(inst), 0,   \
			    rtc_it82xx2_isr, DEVICE_DT_INST_GET(inst),         \
			    DT_INST_IRQ(inst, flags));                         \
		irq_enable(DT_INST_IRQN(inst));                                \
	}
DT_INST_FOREACH_STATUS_OKAY(RTC_ITE_INIT)
