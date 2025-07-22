/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "ec_app_main.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "host_command.h"

#include <assert.h>
#include <drivers/pdc.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbc_pdc);

#define DT_DRV_COMPAT ite_it52xx_pdc

#define BYTE0(n) ((n) & 0xff)
#define BYTE1(n) (((n) >> 8) & 0xff)
#define BYTE2(n) (((n) >> 16) & 0xff)
#define BYTE3(n) (((n) >> 24) & 0xff)

#define CONFIG_PLATFORM_EC_USB_PD_OPERATING_POWER_MW 15000
#define CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA 3250
#define CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV 20000

#define I2C_DEVICE_ADDR 0x40 /* i2c: 7b'0x40 */
#define TCPC_DEVICE_ADDR 0x26 /* TCPC: 7b'0x26 (port0 and port1) */
#define TIMEOUT_MS 20

#define I2C_NODE_ID DT_NODELABEL(i2c1)
#define PDC_POWER_P0_NODE_ID DT_NODELABEL(pdc_power_p0)

/*
 * Constants for it52xx interrupt status
 */
#define IT52XX_INT_VDM_STATUS         BIT(0)
#define IT52XX_INT_UCSI_STATUS        BIT(1)
#define IT52XX_INT_READ_STATUS_LENGTH 1
/*
 * Constants for CONTROL command Length: cmd + byte count + 8 bytes data structure
 */
#define IT52XX_CONTROL_CMD_BASE_LENGTH 10
/*
 * It5271 PDC only supports one C-port, so regardless of which port the task
 * is running on, we always set the connector number to 1.
 * Also, it5271 always replies to connector number 1 to EC.
 */
#define IT5271_CONNECTOR_NUMBER 1
/*
 * Constants for VDM
 */
#define IT52XX_VDM_VDC_GET_PDC_INFO 0
#define IT52XX_VDM_VDC_VER 1
#define ITE_VDM_VID_L 0x8D
#define ITE_VDM_VID_H 0x04
/*
 * Constants for SET_PDO
 */
#define IT52XX_SET_PDO_MAX_PDO_COUNT 7
/* Length of command header and other fields up to the first PDO */
#define IT52XX_SET_PDO_CMD_BASE_LENGTH 12
/* Maximum length of the SET_PDO command */
#define IT52XX_SET_PD_CMD_MAX_LENGTH      \
	(IT52XX_SET_PDO_CMD_BASE_LENGTH + \
	 sizeof(uint32_t) * IT52XX_SET_PDO_MAX_PDO_COUNT)
#define END_OF_MESSAGE 1

#define IT52XX_GET_IC_STATUS_LENGTH 2

/* CCI byte count and 4bytes data  */
#define PDC_MAX_CCI_LENGTH 0x05

bool ucsi_address;
//static struct gpio_dt_spec
//	it52xx_irq_list[1/*DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)*/];
struct gpio_callback gpio_cb;

struct smbus_cmd_t {
	/* Command ex. CTRL, MSGOUT, CCI, MSGIN */
	uint8_t cmd;
	/* Byte count */
	uint8_t len;
	/* Sub-Command */
	uint8_t sub;
};

static const struct smbus_cmd_t GET_ALERT_STATUS = { 0xBD, 0x01 };
static const struct smbus_cmd_t CLR_ALERT_STATUS = { 0xBC,
			     (IT52XX_INT_VDM_STATUS | IT52XX_INT_UCSI_STATUS) };
static const struct smbus_cmd_t SET_MSGOUT = { 0x83 };
static const struct smbus_cmd_t GET_CCI = { 0x80 };
static const struct smbus_cmd_t GET_MSGIN = { 0x82 };

static const struct smbus_cmd_t VENDOR_CMD_ENABLE = { 0xF4, 0xA0/*global reset*/, 0x7F };
static const struct smbus_cmd_t SET_PPM_RESET = { 0x81, 0x08, 0x01 };
static const struct smbus_cmd_t SET_NOTIFICATION_ENABLE = { 0x81, 0x08, 0x05 };
static const struct smbus_cmd_t GET_IC_STATUS = { 0x81, 0x08, 0x20 };
static const struct smbus_cmd_t GET_ERROR_STATUS = { 0x81, 0x08, 0x13 };
static const struct smbus_cmd_t SET_PDO = { 0x81, 0x08, 0x1D };
static const struct smbus_cmd_t GET_CONNECTOR_STATUS = { 0x81, 0x08, 0x12 };

static const struct smbus_cmd_t ACK_CC_CI = { 0x81, 0x08, 0x04 };
/**
 * @brief PDC Config object
 */
struct pdc_config_t {
	/** I2C config */
	struct i2c_dt_spec i2c;
	/** pdc power path interrupt */
	struct gpio_dt_spec irq_gpios;
	/** connector number of this port */
	uint8_t connector_number;
	/** Notification enable bits */
	union notification_enable_t bits;
	/** Create thread function */
	void (*create_thread)(const struct device *dev);
	/** If true, do not apply PDC FW updates to this port */
	bool no_fw_update;
	/** Whether or not this port supports CCD */
	bool ccd;
	/** Pointer to the device-specific callback function */
	gpio_callback_handler_t callback_handler;
};

enum cmd_t {
	/** No command */
	CMD_NONE,
	/** CMD_TRIGGER_PDC_RESET */
	CMD_TRIGGER_PDC_RESET,
	/** PDC Enable */
	CMD_VENDOR_ENABLE,
	/** Set Notification Enable */
	CMD_SET_NOTIFICATION_ENABLE,
	/** PDC Reset */
	CMD_PPM_RESET,
	/** Connector Reset */
	CMD_CONNECTOR_RESET,
	/** Get Capability */
	CMD_GET_CAPABILITY,
	/** Get Connector Capability */
	CMD_GET_CONNECTOR_CAPABILITY,
	/** Set UOR */
	CMD_SET_UOR,
	/** Set PDR */
	CMD_SET_PDR,
	/** Get PDOs */
	CMD_GET_PDOS,
	/** Get Connector Status */
	CMD_GET_CONNECTOR_STATUS,
	/** Get Error Status */
	CMD_GET_ERROR_STATUS,
	/** Get VBUS Voltage */
	CMD_GET_VBUS_VOLTAGE,
	/** Get IC Status */
	CMD_GET_IC_STATUS,
	/** Set CCOM */
	CMD_SET_CCOM,
	/** Set DRP_MODE */
	CMD_SET_DRP_MODE,
	/** Get DRP_MODE */
	CMD_GET_DRP_MODE,
	/** Read Power Level */
	CMD_READ_POWER_LEVEL,
	/** Get RDO */
	CMD_GET_RDO,
	/** Set RDO */
	CMD_SET_RDO,
	/** Set Sink Path */
	CMD_SET_SINK_PATH,
	/** Get current Partner SRC PDO */
	CMD_GET_CURRENT_PARTNER_SRC_PDO,
	/** Set the Fast Role Swap */
	CMD_SET_FRS_FUNCTION,
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
	/** TypeC reconnect */
	CMD_SET_TPC_RECONNECT,
	/** set Retimer into FW Update Mode */
	CMD_SET_RETIMER_FW_UPDATE_MODE,
	/** Get the cable properties */
	CMD_GET_CABLE_PROPERTY,
	/** Get VDO(s) of PDC, Cable, or Port partner */
	CMD_GET_VDO,
	/** CMD_GET_IDENTITY_DISCOVERY */
	CMD_GET_IDENTITY_DISCOVERY,
	/** CMD_GET_IS_VCONN_SOURCING */
	CMD_GET_IS_VCONN_SOURCING,
	/** CMD_SET_PDO */
	CMD_SET_PDO,
	/** Get PDC ALT MODE Status Register value */
	CMD_GET_PCH_DATA_STATUS,
	/** CMD_ACK_CC_CI */
	CMD_ACK_CC_CI,
	/** Raw UCSI call.
	 * Special handling of the data read from a PDC will be skipped. */
	CMD_RAW_UCSI,
	/** CMD_GET_LPM_PPM_INFO */
	CMD_GET_LPM_PPM_INFO,
	/** CMD_GET_ATTENTION_VDO */
	CMD_GET_ATTENTION_VDO,
	/** CMD_GET_SBU_MUX_MODE */
	CMD_GET_SBU_MUX_MODE,
	/** CMD_SET_SBU_MUX_MODE */
	CMD_SET_SBU_MUX_MODE,
	/** Set the Burnside Bridge retimer into CTS test mode */
	CMD_SET_BBR_CTS,
	/** Clear it52xx alert status */
	CMD_CLR_ALERT_STATUS,
};

union ping_status_t {
	/* rtk cci only reply one byte */
	//struct {
	//	/** Command status */
	//	uint8_t cmd_sts : 2;
	//	/** Length of data read to read */
	//	uint8_t data_len : 6;
	//};
	//uint8_t raw_value;

	/* ite(spec) cci reply five bytes ByteCount - xx xx(MSGIN len) xx xx(status: ... /busy/ACK/error/CC) */
	struct {
		/** Byte count of CCI Command */
		uint8_t cci_byte_cnt;
		/** End of message indicator */
		uint8_t end_of_msgi : 1;
		/** Connector change indicator */
		uint8_t cci : 7;
		/** Data length */
		uint8_t data_len;
		/** Vendor defined message indicator */
		uint8_t vdmi : 1;
		/** Reserved */
		uint8_t Reserved : 6;
		/** Security request indicator */
		uint8_t sri : 1;
		/** Command status */
		uint8_t cmd_sts;
	};
	uint8_t raw_value[PDC_MAX_CCI_LENGTH];
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
	/** State machine context */
	//struct smf_ctx ctx;
	/** Init's local state variable */
	//enum init_state_t init_local_state;
	/** Init's current state */
	//enum init_state_t init_local_current_state;
	/** Init's next state */
	//enum init_state_t init_local_next_state;
	/** PDC's last state */
	//enum state_t last_state;
	/** PDC device structure */
	const struct device *dev;
	/** PDC command */
	enum cmd_t cmd;
	/** Driver thread */
	//k_tid_t thread;
	/** Driver thread's data */
	//struct k_thread thread_data;
	/** Ping status */
	union ping_status_t ping_status;
	/** Timepoint for when we can next call ping status. */
	k_timepoint_t next_ping_status;
	/** Ping status retry counter */
	uint8_t ping_retry_counter;
	/** Number of time the init process has been attempted */
	uint8_t init_retry_counter;
	/** I2C retry counter */
	uint8_t i2c_transaction_retry_counter;
	/** PDC write buffer */
	uint8_t wr_buf[PDC_MAX_DATA_LENGTH];
	/** Length of bytes in the write buffer */
	uint8_t wr_buf_len;
	/** PDC read buffer */
	uint8_t rd_buf[PDC_MAX_DATA_LENGTH];
	/** Length of bytes in the read buffer */
	uint8_t rd_buf_len;
	/** Pointer to user data */
	uint8_t *user_buf;
	/** Command mutex */
	//struct k_mutex mtx;
	/** GPIO interrupt callback */
	struct gpio_callback gpio_cb;
	/** Error status */
	union error_status_t error_status;
	/** CCI Event */
	union cci_event_t cci_event;
	/** CC Event callback */
	struct pdc_callback *cc_cb;
	/** CC Event one-time callback. If it's NULL, cci_cb will be called. */
	struct pdc_callback *cc_cb_tmp;
	/** Asynchronous (CI) Event callbacks */
	//sys_slist_t ci_cb_list;
	/** Information about the PDC */
	struct pdc_info_t info;
	/** Init done flag */
	bool init_done;
	/** Error recovery delay counter */
	uint16_t error_recovery_delay_counter;
	/** Error recovery counter */
	uint16_t error_recovery_counter;
	/** Error Status used during initialization */
	union error_status_t es;
	/* Driver specific events to handle. */
	//struct k_event driver_event;
	/* Currently running UCSI command. */
	enum ucsi_command_t active_ucsi_cmd;
};

struct pdc_data_t data_0;

static const uint32_t pdo_snk_fixed_flags =
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP);

static uint32_t pdc_snk_pdos[] = {
	/* Mandatory fixed 5V PDO 5V@3A = 0x2601912C */
	PDO_FIXED(5000,
		  MIN((CONFIG_PLATFORM_EC_USB_PD_OPERATING_POWER_MW / 5),
		      CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA),
		  pdo_snk_fixed_flags),
	/* Battery PDO covering 5V-5% to the board maximum voltage and current 20V,15W = 0x59017c3c
	 */
	PDO_BATT(4750, CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		 CONFIG_PLATFORM_EC_USB_PD_OPERATING_POWER_MW),
	/* Variable PDO covering 5V-5% to the board maximum voltage and current 20V,3.25A = 0x99017d45
	 */
	PDO_VAR(4750, CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA),
};

/**
 * @brief Helper method for setting up a command call.
 * @param dev PDC device pointer
 * @param cmd Command to execute
 * @param buf Command payload to copy into write buffer
 * @param len Length of paylaod buffer
 * @param user_buf Pointer to buffer where response data will be written.
 * @return 0 on success
 * @return -EBUSY if command is already pending.
 * @return -ECONNREFUSED if chip communication is disabled
 */
static int it52xx_post_command_with_callback(/*const struct device *dev,*/
					    enum cmd_t cmd, const uint8_t *buf,
					    uint8_t len, uint8_t *user_buf,
					    struct pdc_callback *callback)
{
	struct pdc_data_t *data = &data_0 /*dev->data*/;

	/* Return an error if chip communication is suspended */
	//if (check_comms_suspended()) {
	//	return -ECONNREFUSED;
	//}

	//k_mutex_lock(&data->mtx, K_FOREVER);

	//if (data->cmd != CMD_NONE) {
	//	k_mutex_unlock(&data->mtx);
	//	return -EBUSY;
	//}

	if (buf) {
		assert(len <= sizeof(data->wr_buf));
		memcpy(data->wr_buf, buf, len);
	}

	data->wr_buf_len = len;
	data->user_buf = user_buf;
	data->cmd = cmd;
	data->cc_cb_tmp = callback;

	//LOG_ERR("buf[0x%x] = { 0x%x, 0x%x, 0x%x, 0x%x }  ", len, buf[0], buf[1], buf[2], buf[3]);
	//LOG_ERR("data->wr_buf[0x%x] = { 0x%x, 0x%x, 0x%x, 0x%x }  ", data->wr_buf_len, data->wr_buf[0], data->wr_buf[1], data->wr_buf[2], data->wr_buf[3]);
	/* If sending a raw UCSI command, byte[2] is the actual UCSI command
	 * being executed.
	 */
	if (cmd == CMD_RAW_UCSI && buf) {
		data->active_ucsi_cmd = data->wr_buf[2];
	}

	//if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
	//	const struct pdc_config_t *cfg = dev->config;

	//	pdc_trace_msg_req(cfg->connector_number,
	//			  PDC_TRACE_CHIP_TYPE_RTS54XX, data->wr_buf,
	//			  data->wr_buf_len);
	//}

	//k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	//k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);

	return 0;
}

static int it52xx_post_command(/*const struct device *dev,*/ enum cmd_t cmd,
			      const uint8_t *buf, uint8_t len,
			      uint8_t *user_buf)
{
	return it52xx_post_command_with_callback(/*dev,*/ cmd, buf, len, user_buf,
						NULL);
}

static int get_ara(const struct i2c_dt_spec *i2c_dt/*const struct device *dev*/, uint8_t *ara)
{
	//const struct pdc_config_t *cfg = dev->config;

	return i2c_read(i2c_dt->bus /*cfg->i2c.bus*/, ara, 1, SMBUS_ADDRESS_ARA);
}

static int it52xx_get_alert_status(struct pdc_data_t *pdc_int_data, const struct i2c_dt_spec *i2c_dt /*const struct device *dev*/)
{
	//const struct pdc_config_t *cfg = dev->config;
	//struct pdc_data_t *const pdc_int_data = pdc_data[cfg->connector_number];
	struct i2c_msg msg[2];
	uint8_t cmd = GET_ALERT_STATUS.cmd;
	uint8_t len = GET_ALERT_STATUS.len;

	/* Return an error if chip communication is suspended */
	//if (check_comms_suspended()) {
	//	return -ECONNREFUSED;
	//}

	msg[0].buf = &cmd;
	msg[0].len = len;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = pdc_int_data->rd_buf;
	msg[1].len = IT52XX_INT_READ_STATUS_LENGTH;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, msg, 2);
}

static int it52xx_clr_alert_status(struct pdc_data_t *pdc_int_data, const struct i2c_dt_spec *i2c_dt /*const struct device *dev*/)
{
	//const struct pdc_config_t *cfg = dev->config;
	//struct pdc_data_t *const pdc_int_data = pdc_data[cfg->connector_number];
	struct i2c_msg msg;

	/* Return an error if chip communication is suspended */
	//if (check_comms_suspended()) {
	//	return -ECONNREFUSED;
	//}

	//k_mutex_lock(&data->mtx, K_FOREVER); //TODO: need?

	//if (data->cmd != CMD_NONE) {
	//	k_mutex_unlock(&data->mtx);
	//	return -EBUSY;
	//}

	uint8_t payload[] = {
		CLR_ALERT_STATUS.cmd,
		CLR_ALERT_STATUS.len, /* clear IT52XX_INT_VDM_STATUS & IT52XX_INT_UCSI_STATUS */
	};

	memcpy(pdc_int_data->wr_buf, payload, ARRAY_SIZE(payload));

	//TODO: directly set data to msg.buf (not through pdc_int_data->wr_buf_len) => can't, payload is local var
	pdc_int_data->wr_buf_len = ARRAY_SIZE(payload);
	pdc_int_data->user_buf = NULL;
	pdc_int_data->cmd = CMD_CLR_ALERT_STATUS;
	pdc_int_data->cc_cb_tmp = NULL;

	//k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	//k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);

	msg.buf = pdc_int_data->wr_buf;
	msg.len = pdc_int_data->wr_buf_len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg, 1);
}

static int it52xx_pdc_reset(struct pdc_data_t *pdc_int_data, const struct i2c_dt_spec *i2c_dt /*const struct device *dev*/)
{
	//struct pdc_data_t *data = dev->data;

#if 0
	if (get_state(data) == ST_DISABLE) {
		perform_pdc_init(data);
		return 0;
	}

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * Not UCSI cmd, it5271 does not support, this only sets dummy data,
	 * not goes to write state. Later ppm may go to unattached state.
	 * After lpm reset, it must needs UCSI init again, just go through by perform_pdc_init() without setting snk pdo, seems not good?
	 */
	return it52xx_post_command(dev, CMD_TRIGGER_PDC_RESET, NULL, 0, NULL);
#else   /* test only, needs UCSI init again */
	//const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg;

	/* Can only be called from Init State */
	//if (get_state(data) != ST_INIT) {
	//	return -EBUSY;
	//}

	/* Reboot it52xx */
	uint8_t payload[] = {
		VENDOR_CMD_ENABLE.cmd,
		VENDOR_CMD_ENABLE.len, /* global reset */
		VENDOR_CMD_ENABLE.sub,
	};

	memcpy(pdc_int_data->wr_buf, payload, ARRAY_SIZE(payload));

	/* TODO: directly set payload to msg.buf (not through data->wr_buf_len) */
	pdc_int_data->wr_buf_len = ARRAY_SIZE(payload);
	//data->user_buf = NULL;            shouldn't overwrite (keep last CONTROL cmd user_buf)
	pdc_int_data->cmd = CMD_TRIGGER_PDC_RESET;
	//data->cc_cb_tmp = NULL;           shouldn't overwrite (keep last cmd cc_cb_tmp)

	//k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	//k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);

	msg.buf = pdc_int_data->wr_buf;
	msg.len = pdc_int_data->wr_buf_len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg, 1);
#endif
}

static int it52xx_ppm_reset(void /*const struct device *dev*/)
{
	//struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	//if (get_state(data) != ST_INIT) {
	//	return -EBUSY;
	//}

	uint8_t payload[] = {
		SET_PPM_RESET.cmd,
		SET_PPM_RESET.len,
		SET_PPM_RESET.sub,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(/*dev,*/ CMD_PPM_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_set_notification_enable(void /*const struct device *dev,
					 union notification_enable_t bits*/)
{
	//struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	//if (get_state(data) != ST_INIT) {
	//	return -EBUSY;
	//}

	uint8_t payload[] = {
		SET_NOTIFICATION_ENABLE.cmd,
		SET_NOTIFICATION_ENABLE.len,
		SET_NOTIFICATION_ENABLE.sub,
		0x00,
		0xEF, //BYTE0(bits.raw_value),
		0xDB, //BYTE1(bits.raw_value),
		0x01, //BYTE2(bits.raw_value),
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(/*dev,*/ CMD_SET_NOTIFICATION_ENABLE, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_get_info(/*const struct device *dev,*/ struct pdc_info_t *info/*,bool live*/)
{
	//const struct pdc_config_t *cfg = dev->config;
	//struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte6;

#if 0
	if (info == NULL) {
		return -EINVAL;
	}

	/* If caller is OK with a non-live value and we have one, we can
	 * immediately return a cached value.
	 */
	if (!live) {
		k_mutex_lock(&data->mtx, K_FOREVER);

		/* Check FW ver and VID/PID fields for valid values to ensure
		 * we have a resident value.
		 */
		if (data->info.fw_version == PDC_FWVER_INVALID ||
		    data->info.vid == PDC_VID_INVALID ||
		    data->info.pid == PDC_PID_INVALID) {
			k_mutex_unlock(&data->mtx);

			/* No cached value. Caller should request a live read */
			return -EAGAIN;
		}

		*info = data->info;
		k_mutex_unlock(&data->mtx);

		LOG_DBG("ST%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(data->info.fw_version),
			PDC_FWVER_GET_MINOR(data->info.fw_version),
			PDC_FWVER_GET_PATCH(data->info.fw_version));
		return 0;
	}
#endif

	/* Handle a live read */

	//if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
	//	return -EBUSY;
	//}

	/* bit[16:23]: connector num 7'b, IT52XX_VDM_VDC_GET_PDC_INFO 1'b */
	/* bit[24:31]: IT52XX_VDM_VDC_GET_PDC_INFO 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) | (IT52XX_VDM_VDC_GET_PDC_INFO & BIT(0) << 7);
	byte6 = ((IT52XX_VDM_VDC_GET_PDC_INFO >> 1) & 0xf) | ((IT52XX_VDM_VDC_VER & 0xf) << 4);

	/* Post a command and perform a chip operation */
	uint8_t payload[] = {
		GET_IC_STATUS.cmd,
		GET_IC_STATUS.len,
		GET_IC_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		byte6,
		ITE_VDM_VID_L,
		ITE_VDM_VID_H,
		0x00, /* EC_PID_L --> it52xx don't care */
		0x00, /* EC_PID_H --> it52xx don't care */
	};

	//LOG_DBG("ST%d: Get live chip info", cfg->connector_number);

	return it52xx_post_command(/*dev,*/ CMD_GET_IC_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)info);
}

static int it52xx_get_error_status(/*const struct device *dev,*/ union error_status_t *es)
{
	//struct pdc_data_t *data = dev->data;

	//if (es == NULL) {
	//	return -EINVAL;
	//}

	/* Port is disabled. Return the last read error_status. */
	//if (get_state(data) == ST_DISABLE) {
	//	es->raw_value = data->error_status.raw_value;
	//	return 0;
	//}

	//if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
	//	return -EBUSY;
	//}

	uint8_t payload[] = {
		GET_ERROR_STATUS.cmd,
		GET_ERROR_STATUS.len,
		GET_ERROR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		IT5271_CONNECTOR_NUMBER, /* Connector number --> it5271 only support one port */
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(/*dev,*/ CMD_GET_ERROR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)es);
}

static int it52xx_set_pdo(/*const struct device *dev,*/ enum pdo_type_t type,
			 uint32_t *pdo, int count)
{
	//struct pdc_data_t *data = dev->data;
	uint32_t pdo_info;

	//if (pdo == NULL) {
	//	return -EINVAL;
	//}

	//if (count < 1 || count > IT52XX_SET_PDO_MAX_PDO_COUNT) {
		/* Count == 0 is reserved per RTK manual */
	//	return -ERANGE;
	//}

	//if (get_state(data) != ST_IDLE) {
	//	return -EBUSY;
	//}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	/* bit[24:31]: reserved 2'b, SRC or SNK PDO 1'b, num of pdo 4'b, data index 1'b */
	/* bit[32:39]: data index 6'b, end of message 1'b, reserved 1'b */
	pdo_info = 1/* port0 */ | (type << 10) | ((count & 0xF) << 11) |
		   (END_OF_MESSAGE << 22);

	/* Load command header and fields up to the first PDO field */
	uint8_t payload[IT52XX_SET_PD_CMD_MAX_LENGTH] = {
		/* CONTROL cmd */
		SET_PDO.cmd,
		SET_PDO.len,
		SET_PDO.sub,
		sizeof(uint32_t) * count,
		BYTE0(pdo_info),
		BYTE1(pdo_info),
		BYTE2(pdo_info),
		0x00,
		0x00,
		0x00,
		/* MSGOUT cmd */
		SET_MSGOUT.cmd,
		sizeof(uint32_t) * count,
		/* Memcopy pdo to here */
	};

	/* Compute actual length given number of PDOs being set */
	uint8_t payload_len =
		IT52XX_SET_PDO_CMD_BASE_LENGTH + sizeof(uint32_t) * count;

	/* Copy PDOs into payload buffer */
	memcpy(&payload[IT52XX_SET_PDO_CMD_BASE_LENGTH], pdo,
	       sizeof(uint32_t) * count);

	return it52xx_post_command(/*dev,*/ CMD_SET_PDO, payload, payload_len, NULL);
}

static int it52xx_get_connector_status(/*const struct device *dev,*/
				      union connector_status_t *cs)
{
	//struct pdc_data_t *data = dev->data;
	//struct pdc_config_t *cfg = dev->config;

	//if (get_state(data) != ST_IDLE) {
	//	return -EBUSY;
	//}

	//if (cs == NULL) {
	//	return -EINVAL;
	//}

	uint8_t payload[] = {
		GET_CONNECTOR_STATUS.cmd,
		GET_CONNECTOR_STATUS.len,
		GET_CONNECTOR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		1 /* port0 */, /* Connector number --> start from 1 */
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(/*dev,*/ CMD_GET_CONNECTOR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)cs);
}

static int it52xx_ack_cc_ci(/*const struct device *dev,*/
			   union conn_status_change_bits_t ci, bool cc/*, uint16_t vendor_defined*/)
{
	//struct pdc_data_t *data = dev->data;

	//if (get_state(data) != ST_IDLE) {
	//	return -EBUSY;
	//}

#if 0
	uint8_t payload[] = { ACK_CC_CI.cmd,
			      ACK_CC_CI.len,
			      ACK_CC_CI.sub,
			      0x00,
			      BYTE0(ci.raw_value), //refer to spec: only bit[16:17] set to 1b/0b
			      BYTE1(ci.raw_value), //why set ci.raw_value & vendor_defined here?
			      BYTE0(vendor_defined),
			      BYTE1(vendor_defined),
			      cc };
#endif

#if 1 //not verified
	uint8_t bit16_17 = (ci.connect_change | (cc << 1));
	uint8_t payload[] = { ACK_CC_CI.cmd,
			      ACK_CC_CI.len,
			      ACK_CC_CI.sub,
			      0x00,
			      bit16_17,
			      0x00,
			      0x00,
			      0x00,
			      0x00,
			      0x00 };
#endif

	return it52xx_post_command(/*dev,*/ CMD_ACK_CC_CI, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_i2c_write(/*const struct device *dev,*/
			    const struct i2c_dt_spec *i2c_dt)
{

	struct pdc_data_t *data = &data_0 /*dev->data*/;
	/* const struct pdc_config_t *cfg = dev->config;*/
#if 0
	struct i2c_msg msg;

	msg.buf = data->wr_buf;
	msg.len = data->wr_buf_len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg, 1);
#endif

	//Transmit MSGOUT data + CONTROL cmd
	if (data->cmd == CMD_SET_PDO) {
		struct i2c_msg msg[2];
		uint32_t len = IT52XX_CONTROL_CMD_BASE_LENGTH;

		msg[1].buf = &data->wr_buf[IT52XX_CONTROL_CMD_BASE_LENGTH];
		/* MSGOUT len = total len - CONTROL cmd len */
		msg[1].len = (data->wr_buf_len - len);
		msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg[1], 1);

		msg[0].buf = data->wr_buf;
		/* CONTROL cmd len */
		msg[0].len = len;
		msg[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg[0], 1);
	} else {
		struct i2c_msg msg;

		msg.buf = data->wr_buf;
		msg.len = data->wr_buf_len;
		msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, &msg, 1);
	}
}

static int get_ping_status(/*const struct device *dev,*/
			   const struct i2c_dt_spec *i2c_dt)
{
	//struct pdc_data_t *data = dev->data;
	//const struct pdc_config_t *cfg = dev->config;
	//struct i2c_msg msg;

	/* only read 1 byte CCI */
	//msg.buf = &data->ping_status.raw_value;
	//msg.len = 1;
	//msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	//return i2c_transfer_dt(&cfg->i2c, &msg, 1);

	struct pdc_data_t *data = &data_0 /*dev->data*/;
	/* const struct pdc_config_t *cfg = dev->config; */
	struct i2c_msg msg[2];
	uint8_t cmd = GET_CCI.cmd;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->ping_status.raw_value; /* original 1byte, we need 5byte space */
	msg[1].len = 5; /* W CCI - R [04 xx xx xx xx] => len = 5 */
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, msg, 2);
}

static int it52xx_i2c_read(/*const struct device *dev,*/ const struct i2c_dt_spec *i2c_dt)
{
	struct pdc_data_t *data = &data_0 /*dev->data*/;
	//const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t cmd = GET_MSGIN.cmd;
	int rv;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->rd_buf;
	msg[1].len = data->ping_status.data_len + 1; /* need + 1 for byte count field */
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	rv = i2c_transfer_dt(i2c_dt /*&cfg->i2c*/, msg, 2);
	if (rv < 0) {
		return rv;
	}

	data->rd_buf_len = data->ping_status.data_len; //not include byte count

	//if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
	//	pdc_trace_msg_resp(cfg->connector_number,
	//			   PDC_TRACE_CHIP_TYPE_RTS54XX, data->rd_buf,
	//			   data->ping_status.data_len + 1);
	//}

	return rv;
}

#if 0
/**
 * @brief This function should be called after any I2C transfer that failed.
 * It increments a counter, notifies the subsystem of the I2C error and then
 * enters the recovery state. NOTE: the data->i2c_transaction_retry_counter
 * should be set to zero in the calling states entry action.
 */
static bool max_i2c_retry_reached(struct pdc_data_t *data, int type)
{
	const struct pdc_config_t *cfg = data->dev->config;

	data->i2c_transaction_retry_counter++;
	if (data->i2c_transaction_retry_counter > N_I2C_TRANSACTION_COUNT) {
		/* MAX I2C transactions exceeded */
		LOG_ERR("C%d: %s i2c error", cfg->connector_number,
			(type & I2C_MSG_READ) ? "Read" : "Write");
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command has completed */
		data->cci_event.command_completed = 1;
		/* Clear busy event */
		data->cci_event.busy = 0;
		/* Set error, I2C read error */
		if (type & I2C_MSG_READ) {
			data->error_status.i2c_read_error = 1;
		} else {
			data->error_status.i2c_write_error = 1;
		}
		/* Notify system of status change */
		call_cci_event_cb(data);
		return true;
	}
	return false;
}
#endif

static void pdc_interrupt_callback(const struct device *dev,
				   struct gpio_callback *cb,
				   uint32_t pins)
{
	//k_event_post(&PDC_DATA_STRUCT_NAME(inst).driver_event,
	//	     RTS54XX_IRQ_EVENT);
	//read 0xBD
	LOG_ERR("pdc interrupt callback");
}

/** A stub main to call the real ec app main function. LCOV_EXCL_START */
int main(void)
{
	const struct i2c_dt_spec i2c_dt = { I2C_DT_SPEC_GET_ON_I2C(PDC_POWER_P0_NODE_ID) }; /*Get PDC_POWER_P0_NODE is belong which i2c node(bus) & reg of PDC_POWER_P0_NODE(address)*/
	const struct gpio_dt_spec irq_gpios_dt = { .port = DEVICE_DT_GET(DT_NODELABEL(gpioa)),
						   .pin = 6,
						   .dt_flags = GPIO_ACTIVE_LOW };
	/*GPIO_DT_SPEC_INST_GET(0, irq_gpios); => __device_dts_ord undeclared*/
	/*GPIO_DT_SPEC_GET(PDC_POWER_P0_NODE_ID, irq_gpios); => __device_dts_ord undeclared*/
	/*GPIO_DT_SPEC_GET_BY_IDX(PDC_POWER_P0_NODE_ID, irq_gpios, 0); => __device_dts_ord undeclared*/
	/*Get port & pin & flag of irq-gpios prop (within PDC_POWER_P0_NODE)*/
	//bool irq_init_done = false;
	/* PDC device structure */
	//const struct device *dev = DEVICE_DT_GET(PDC_POWER_P0_NODE_ID); /*=> undefined reference to `__device_dts_ord_371 */
	struct pdc_data_t *data = &data_0;
	uint8_t len;
	int rv;

	ec_app_main();

	/* i2c test code (refer to i2c test & pdc driver) */
	rv = i2c_is_ready_dt(&i2c_dt /*&cfg->i2c*/);
	if (rv < 0) {
		LOG_ERR("device %s not ready", i2c_dt.bus->name /*cfg->i2c.bus->name*/);
		return -ENODEV;
	}

	rv = gpio_is_ready_dt(&irq_gpios_dt /*&cfg->irq_gpios*/);
	if (rv < 0) {
		LOG_ERR("device %s not ready", irq_gpios_dt.port->name /*cfg->irq_gpios.port->name*/);
		return -ENODEV;
	}

	//k_event_init(&data->driver_event);

	//for (int i = 0; i < ARRAY_SIZE(it52xx_irq_list); i++) {
	//	if (it52xx_irq_list[i].port == irq_gpios_dt.port/*cfg->irq_gpios.port*/ &&
	//	    it52xx_irq_list[i].pin == irq_gpios_dt.pin/*cfg->irq_gpios.pin*/) {
	//		irq_init_done = true;
	//		break;
	//	}

	//	if (it52xx_irq_list[i].port == NULL) {
	//		it52xx_irq_list[i] = irq_gpios_dt /*cfg->irq_gpios*/;
	//		break;
	//	}
	//}

	//if (!irq_init_done) {
		rv = gpio_pin_configure_dt(&irq_gpios_dt /*&cfg->irq_gpios*/, GPIO_INPUT);
		if (rv < 0) {
			LOG_ERR("Unable to configure GPIO");
			return rv;
		}

#if 0
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_gpio_ec_i2c_pdc0_sm_int_odl));
		//but need to declare in interrupt.dts:
		//gpio-interrupts {
		//      compatible = "cros-ec,gpio-interrupts";
		//
		//      int_gpio_ec_i2c_pdc0_sm_int_odl: gpio_ec_i2c_pdc0_sm_int_odl {
		//          irq-pin = <&gpio_ec_i2c_pdc0_sm_int_odl>;
		//          flags = <GPIO_INT_EDGE_FALLING>;
		//          handler = "pdc_interrupt_callback";
		//      };
		//};
		/* 'pdc_interrupt_callback' defined but not used */
#else
		gpio_init_callback(&gpio_cb /*&data->gpio_cb*/, pdc_interrupt_callback /*cfg->callback_handler*/,
				   BIT(irq_gpios_dt.pin /*cfg->irq_gpios.pin*/));

		rv = gpio_add_callback(irq_gpios_dt.port /*cfg->irq_gpios.port*/, &gpio_cb /*&data->gpio_cb*/);
		if (rv < 0) {
			LOG_ERR("Unable to add callback");
			return rv;
		}

		rv = gpio_pin_interrupt_configure_dt(&irq_gpios_dt /*&cfg->irq_gpios*/,
						     GPIO_INT_EDGE_FALLING);
		if (rv < 0) {
			LOG_ERR("Unable to configure interrupt");
			return rv;
		}
		LOG_ERR("init done");
#endif
		/* Trigger IRQ on startup to read any pending interrupts */
		//k_event_post(&data->driver_event, IT52XX_IRQ_EVENT);
	//}
	k_sleep(K_MSEC(TIMEOUT_MS));

	//test tx/rx i2c data

	//test tx/rx ucsi data

#if 0   //experiment: test reboot it5271
	/* CMD_TRIGGER_PDC_RESET */
	it52xx_pdc_reset(data, &i2c_dt);
	/* it52xx boot to ready */
	k_sleep(K_MSEC(200));
#endif

	/* CMD_PPM_RESET */
	it52xx_ppm_reset(/*dev*/);
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);



	/* CMD_SET_NOTIFICATION_ENABLE */
	//experiment: CMD_SET_NOTIFICATION_ENABLE -> CMD_PPM_RESET
	//why only set CMD_PPM_RESET not set CMD_SET_NOTIFICATION_ENABLE, pdc trigger INT when plug-in dongle?
	it52xx_set_notification_enable(/*data->dev, cfg->bits*/);
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#if 0   //experiment: if read CCI again before clear INT?
        //reply the same CCI event (pdc buffer data not clear? seems not clear by clear INT)
	k_sleep(K_MSEC(TIMEOUT_MS));
	get_ping_status(/*dev,*/ &i2c_dt);
#endif
#if 0   //experiment: if read CCI again after clear INT?
        //reply the same CCI event (pdc buffer data not clear?)
	k_sleep(K_MSEC(TIMEOUT_MS));
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	/* CMD_ACK_CC_CI */
	union conn_status_change_bits_t ci;
	bool cc = 1;
	ci.connect_change = 0;
	it52xx_ack_cc_ci(/*data->dev,*/ci, cc/*, vendor_defined*/);
#if 1   //experiment: if not reply this ACK_CC_CI, it5271 won't trigger int low in SET_PDO
	// (only after rx CMD_ACK_CC_CI, it5271 can handle other event)
	//UCSI driver always send CMD_ACK_CC_CI? seems Not

	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	/* CMD_GET_IC_STATUS */
	it52xx_get_info(/*data->dev,*/ &data->info/*, true*/);
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
#if 1   //experiment: check it5271 support ARA or not, when alert is low
	//            addr 7'b 0x0c => 8'b 0x18 & read => 0x19, it5271 no reply, so ara = 0x00
	uint8_t ara;
	get_ara(&i2c_dt /*data->dev*/, &ara);
	ara = (ara >> 1);
	LOG_ERR("ARA addr 0x%x, tcpc addr 0x%x", ara, i2c_dt.addr);
#endif
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
	/* [ST_READ] = read MSGIN, if data len of CCI != 0 */
	if (data->ping_status.data_len > 0) {
		it52xx_i2c_read(/*dev,*/ &i2c_dt);
		/* byte count of MSGIN */
		len = data->rd_buf[0];
		LOG_ERR("MSGIN byte count 0x%x\n", len);
		//memcpy(data->user_buf, data->rd_buf + 1/* skip byte count */, len);

		data->info.fw_version = ((data->rd_buf[6] << 8) | data->rd_buf[5]);
		data->info.pid = ((data->rd_buf[4] << 8) | data->rd_buf[3]);
		memcpy(data->info.project_name,
		       &data->rd_buf[15],
		       USB_PD_CHIP_INFO_PROJECT_NAME_LEN);
		data->info.project_name[USB_PD_CHIP_INFO_PROJECT_NAME_LEN] = '\0';
		LOG_ERR("fw_version 0x%x, pid 0x%x, %s\n", data->info.fw_version, data->info.pid, data->info.project_name);
	}



	/* CMD_ACK_CC_CI, even [MSGIN] it5271 still need */
	cc = 1;
	ci.connect_change = 0;
	it52xx_ack_cc_ci(/*data->dev,*/ci, cc/*, vendor_defined*/);
#if 1
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	/* CMD_GET_ERROR_STATUS */
	it52xx_get_error_status(/*data->dev,*/ &data->es);
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
	/* [ST_READ] = read MSGIN, if data len of CCI != 0 */
	if (data->ping_status.data_len > 0) {
		it52xx_i2c_read(/*dev,*/ &i2c_dt);
		/* byte count of MSGIN */
		len = data->rd_buf[0];
		memcpy(data->user_buf, data->rd_buf + 1/* skip byte count */, len);
		LOG_ERR("MSGIN byte count 0x%x\n", len);
		//for (uint8_t ii = 0; ii < len; ii++) {
		//	LOG_ERR("0x%x", data->user_buf[ii]);
		//}
		//LOG_ERR("\n");
	}



	/* CMD_ACK_CC_CI, even [MSGIN] it5271 still need */
	cc = 1;
	ci.connect_change = 0;
	it52xx_ack_cc_ci(/*data->dev,*/ci, cc/*, vendor_defined*/);
#if 1
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	/* CMD_SET_PDO */
	enum pdo_type_t type = SINK_PDO;
	it52xx_set_pdo(/*data->dev,*/type, pdc_snk_pdos, ARRAY_SIZE(pdc_snk_pdos));
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);



	/* CMD_ACK_CC_CI */
	cc = 1;
	ci.connect_change = 0;
	it52xx_ack_cc_ci(/*data->dev,*/ci, cc/*, vendor_defined*/);
#if 1
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	/* CMD_GET_CONNECTOR_STATUS */
	union connector_status_t cs;
	it52xx_get_connector_status(/*data->dev,*/ &cs);
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);

	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
	/* [ST_READ] = read MSGIN, if data len of CCI != 0 */
	if (data->ping_status.data_len > 0) {
		it52xx_i2c_read(/*dev,*/ &i2c_dt);
		/* byte count of MSGIN */
		len = data->rd_buf[0];
		memcpy(data->user_buf, data->rd_buf + 1/* skip byte count */, len);
		LOG_ERR("MSGIN byte count 0x%x\n", len);
		//for (uint8_t ii = 0; ii < len; ii++) {
		//	LOG_ERR("0x%x", data->user_buf[ii]);
		//}
		//LOG_ERR("\n");
	}



	/* CMD_ACK_CC_CI */
	cc = 1;
	ci.connect_change = 0; /* not plug/un-plug, CCI that it5271 reply should be 0 */
	it52xx_ack_cc_ci(/*data->dev,*/ci, cc/*, vendor_defined*/);
#if 1
	/* [ST_WRITE]: st_write_entry(&pdc_data_0); -> st_write_run(&pdc_data_0); */
	it52xx_i2c_write(/*dev,*/ &i2c_dt);
	/* handle_irqs(): read INT */
	k_sleep(K_MSEC(TIMEOUT_MS));
	do {
		it52xx_get_alert_status(/*dev,*/data, &i2c_dt);
		//LOG_ERR("INT status 0x%x", data->rd_buf[0]);
	} while((data->rd_buf[0] & IT52XX_INT_UCSI_STATUS) == 0);

	/* clear INT, do in handle_irqs() (not in [PING_STATUS] data == 0 & [ST_READ] data != 0?) */
	it52xx_clr_alert_status(/*dev,*/data, &i2c_dt);
	/* [PING_STATUS] = get CCI */
	get_ping_status(/*dev,*/ &i2c_dt);
#endif



	if (IS_ENABLED(CONFIG_TASK_HOSTCMD_THREAD_MAIN)) {
		host_command_main();
	} else if (IS_ENABLED(CONFIG_THREAD_MONITOR)) {
		/*
		 * Avoid returning so that the main stack is displayed by the
		 * "kernel stacks" shell command.
		 */
		k_sleep(K_FOREVER);
	}

	return 0;
}
/* LCOV_EXCL_STOP */
