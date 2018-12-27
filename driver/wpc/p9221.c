/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * IDT P9221-R7 Wireless Power Receiver driver.
 */

#include "p9221.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"
#include <stdbool.h>
#include "printf.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, "WPC " format, ## args)

#define P9221_TX_TIMEOUT_MS		(20 * 1000*1000)
#define P9221_DCIN_TIMEOUT_MS		(2 * 1000*1000)
#define P9221_VRECT_TIMEOUT_MS		(2 * 1000*1000)
#define P9221_NOTIFIER_DELAY_MS		(80*1000)
#define P9221R7_ILIM_MAX_UA		(1600 * 1000)
#define P9221R7_OVER_CHECK_NUM		3

#define OVC_LIMIT			1
#define OVC_THRESHOLD			1400000
#define OVC_BACKOFF_LIMIT		900000
#define OVC_BACKOFF_AMOUNT		100000

/* P9221  parameters */
static struct wpc_charger_info p9221_charger_info = {
	.online = false,
	.cust_id = 0,
	.i2c_port = I2C_PORT_WPC,
	.pp_buf_valid = false,
	.rx_len = 0,
	.rx_done = 0,
	.tx_id = 0,
	.tx_len = 0,
	.tx_done = 0,
	.tx_busy = 0,
	.p9221_check_vbus = 0,
	.p9221_check_det = 0,
	.vbus_status = 0,
};

static struct wpc_charger_info *wpc_get_charger_info(void)
{
	return &p9221_charger_info;
}

static int p9221_is_r7(struct wpc_charger_info *wpc)
{
	return wpc->cust_id == P9221R7_CUSTOMER_ID_VAL;
}

static size_t p9221_hex_str(uint8_t *data, size_t len, char *buf,
					size_t max_buf, bool msbfirst)
{
	int i;
	int blen = 0;
	uint8_t val;

	for (i = 0; i < len; i++) {
		if (msbfirst)
			val = data[len - 1 - i];
		else
			val = data[i];
		blen += snprintf(buf + (i * 3), max_buf - (i * 3),
				  "%02x ", val);
	}
	return blen;
}


static void p9221_set_offline(struct wpc_charger_info *wpc);

static const uint32_t p9221_ov_set_lut[] = {
	17000000, 20000000, 15000000, 13000000,
	11000000, 11000000, 11000000, 11000000
};

static int p9221_reg_is_8_bit(struct wpc_charger_info *wpc, uint16_t reg)
{
	if (p9221_is_r7(wpc)) {
		switch (reg) {
		case P9221_CHIP_REVISION_REG:
		case P9221R7_VOUT_SET_REG:
		case P9221R7_ILIM_SET_REG:
		case P9221R7_CHARGE_STAT_REG:
		case P9221R7_EPT_REG:
		case P9221R7_SYSTEM_MODE_REG:
		case P9221R7_COM_CHAN_RESET_REG:
		case P9221R7_COM_CHAN_SEND_SIZE_REG:
		case P9221R7_COM_CHAN_SEND_IDX_REG:
		case P9221R7_COM_CHAN_RECV_SIZE_REG:
		case P9221R7_COM_CHAN_RECV_IDX_REG:
		case P9221R7_DEBUG_REG:
		case P9221R7_EPP_Q_FACTOR_REG:
		case P9221R7_EPP_TX_GUARANTEED_POWER_REG:
		case P9221R7_EPP_TX_POTENTIAL_POWER_REG:
		case P9221R7_EPP_TX_CAPABILITY_FLAGS_REG:
		case P9221R7_EPP_RENEGOTIATION_REG:
		case P9221R7_EPP_CUR_RPP_HEADER_REG:
		case P9221R7_EPP_CUR_NEGOTIATED_POWER_REG:
		case P9221R7_EPP_CUR_MAXIMUM_POWER_REG:
		case P9221R7_EPP_CUR_FSK_MODULATION_REG:
		case P9221R7_EPP_REQ_RPP_HEADER_REG:
		case P9221R7_EPP_REQ_NEGOTIATED_POWER_REG:
		case P9221R7_EPP_REQ_MAXIMUM_POWER_REG:
		case P9221R7_EPP_REQ_FSK_MODULATION_REG:
		case P9221R7_VRECT_TARGET_REG:
		case P9221R7_VRECT_KNEE_REG:
		case P9221R7_FOD_SECTION_REG:
		case P9221R7_VRECT_ADJ_REG:
		case P9221R7_ALIGN_X_ADC_REG:
		case P9221R7_ALIGN_Y_ADC_REG:
		case P9221R7_ASK_MODULATION_DEPTH_REG:
		case P9221R7_OVSET_REG:
		case P9221R7_EPP_TX_SPEC_REV_REG:
			return true;
		default:
			return false;
		}
	}

	switch (reg) {
	case P9221_CHIP_REVISION_REG:
	case P9221_CUSTOMER_ID_REG:
	case P9221_CHARGE_STAT_REG:
	case P9221_EPT_REG:
	case P9221_VOUT_SET_REG:
	case P9221_ILIM_SET_REG:
	case P9221_OP_MODE_REG:
	case P9221_COM_REG:
	case P9221_FW_SWITCH_KEY_REG:
	case P9221_ALIGN_X_ADC_REG:
	case P9221_ALIGN_Y_ADC_REG:
		return true;
	default:
		return false;
	}
}

static int p9221_read8(struct wpc_charger_info *wpc, uint16_t reg, int *val)
{
	return i2c_read_offset16(wpc->i2c_port, P9221_R7_ADDR, reg, val, 1);
}

static int p9221_write8(struct wpc_charger_info *wpc, uint16_t reg, int val)
{
	return i2c_write_offset16(wpc->i2c_port, P9221_R7_ADDR, reg, val, 1);
}

static int p9221_read16(struct wpc_charger_info *wpc, uint16_t reg, int *val)
{
	return i2c_read_offset16(wpc->i2c_port, P9221_R7_ADDR, reg, val, 2);
}

static int p9221_write16(struct wpc_charger_info *wpc, uint16_t reg, int val)
{
	return i2c_write_offset16(wpc->i2c_port, P9221_R7_ADDR, reg, val, 2);
}

static int p9221_block_read(struct wpc_charger_info *wpc, uint16_t reg,
			    uint8_t *data, int len)
{
	return i2c_read_offset16_block(wpc->i2c_port, P9221_R7_ADDR, reg, data,
				     len);
}

static int p9221_block_write(struct wpc_charger_info *wpc, uint16_t reg,
			     uint8_t *data, int len)
{
	return i2c_write_offset16_block(wpc->i2c_port, P9221_R7_ADDR, reg, data,
				      len);
}

static int p9221_set_cmd_reg(struct wpc_charger_info *wpc, uint8_t cmd)
{
	int cur_cmd = 0;
	int retry;
	int ret;

	for (retry = 0; retry < P9221_COM_CHAN_RETRIES; retry++) {
		ret = p9221_read8(wpc, P9221_COM_REG, &cur_cmd);
		if (ret == 0 && cur_cmd == 0)
			break;
		msleep(25);
	}

	if (retry >= P9221_COM_CHAN_RETRIES) {
		CPRINTS("Failed to wait for cmd free %02x", cur_cmd);
		return -1;
	}

	ret = p9221_write8(wpc, P9221_COM_REG, cmd);
	if (ret)
		CPRINTS("Failed to set cmd reg %02x: %d", cmd, ret);

	return ret;
}

/*
 * Cook the value according to the register (r7+)
 */
static int p9221_cook_reg_r7(uint16_t reg, uint16_t raw_data, uint32_t *val)
{
	/* Do the appropriate conversion */
	switch (reg) {
		/* The following raw values */
	case P9221R7_ALIGN_X_ADC_REG:
	case P9221R7_ALIGN_Y_ADC_REG:
		*val = raw_data;
		break;

		/* The following are 12-bit ADC raw values */
	case P9221R7_VOUT_ADC_REG:
	case P9221R7_IOUT_ADC_REG:
	case P9221R7_DIE_TEMP_ADC_REG:
	case P9221R7_EXT_TEMP_REG:
		*val = raw_data & 0xFFF;
		break;

		/* The following are in 0.1 mill- and need to go to micro- */
	case P9221R7_VOUT_SET_REG:	/* 100mV -> uV */
		raw_data *= 100;
		/* Fall through */

		/* The following are in milli- and need to go to micro- */
	case P9221R7_IOUT_REG:	/* mA -> uA */
	case P9221R7_VRECT_REG:	/* mV -> uV */
	case P9221R7_VOUT_REG:	/* mV -> uV */
		/* Fall through */

		/* The following are in kilo- and need to go to their base */
	case P9221R7_OP_FREQ_REG:	/* kHz -> Hz */
	case P9221R7_TX_PINGFREQ_REG:	/* kHz -> Hz */
		*val = raw_data * 1000;
		break;

	case P9221R7_ILIM_SET_REG:
		/* 100mA -> uA, 200mA offset */
		*val = ((raw_data * 100) + 200) * 1000;
		break;

	case P9221R7_OVSET_REG:
		/* uV */
		raw_data &= P9221R7_OVSET_MASK;
		*val = p9221_ov_set_lut[raw_data];
		break;

	default:
		return -2;
	}

	return 0;
}

static int p9221_reg_read_cooked(struct wpc_charger_info *wpc, uint16_t reg,
				 uint32_t *val)
{
	int ret = 0;
	int data = 0;

	if (p9221_reg_is_8_bit(wpc, reg))
		ret = p9221_read8(wpc, reg, &data);
	else
		ret = p9221_read16(wpc, reg, &data);

	if (ret)
		return ret;

	return p9221_cook_reg_r7(reg, data, val);
}

static int p9221_is_online(struct wpc_charger_info *wpc)
{
	int chip_id;

	if (p9221_read16(wpc, P9221_CHIP_ID_REG, &chip_id)
			|| chip_id != P9221_CHIP_ID)
		return false;
	else
		return true;
}

int wpc_chip_is_online(void)
{
	return p9221_is_online(&p9221_charger_info);
}


void p9221_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_WPC);
}

static int p9221_clear_interrupts(struct wpc_charger_info *wpc, uint16_t mask)
{
	int ret;
	uint16_t reg;

	reg = p9221_is_r7(wpc) ? P9221R7_INT_CLEAR_REG : P9221_INT_CLEAR_REG;

	ret = p9221_write16(wpc, reg, mask);
	if (ret) {
		CPRINTS("Failed to clear INT reg: %d", ret);
		return ret;
	}

	ret = p9221_set_cmd_reg(wpc, P9221_COM_CLEAR_INT_MASK);
	if (ret)
		CPRINTS("Failed to reset INT: %d", ret);

	return ret;
}

/*
 * Enable interrupts on the P9221, note we don't really need to disable
 * interrupts since when the device goes out of field, the P9221 is reset.
 */
static int p9221_enable_interrupts(struct wpc_charger_info *wpc)
{
	uint16_t mask = 0;
	int ret;

	CPRINTS("Enable interrupts");

	if (p9221_is_r7(wpc))
		mask = P9221R7_STAT_LIMIT_MASK | P9221R7_STAT_CC_MASK;
	else
		mask = P9221_STAT_LIMIT_MASK;

	mask |= P9221_STAT_VRECT;

	ret = p9221_clear_interrupts(wpc, mask);
	if (ret)
		CPRINTS("Could not clear interrupts: %d", ret);

	ret = p9221_write8(wpc, P9221_INT_ENABLE_REG, mask);
	if (ret)
		CPRINTS("Could not enable interrupts: %d", ret);
	return ret;
}

static int p9221_send_csp(struct wpc_charger_info *wpc, uint8_t stat)
{
	int ret;
	uint8_t cmd;

	CPRINTS("Send CSP status=%d\n", stat);
	mutex_lock(&wpc->cmd_lock);
	if (p9221_is_r7(wpc)) {
		ret = p9221_write8(wpc, P9221R7_CHARGE_STAT_REG, stat);
		cmd = P9221R7_COM_SENDCSP;
	} else {
		ret = p9221_write8(wpc, P9221_CHARGE_STAT_REG, stat);
		cmd = P9221_COM_SEND_CHG_STAT_MASK;
	}
	if (ret == 0)
		ret = p9221_set_cmd_reg(wpc, cmd);
	mutex_unlock(&wpc->cmd_lock);
	return ret;
}

static int p9221_send_eop(struct wpc_charger_info *wpc, uint8_t reason)
{
	int ret;
	uint8_t cmd;

	CPRINTS("Send EOP reason=%d\n", reason);
	mutex_lock(&wpc->cmd_lock);
	if (p9221_is_r7(wpc)) {
		ret = p9221_write8(wpc, P9221R7_EPT_REG, reason);
		cmd = P9221R7_COM_SENDEPT;
	} else {
		ret = p9221_write8(wpc, P9221_EPT_REG, reason);
		cmd = P9221_COM_SEND_EOP_MASK;
	}
	if (ret == 0)
		ret = p9221_set_cmd_reg(wpc, cmd);
	mutex_unlock(&wpc->cmd_lock);
	return ret;
}

static void print_current_samples(struct wpc_charger_info *wpc,
					uint32_t *iout_val, int count)
{
	int i;
	char temp[P9221R7_OVER_CHECK_NUM * 9 + 1] = { 0 };

	for (i = 0; i < count ; i++)
		snprintf(temp + i * 9, sizeof(temp) - i * 9,
			  "%08x ", iout_val[i]);
	CPRINTS("OVER IOUT_SAMPLES: %s\n", temp);
}


/*
 * Number of times to poll the status to see if the current limit condition
 * was transient or not.
 */
static void p9221_over_handle_r7(struct wpc_charger_info *wpc,
				 uint16_t orign_irq_src)
{
	uint8_t reason = 0;
	int i;
	int ret;
	int ovc_count = 0;
	uint32_t iout_val[P9221R7_OVER_CHECK_NUM] = { 0 };
	int irq_src = (int)orign_irq_src;

	CPRINTS("Received OVER INT: %02x\n", irq_src);

	if (irq_src & P9221R7_STAT_OVV) {
		reason = P9221_EOP_OVER_VOLT;
		goto send_eop;
	}

	if (irq_src & P9221R7_STAT_OVT) {
		reason = P9221_EOP_OVER_TEMP;
		goto send_eop;
	}

	if ((irq_src & P9221R7_STAT_UV) && !(irq_src & P9221R7_STAT_OVC))
		return;

	reason = P9221_EOP_OVER_CURRENT;
	for (i = 0; i < P9221R7_OVER_CHECK_NUM; i++) {
		ret = p9221_clear_interrupts(wpc,
					   irq_src & P9221R7_STAT_LIMIT_MASK);
		msleep(50);
		if (ret)
			continue;

		ret = p9221_reg_read_cooked(wpc, P9221R7_IOUT_REG,
					    &iout_val[i]);
		if (ret) {
			CPRINTS("Failed to read IOUT[%d]: %d\n", i, ret);
			continue;
		} else if (iout_val[i] > OVC_THRESHOLD) {
			ovc_count++;
		}

		ret = p9221_read16(wpc, P9221_STATUS_REG, &irq_src);
		if (ret) {
			CPRINTS("Failed to read status: %d\n", ret);
			continue;
		}

		if ((irq_src & P9221R7_STAT_OVC) == 0) {
			print_current_samples(wpc, iout_val, i + 1);
			CPRINTS("OVER condition %04x cleared after %d tries\n",
				irq_src, i);
			return;
		}

		CPRINTS("OVER status is still %04x, retry\n", irq_src);
	}

	if (ovc_count < OVC_LIMIT) {
		print_current_samples(wpc, iout_val, P9221R7_OVER_CHECK_NUM);
		CPRINTS("ovc_threshold=%d, ovc_count=%d, ovc_limit=%d\n",
			OVC_THRESHOLD, ovc_count, OVC_LIMIT);
		return;
	}

send_eop:
	CPRINTS("OVER is %04x, sending EOP %d\n", irq_src, reason);

	ret = p9221_send_eop(wpc, reason);
	if (ret)
		CPRINTS("Failed to send EOP %d: %d\n", reason, ret);
}

static void p9221_abort_transfers(struct wpc_charger_info *wpc)
{
	wpc->tx_busy = false;
	wpc->tx_done = true;
	wpc->rx_done = true;
	wpc->rx_len = 0;
}

/* Handler for r7 and R7 chips */
static void p9221r7_irq_handler(struct wpc_charger_info *wpc, uint16_t irq_src)
{
	int res;

	if (irq_src & P9221R7_STAT_LIMIT_MASK)
		p9221_over_handle_r7(wpc, irq_src);

	/* Receive complete */
	if (irq_src & P9221R7_STAT_CCDATARCVD) {
		int rxlen = 0;

		res = p9221_read8(wpc, P9221R7_COM_CHAN_RECV_SIZE_REG, &rxlen);
		if (res) {
			CPRINTS("Failed to read len: %d", res);
			rxlen = 0;
		}

		if (rxlen) {
			res = p9221_block_read(wpc, P9221R7_DATA_RECV_BUF_START,
					       wpc->rx_buf, rxlen);
			if (res)
				CPRINTS("Failed to read len: %d", res);

			wpc->rx_len = rxlen;
			wpc->rx_done = true;
		}

	}

	/* Send complete */
	if (irq_src & P9221R7_STAT_CCSENDBUSY) {
		wpc->tx_busy = false;
		wpc->tx_done = true;
	}

	/* Proprietary packet */
	if (irq_src & P9221R7_STAT_PPRCVD) {
		const size_t maxsz = sizeof(wpc->pp_buf) * 3 + 1;
		char s[maxsz];

		res = p9221_block_read(wpc, P9221R7_DATA_RECV_BUF_START,
				       wpc->pp_buf, sizeof(wpc->pp_buf));
		if (res)
			CPRINTS("Failed to read PP len: %d", res);

		/* We only care about PP which come with 0x4F header */
		wpc->pp_buf_valid = (wpc->pp_buf[0] == 0x4F);

		p9221_hex_str(wpc->pp_buf, sizeof(wpc->pp_buf),
			      s, maxsz, false);
		CPRINTS("Received PP: %s", s);
	}

	/* CC Reset complete */
	if (irq_src & P9221R7_STAT_CCRESET)
		p9221_abort_transfers(wpc);
}

static void p9221_irq_handler(struct wpc_charger_info *wpc,
				uint16_t irq_src)
{
	uint8_t reason = 0;
	int ret;

	if (!(irq_src & P9221_STAT_LIMIT_MASK))
		return;

	CPRINTS("Received OVER INT: %02x\n", irq_src);

	if (irq_src & P9221_STAT_OV_TEMP)
		reason = P9221_EOP_OVER_TEMP;
	else if (irq_src & P9221_STAT_OV_VOLT)
		reason = P9221_EOP_OVER_VOLT;
	else
		reason = P9221_EOP_OVER_CURRENT;

	ret = p9221_send_eop(wpc, reason);
	if (ret)
		CPRINTS("Failed to send EOP %d: %d\n", reason, ret);
}


static int p9221_is_epp(struct wpc_charger_info *wpc)
{
	int ret;
	uint16_t vout_reg = P9221_VOUT_ADC_REG;
	uint32_t vout_uv;

	if (p9221_is_r7(wpc)) {
		int reg;

		ret = p9221_read8(wpc, P9221R7_SYSTEM_MODE_REG, &reg);

		if (ret == 0)
			return (reg & P9221R7_SYSTEM_MODE_EXTENDED_MASK) > 0;

		CPRINTS("Could not read mode: %d\n", ret);

		vout_reg = P9221R7_VOUT_ADC_REG;
	}

	/* Check based on power supply voltage */

	ret = p9221_reg_read_cooked(wpc, vout_reg, &vout_uv);
	if (ret) {
		CPRINTS("Could read VOUT_ADC, %d\n", ret);
		goto out;
	}

	CPRINTS("Voltage is %duV\n", vout_uv);
	if (vout_uv > P9221_EPP_THRESHOLD_UV)
		return true;

out:
	/* Default to BPP otherwise */
	return false;
}

static void p9221_write_fod(struct wpc_charger_info *wpc)
{

	int epp;
	uint8_t *fod;
	uint8_t *fod_epp;
	int fod_count = 0;
	int fod_epp_count = 0;
	uint16_t fod_reg = p9221_is_r7(wpc) ? P9221R7_FOD_REG : P9221_FOD_REG;

	int ret;
	int retries = 3;

	CPRINTS("write fod start");

	if (p9221_is_epp(wpc))
		fod_epp_count = board_get_epp_fod(&fod_epp);
	else
		fod_count = board_get_fod(&fod);

	if (!fod_count && !fod_epp_count)
		goto no_fod;

	if (fod_epp_count) {
		fod = fod_epp;
		fod_count = fod_epp_count;
		epp = true;
	}

	if (!fod)
		goto no_fod;

	while (retries) {
		char s[fod_count * 3 + 1];
		uint8_t fod_read[fod_count];

		CPRINTS("Writing %s FOD (n=%d reg=%02x try=%d)",
			epp ? "EPP" : "BPP", fod_count, fod_reg, retries);

		ret = p9221_block_write(wpc, fod_reg, fod, fod_count);
		if (ret) {
			CPRINTS("Could not write FOD: %d", ret);
			return;
		}

		/* Verify the FOD has been written properly */
		ret = p9221_block_read(wpc, fod_reg, fod_read, fod_count);
		if (ret) {
			CPRINTS("Could not read back FOD: %d", ret);
			return;
		}

		if (memcmp(fod, fod_read, fod_count) == 0)
			return;

		p9221_hex_str(fod_read, fod_count, s, sizeof(s), 0);
		CPRINTS("FOD verify error, read: %s", s);

		retries--;
		msleep(100);
	}

no_fod:
	CPRINTS("FOD not set! bpp:%d epp:%d r:%d",
		fod_count, fod_epp_count, retries);
}

static void p9221_set_online(struct wpc_charger_info *wpc)
{
	int ret;

	CPRINTS("Set online");

	wpc->online = true;

	wpc->tx_busy = false;
	wpc->tx_done = true;
	wpc->rx_done = false;
	wpc->charge_supplier = CHARGE_SUPPLIER_WPC_BPP;

	ret = p9221_enable_interrupts(wpc);
	if (ret)
		CPRINTS("enable int fail: %d", ret);

	/* NOTE: depends on _is_epp() which is not valid until DC_IN */
	p9221_write_fod(wpc);
}

static void p9221_vbus_check_timeout_defer(void)
{
	struct wpc_charger_info *wpc = wpc_get_charger_info();

	CPRINTS("timeout VUBS, online=%d", wpc->online);
	if (wpc->online)
		p9221_set_offline(wpc);

}

DECLARE_DEFERRED(p9221_vbus_check_timeout_defer);

static void p9221_set_offline(struct wpc_charger_info *wpc)
{
	CPRINTS("set offline");

	wpc->online = false;
	/* Reset PP buf so we can get a new serial number next time around */
	wpc->pp_buf_valid = false;

	p9221_abort_transfers(wpc);

	hook_call_deferred(&p9221_vbus_check_timeout_defer_data, -1);
}

/* P9221_NOTIFIER_DELAY_MS from VRECTON */
int p9221_notifier_check_det(struct wpc_charger_info *wpc)
{
	if (wpc->online)
		goto done;

	/* send out a FOD but is_epp() is still invalid */
	p9221_set_online(wpc);

	/* Give the vbus 2 seconds to come up. */
	CPRINTS("vbus detect");
	hook_call_deferred(&p9221_vbus_check_timeout_defer_data, -1);
	hook_call_deferred(&p9221_vbus_check_timeout_defer_data,
			   P9221_DCIN_TIMEOUT_MS);

done:
	wpc->p9221_check_det = false;
	return 0;
}

static int p9221_get_charge_supplier(struct wpc_charger_info *wpc)
{
	int ret = -1;
	uint32_t tx_id = 0;
	int txmf_id = 0;

	if (!wpc->online)
		return ret;

	if (p9221_is_r7(wpc) && p9221_is_epp(wpc)) {
		wpc->charge_supplier = CHARGE_SUPPLIER_WPC_EPP;

		ret = p9221_read16(wpc, P9221R7_EPP_TX_MFG_CODE_REG, &txmf_id);
		if (ret || txmf_id != P9221_GPP_TX_MF_ID)
			return ret;

		ret = p9221_block_read(wpc, P9221R7_PROP_TX_ID_REG,
					(uint8_t *) &tx_id,
					P9221R7_PROP_TX_ID_SIZE);
		if (ret)
			return ret;

		if (tx_id & 1 << 29)
			wpc->charge_supplier = CHARGE_SUPPLIER_WPC_GPP;
	} else
		wpc->charge_supplier = CHARGE_SUPPLIER_WPC_BPP;

	CPRINTS("ret=%d txmf_id=0x%04x tx_id=0x%08x charge_supplier=%d",
				ret, txmf_id, tx_id, wpc->charge_supplier);
	return ret;
}

static int p9221_get_icl(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_WPC_EPP:
	case CHARGE_SUPPLIER_WPC_GPP:
		return P9221_DC_ICL_EPP_MA;
	case CHARGE_SUPPLIER_WPC_BPP:
	default:
		return P9221_DC_ICL_BPP_MA;
	}
}

static int p9221_get_ivl(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_WPC_EPP:
	case CHARGE_SUPPLIER_WPC_GPP:
		return P9221_DC_IVL_EPP_MV;
	case CHARGE_SUPPLIER_WPC_BPP:
	default:
		return P9221_DC_IVL_BPP_MV;
	}
}

static void p9221_update_charger(int type, struct charge_port_info *chg)
{
	if (!chg)
		charge_manager_update_dualrole(0, CAP_UNKNOWN);
	else
		charge_manager_update_dualrole(0, CAP_DEDICATED);

	charge_manager_update_charge(type, 0, chg);
}

/*
 * Uncook the values and write to register
 */
static int p9221_reg_write_cooked_r7(struct wpc_charger_info *wpc,
				     uint16_t reg, uint32_t val)
{
	int ret = 0;
	uint16_t data;
	int i;
	/* Do the appropriate conversion */
	switch (reg) {
	case P9221R7_ILIM_SET_REG:
		/* uA -> 0.1A, offset 0.2A */
		if ((val < 200000) || (val > 1600000))
			return -EC_ERROR_INVAL;
		data = (val / (100 * 1000)) - 2;
		break;
	case P9221R7_VOUT_SET_REG:
		/* uV -> 0.1V */
		val /= 1000;
		if (val < 3500 || val > 9000)
			return -EC_ERROR_INVAL;
		data = val / 100;
		break;
	case P9221R7_OVSET_REG:
		/* uV */
		for (i = 0; i < ARRAY_SIZE(p9221_ov_set_lut); i++) {
			if (val == p9221_ov_set_lut[i])
				break;
		}
		if (i == ARRAY_SIZE(p9221_ov_set_lut))
			return -EC_ERROR_INVAL;
		data = i;
		break;
	default:
		return -EC_ERROR_INVAL;
	}
	if (p9221_reg_is_8_bit(wpc, reg))
		ret = p9221_write8(wpc, reg, data);
	else
		ret = p9221_write16(wpc, reg, data);
	return ret;
}

static int p9221_set_dc_icl(struct wpc_charger_info *wpc)
{
	int ret;

	/* Increase the IOUT limit */
	if (p9221_is_r7(wpc)) {
		ret = p9221_reg_write_cooked_r7(wpc, P9221R7_ILIM_SET_REG,
						P9221R7_ILIM_MAX_UA);
		if (ret)
			CPRINTS("%s set rx_iout limit fail.\n", __func__);
	}
	return ret;
}


static void p9221_notifier_check_vbus(struct wpc_charger_info *wpc)
{
	struct charge_port_info chg;

	wpc->p9221_check_vbus = false;

	CPRINTS("%s online:%d vbus:%d is_online:%d", __func__, wpc->online,
		wpc->vbus_status);
	/*
	 * We now have confirmation from DC_IN, kill the timer, p9221_online
	 * will be set by this function.
	 */

	hook_call_deferred(&p9221_vbus_check_timeout_defer_data, -1);


	if (wpc->vbus_status) {
		/* WPC Vbus on ,Always write FOD, check dc_icl, send CSP */
		p9221_set_dc_icl(wpc);
		p9221_write_fod(wpc);

		p9221_send_csp(wpc, 1);

		/* when wpc vbus attached after 2s, set wpc online */
		if (!wpc->online)
			p9221_set_online(wpc);

		/* WPC Vbus on , update charge voltage and current */
		p9221_get_charge_supplier(wpc);
		chg.voltage = p9221_get_ivl(wpc->charge_supplier);
		chg.current = p9221_get_icl(wpc->charge_supplier);

		p9221_update_charger(wpc->charge_supplier, &chg);
	} else {
		/*
		 * Vbus detached, set wpc offline and update wpc charge voltage
		 * and current to zero.
		 */
		if (wpc->online) {
			p9221_set_offline(wpc);
			p9221_update_charger(wpc->charge_supplier, NULL);
		}
	}

	CPRINTS("check_vbus changed on:%d vbus:%d", wpc->online,
		wpc->vbus_status);

}

static void p9221_detect_defer_work(void)
{

	struct wpc_charger_info *wpc = wpc_get_charger_info();

	CPRINTS("%s on:%d check_vbus:%d check_det:%d vbus:%d", __func__,
		wpc->online, wpc->p9221_check_vbus, wpc->p9221_check_det,
		wpc->vbus_status);

	/* Step 1 */
	if (wpc->p9221_check_det)
		p9221_notifier_check_det(wpc);

	/* Step 2 */
	if (wpc->p9221_check_vbus)
		p9221_notifier_check_vbus(wpc);

}

DECLARE_DEFERRED(p9221_detect_defer_work);

void notify_p9221_detect_defer_work(int vbus)
{
	struct wpc_charger_info *wpc = wpc_get_charger_info();

	CPRINTS("%s vbus:%d", __func__, vbus);
	wpc->p9221_check_vbus = true;
	wpc->vbus_status = vbus;
	hook_call_deferred(&p9221_detect_defer_work_data,
			   P9221_NOTIFIER_DELAY_MS);
}

static void p9221_irq_thread(struct wpc_charger_info *wpc)
{
	int ret, irq_src, cid;

	ret = p9221_read16(wpc, P9221_INT_REG, &irq_src);
	if (ret) {
		CPRINTS("Read INT REG failed.\n");
		return;
	}

	CPRINTS("INT SRC 0x%04x", irq_src);

	ret = p9221_clear_interrupts(wpc, irq_src);
	if (ret) {
		CPRINTS("failed clear INT %d", ret);
		return;
	}

	if (irq_src & P9221_STAT_VRECT) {
		CPRINTS("VRECTON, online=%d", wpc->online);

		if (!wpc->online) {
			if (!wpc->cust_id) {
				ret = p9221_read8(wpc, P9221_CUSTOMER_ID_REG,
						&cid);

				if (!ret)
					wpc->cust_id = (uint8_t) cid;

				CPRINTS("P9221 cid: %02x", wpc->cust_id);
			}
			wpc->p9221_check_det = true;
			hook_call_deferred(&p9221_detect_defer_work_data,
					   P9221_NOTIFIER_DELAY_MS);
		}
	}

	if (p9221_is_r7(wpc))
		p9221r7_irq_handler(wpc, irq_src);
	else
		p9221_irq_handler(wpc, irq_src);
}

void wireless_power_charger_task(void *u)
{
	struct wpc_charger_info *wpc = wpc_get_charger_info();

	while (1) {
		task_wait_event(-1);
		p9221_irq_thread(wpc);
	}
}
