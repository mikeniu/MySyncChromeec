/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE IT52XX Power Delivery Controller driver. */

#include "drivers/ucsi_v3.h"
#include "pdc_it52xx.h"
#include "task_id.h"
#include "usbc/pdc_utils.h"

#include <assert.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

#include <drivers/pdc.h>

LOG_MODULE_REGISTER(pdc_it52xx, CONFIG_USBC_LOG_LEVEL);

#define DT_DRV_COMPAT ite_it52xx_pdc

#define IT52XX_BYTE0(n) ((n) & 0xff)
#define IT52XX_BYTE1(n) (((n) >> 8) & 0xff)
#define IT52XX_BYTE2(n) (((n) >> 16) & 0xff)
#define IT52XX_BYTE3(n) (((n) >> 24) & 0xff)

/** Number of retries after the initial I2C transaction. */
#define N_I2C_TRANSACTION_COUNT 10

/** Number of times to try and initialize the driver. */
#define N_INIT_RETRY_ATTEMPT_MAX 2

/**
 * CONTROL command length: one command byte, one byte count, and eight data
 * bytes.
 */
#define IT52XX_CONTROL_CMD_BASE_LENGTH sizeof(struct it52xx_control_cmd)

/** IT52XX [CONTROL] command byte count. */
#define IT52XX_CONTROL_BYTE_CNT sizeof(struct ucsi_control_t)

BUILD_ASSERT(IT52XX_CONTROL_BYTE_CNT == 0x8);

/** Event posted to run the next state-machine step. */
#define IT52XX_NEXT_STATE_READY BIT(1)

/** UCSI status bit in the IT52XX interrupt status register. */
#define IT52XX_INT_UCSI_STATUS BIT(1)

/** IRQ event posted by the interrupt handler. */
#define IT52XX_IRQ_EVENT BIT(0)

/** Number of bytes in the IT52XX interrupt-status response. */
#define IT52XX_INT_READ_STATUS_LENGTH 1

/** IT52XX vendor-defined command fields used to query PDC information. */
#define IT52XX_VDM_VDC_GET_PDC_INFO 0
#define IT52XX_VDM_VDC_VER 1
#define IT52XX_VDM_VID_L 0x8d
#define IT52XX_VDM_VID_H 0x04

/** Vendor-defined command values selecting the VDO origin. */
#define IT52XX_VDM_VDC_GET_VDO_PDC 1
#define IT52XX_VDM_VDC_GET_VDO_PARTNER 2
#define IT52XX_VDM_VDC_GET_VDO_CABLE 3

/** Vendor-defined command values used to set and get an RDO. */
#define IT52XX_VDM_VDC_SET_RDO 4
#define IT52XX_VDM_VDC_GET_RDO 5

/** Vendor-defined command values used to set and get the SBU mux mode. */
#define IT52XX_VDM_VDC_SET_SBU_MUX_MODE 6
#define IT52XX_VDM_VDC_GET_SBU_MUX_MODE 7

/** Maximum SMBus block payload length. */
#define IT52XX_SMBUS_MAX_BLOCK_SIZE 32

/** Maximum time to wait for the driver to suspend communication. */
#define SUSPEND_TIMEOUT_USEC (3 * USEC_PER_SEC)

/** SET_PDO command layout. */
#define IT52XX_SET_PDO_MAX_PDO_COUNT 7
#define IT52XX_SET_PDO_CMD_BASE_LENGTH 12
#define IT52XX_SET_PDO_CMD_MAX_LENGTH     \
	(IT52XX_SET_PDO_CMD_BASE_LENGTH + \
	 sizeof(uint32_t) * IT52XX_SET_PDO_MAX_PDO_COUNT)
#define IT52XX_END_OF_MESSAGE 1

/** SET_POWER_LEVEL value selecting source power. */
#define IT52XX_SET_SRC_PWR_LVL 1

/** State-machine task timeout, in milliseconds. */
#define T_TASK_TIMEOUT_EVENT 20

/** Number of task timeouts in the 60 ms error-recovery delay. */
#define N_ERROR_RECOVERY_DELAY_COUNT (60 / T_TASK_TIMEOUT_EVENT)

/** Maximum number of error-recovery attempts. */
#define N_MAX_ERROR_RECOVERY_COUNT 4

/** Connector-status VBUS voltage scale factor, in millivolts. */
#define VOLTAGE_SCALE_FACTOR 5

/** Transition back to init or idle and finish handling the SMF event. */
#define TRANSITION_TO_INIT_OR_IDLE_STATE(data)  \
	transition_to_init_or_idle_state(data); \
	return SMF_EVENT_HANDLED

/**
 * Offsets of data fields in the GET_IC_STATUS response, based on the ITE FW
 * guide version 0.7. Data byte 0 follows the byte-count field at rd_buf[1].
 */
#define IT52XX_GET_IC_STATUS_VID_L 1
#define IT52XX_GET_IC_STATUS_VID_H 2
#define IT52XX_GET_IC_STATUS_PID_L 3
#define IT52XX_GET_IC_STATUS_PID_H 4
#define IT52XX_GET_IC_STATUS_FWVER_MINOR_INDEX 5
#define IT52XX_GET_IC_STATUS_FWVER_MAJOR_INDEX 6
#define IT52XX_GET_IC_STATUS_PD_REV_MINOR_INDEX 9
#define IT52XX_GET_IC_STATUS_PD_REV_MAJOR_INDEX 10
#define IT52XX_GET_IC_STATUS_PD_VER_MINOR_INDEX 11
#define IT52XX_GET_IC_STATUS_PD_VER_MAJOR_INDEX 12
/* UCSI is only supported by IT5271 flash code, so this value is always 1. */
#define IT52XX_GET_IC_STATUS_RUNNING_FLASH_CODE_INDEX 13
/* The running flash bank is either bank 1 or bank 2. */
#define IT52XX_GET_IC_STATUS_RUNNING_FLASH_BANK_INDEX 14
#define IT52XX_GET_IC_STATUS_PROG_NAME_STR_INDEX 15

static void it52xx_unpack_get_ic_status_response(const uint8_t *rx_buf,
						 struct pdc_info_t *info)
{
	/* ITE VID: Data Byte0..1 (little-endian) */
	info->vid = rx_buf[IT52XX_GET_IC_STATUS_VID_H] << 8 |
		    rx_buf[IT52XX_GET_IC_STATUS_VID_L];

	/* ITE PID: Data Byte2..3 (little-endian) */
	info->pid = rx_buf[IT52XX_GET_IC_STATUS_PID_H] << 8 |
		    rx_buf[IT52XX_GET_IC_STATUS_PID_L];

	/* ITE FW version: Data Byte4..5 (little-endian) */
	info->fw_version = rx_buf[IT52XX_GET_IC_STATUS_FWVER_MAJOR_INDEX]
				   << 16 |
			   rx_buf[IT52XX_GET_IC_STATUS_FWVER_MINOR_INDEX] << 8;

	/* ITE PD Revision: Data Byte8..9 (little-endian) */
	info->pd_revision = rx_buf[IT52XX_GET_IC_STATUS_PD_REV_MAJOR_INDEX]
				    << 8 |
			    rx_buf[IT52XX_GET_IC_STATUS_PD_REV_MINOR_INDEX];

	/* ITE PD Version: Data Byte10..11 (little-endian) */
	info->pd_version = rx_buf[IT52XX_GET_IC_STATUS_PD_VER_MAJOR_INDEX]
				   << 8 |
			   rx_buf[IT52XX_GET_IC_STATUS_PD_VER_MINOR_INDEX];

	/* ITE is running flash code: Data Byte12 */
	info->is_running_flash_code =
		rx_buf[IT52XX_GET_IC_STATUS_RUNNING_FLASH_CODE_INDEX];

	/* ITE running flash bank offset: Data Byte13 */
	info->running_in_flash_bank =
		rx_buf[IT52XX_GET_IC_STATUS_RUNNING_FLASH_BANK_INDEX];
}

/** IT52XX SMBus command descriptor. */
struct smbus_cmd_t {
	/* Command ex. [CONTROL], [MSGOUT], [CCI], [MSGIN] */
	uint8_t cmd;
	/* Byte count */
	uint8_t len;
	/* Sub-Command */
	uint8_t sub;
};

struct it52xx_control_cmd {
	/* [CONTROL] command. */
	uint8_t ctrl_cmd;
	/* [CONTROL] command byte count. */
	uint8_t len;
	/*
	 * UCSI command (ex. GET_CONNECTOR_STATUS), data length,
	 * and command-specific fields.
	 */
	struct ucsi_control_t control;
} __packed;

/** ITE SMBus commands. */
static const struct smbus_cmd_t GET_ALERT_STATUS = { 0xBD, 0x01 };
static const struct smbus_cmd_t CLR_ALERT_STATUS = { 0xBC };
static const struct smbus_cmd_t SET_CONTROL = { 0x81 };
static const struct smbus_cmd_t SET_MSGOUT = { 0x83 };
static const struct smbus_cmd_t GET_CCI = { 0x80 };
static const struct smbus_cmd_t GET_MSGIN = { 0x82 };

static const struct smbus_cmd_t SET_PPM_RESET = { 0x81, IT52XX_CONTROL_BYTE_CNT,
						  UCSI_PPM_RESET };
static const struct smbus_cmd_t SET_NOTIFICATION_ENABLE = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_SET_NOTIFICATION_ENABLE
};
static const struct smbus_cmd_t GET_IC_STATUS = { 0x81, IT52XX_CONTROL_BYTE_CNT,
						  UCSI_VENDOR_DEFINED_COMMAND };
static const struct smbus_cmd_t GET_ERROR_STATUS = { 0x81,
						     IT52XX_CONTROL_BYTE_CNT,
						     UCSI_GET_ERROR_STATUS };
static const struct smbus_cmd_t SET_PDO = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_SET_PDOS };
static const struct smbus_cmd_t GET_CONNECTOR_STATUS = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_GET_CONNECTOR_STATUS
};
static const struct smbus_cmd_t SET_SINK_PATH = { 0x81, IT52XX_CONTROL_BYTE_CNT,
						  UCSI_SET_SINK_PATH };
static const struct smbus_cmd_t SET_CCOM = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					     UCSI_SET_CCOM };
static const struct smbus_cmd_t SET_TPC_RP = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					       UCSI_SET_POWER_LEVEL };
static const struct smbus_cmd_t READ_POWER_LEVEL = { 0x81,
						     IT52XX_CONTROL_BYTE_CNT,
						     UCSI_READ_POWER_LEVEL };
static const struct smbus_cmd_t GET_CONNECTOR_CAPABILITY = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_GET_CONNECTOR_CAPABILITY
};
static const struct smbus_cmd_t SET_UOR = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_SET_UOR };
static const struct smbus_cmd_t SET_PDR = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_SET_PDR };
static const struct smbus_cmd_t GET_VDO = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_VENDOR_DEFINED_COMMAND };
static const struct smbus_cmd_t GET_PDOS = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					     UCSI_GET_PDOS };
static const struct smbus_cmd_t SET_RDO = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_VENDOR_DEFINED_COMMAND };
static const struct smbus_cmd_t GET_CABLE_PROPERTY = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_GET_CABLE_PROPERTY
};
static const struct smbus_cmd_t GET_RDO = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					    UCSI_VENDOR_DEFINED_COMMAND };
static const struct smbus_cmd_t SET_CONNECTOR_RESET = { 0x81,
							IT52XX_CONTROL_BYTE_CNT,
							UCSI_CONNECTOR_RESET };
static const struct smbus_cmd_t GET_CAPABILITY = { 0x81,
						   IT52XX_CONTROL_BYTE_CNT,
						   UCSI_GET_CAPABILITY };
static const struct smbus_cmd_t GET_LPM_PPM_INFO = { 0x81,
						     IT52XX_CONTROL_BYTE_CNT,
						     UCSI_GET_LPM_PPM_INFO };
static const struct smbus_cmd_t GET_ATTENTION_VDO = { 0x81,
						      IT52XX_CONTROL_BYTE_CNT,
						      UCSI_GET_ATTENTION_VDO };
static const struct smbus_cmd_t SET_SBU_MUX_MODE = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_VENDOR_DEFINED_COMMAND
};
static const struct smbus_cmd_t GET_SBU_MUX_MODE = {
	0x81, IT52XX_CONTROL_BYTE_CNT, UCSI_VENDOR_DEFINED_COMMAND
};

static const struct smbus_cmd_t ACK_CC_CI = { 0x81, IT52XX_CONTROL_BYTE_CNT,
					      UCSI_ACK_CC_CI };

/**
 * @brief States of the main state machine
 */
enum state_t {
	/** Init State */
	ST_INIT,
	/** Idle State */
	ST_IDLE,
	/** Write State */
	ST_WRITE,
	/** Write ACK State */
	ST_WRITE_ACK,
	/** Task Wait State */
	ST_TASK_WAIT,
	/** Get CCI State */
	ST_GET_CCI,
	/** Read State */
	ST_READ,
	/** Error Recovery State */
	ST_ERROR_RECOVERY,
	/** Disable State */
	ST_DISABLE,
	/** PDC communication suspended */
	ST_SUSPENDED,
};

/**
 * @brief Init sub-states
 */
enum init_state_t {
	/** Set the PDC PPM reset */
	INIT_PDC_SET_PPM_RESET,
	/** Set the PDC Notifications */
	INIT_PDC_SET_NOTIFICATION_ENABLE,
	/** Get the PDC IC Status by VDM */
	INIT_PDC_GET_VDM_IC_STATUS,
	/** Initialization complete */
	INIT_PDC_COMPLETE,
	/** Initialization error */
	INIT_ERROR,
	/** Wait for command to send */
	INIT_PDC_CMD_WAIT
};

/**
 * @brief PDC commands
 */
enum cmd_t {
	/** No command */
	CMD_NONE,
	/** CMD_TRIGGER_PDC_RESET */
	CMD_TRIGGER_PDC_RESET,
	/** Set PPM Reset */
	CMD_PPM_RESET,
	/** Set Notification Enable */
	CMD_SET_NOTIFICATION_ENABLE,
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
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
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
	/** CMD_ACK_CC_CI */
	CMD_ACK_CC_CI,
	/** Raw UCSI call.
	 * Special handling of the data read from a PDC will be skipped. */
	CMD_RAW_UCSI,
	/** CMD_GET_LPM_PPM_INFO */
	CMD_GET_LPM_PPM_INFO,
	/** CMD_GET_ATTENTION_VDO */
	CMD_GET_ATTENTION_VDO,
	/** CMD_SET_SBU_MUX_MODE */
	CMD_SET_SBU_MUX_MODE,
	/** CMD_GET_SBU_MUX_MODE */
	CMD_GET_SBU_MUX_MODE,
};

/**
 * @brief PDC Config object
 */
struct pdc_config_t {
	/** I2C config */
	struct i2c_dt_spec i2c;
	/** pdc power path interrupt */
	struct gpio_dt_spec irq_gpios;
	/** connector number of this port on board (start from 0) */
	uint8_t connector_number;
	/** Notification enable bits */
	union notification_enable_t bits;
	/** Create thread function */
	void (*create_thread)(const struct device *dev);
	/** If true, do not apply PDC FW updates to this port */
	bool no_fw_update;
	/** Whether or not this port supports CCD */
	bool ccd;
	/** Whether or not this port supports FRS */
	bool frs_supported;
	/** Pointer to the device-specific callback function */
	gpio_callback_handler_t callback_handler;
	/** The index of port number on chip (start from 1) */
	uint8_t port_index_on_chip;
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
	/** State machine context */
	struct smf_ctx ctx;
	/** Init's local state variable */
	enum init_state_t init_local_state;
	/** Init's current state */
	enum init_state_t init_local_current_state;
	/** Init's next state */
	enum init_state_t init_local_next_state;
	/** PDC's last state */
	enum state_t last_state;
	/** PDC device structure */
	const struct device *dev;
	/** PDC command */
	enum cmd_t cmd;
	/** Driver thread */
	k_tid_t thread;
	/** Driver thread's data */
	struct k_thread thread_data;
	/** [CCI] message from PDC */
	union cci_event_t cci_message;
	/** [CCI] byte count. */
	uint8_t cci_byte_cnt;
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
	struct k_mutex mtx;
	/** GPIO interrupt callback */
	struct gpio_callback gpio_cb;
	/** Error status */
	union error_status_t error_status;
	/** CCI Event for PPM */
	union cci_event_t cci_event;
	/** CCI event for an initialization command (except ACK_CC_CI) */
	union cci_event_t init_cci_event;
	/** Callback to handle commands complete indicator in interrupt */
	struct pdc_callback *cc_cb;
	/** One-time callback to handle commands complete indicator in interrupt
	 */
	struct pdc_callback *cc_cb_tmp;
	/** Callbacks to handle connect change & vendor indicator in interrupt
	 */
	sys_slist_t ci_cb_list;
	/** When connect change indicator interrupt is fired, raise this flag to
	 * 1 */
	bool ci_interrupt;
	/** This value would be set to CMD_ACK_CC_CI "change indicator" field.
	 */
	bool ack_ci;
	/** Information about the PDC */
	struct pdc_info_t info;
	/** True after all cached chip info fields are filled. Protected by mtx.
	 */
	bool info_valid;
	/** Cached connector status for this connector. */
	union connector_status_t cached_conn_status;
	/** Init done flag */
	bool init_done;
	/** Error recovery delay counter */
	uint16_t error_recovery_delay_counter;
	/** Error recovery counter */
	uint16_t error_recovery_counter;
	/** Error Status used during initialization */
	union error_status_t es;
	/* Driver specific events to handle */
	struct k_event driver_event;
	/* Currently running UCSI command */
	enum ucsi_command_t active_ucsi_cmd;
};

/**
 * @brief Name of the command, used for debugging
 */
static const char *const cmd_names[] = {
	[CMD_NONE] = "",
	[CMD_TRIGGER_PDC_RESET] = "TRIGGER_PDC_RESET",
	[CMD_PPM_RESET] = "PPM_RESET",
	[CMD_SET_NOTIFICATION_ENABLE] = "SET_NOTIFICATION_ENABLE",
	[CMD_CONNECTOR_RESET] = "CONNECTOR_RESET",
	[CMD_GET_CAPABILITY] = "GET_CAPABILITY",
	[CMD_GET_CONNECTOR_CAPABILITY] = "GET_CONNECTOR_CAPABILITY",
	[CMD_SET_UOR] = "SET_UOR",
	[CMD_SET_PDR] = "SET_PDR",
	[CMD_GET_PDOS] = "GET_PDOS",
	[CMD_GET_CONNECTOR_STATUS] = "GET_CONNECTOR_STATUS",
	[CMD_GET_ERROR_STATUS] = "GET_ERROR_STATUS",
	[CMD_GET_VBUS_VOLTAGE] = "GET_VBUS_VOLTAGE",
	[CMD_GET_IC_STATUS] = "GET_IC_STATUS",
	[CMD_SET_CCOM] = "SET_CCOM",
	[CMD_GET_DRP_MODE] = "GET_DRP_MODE",
	[CMD_SET_SINK_PATH] = "SET_SINK_PATH",
	[CMD_READ_POWER_LEVEL] = "READ_POWER_LEVEL",
	[CMD_GET_RDO] = "GET_RDO",
	[CMD_SET_TPC_RP] = "SET_TPC_RP",
	[CMD_SET_RDO] = "SET_RDO",
	[CMD_GET_CABLE_PROPERTY] = "GET_CABLE_PROPERTY",
	[CMD_GET_VDO] = "GET_VDO",
	[CMD_GET_IDENTITY_DISCOVERY] = "CMD_GET_IDENTITY_DISCOVERY",
	[CMD_GET_IS_VCONN_SOURCING] = "CMD_GET_IS_VCONN_SOURCING",
	[CMD_SET_PDO] = "CMD_SET_PDO",
	[CMD_ACK_CC_CI] = "CMD_ACK_CC_CI",
	[CMD_RAW_UCSI] = "CMD_RAW_UCSI",
	[CMD_GET_LPM_PPM_INFO] = "CMD_GET_LPM_PPM_INFO",
	[CMD_GET_ATTENTION_VDO] = "CMD_GET_ATTENTION_VDO",
	[CMD_SET_SBU_MUX_MODE] = "CMD_SET_SBU_MUX_MODE",
	[CMD_GET_SBU_MUX_MODE] = "CMD_GET_SBU_MUX_MODE",
};

/**
 * @brief List of human readable state names for console debugging
 */
static const char *const state_names[] = {
	[ST_INIT] = "INIT",
	[ST_IDLE] = "IDLE",
	[ST_WRITE] = "WRITE",
	[ST_WRITE_ACK] = "WRITE_ACK",
	[ST_TASK_WAIT] = "TASK_WAIT",
	[ST_GET_CCI] = "GET_CCI",
	[ST_READ] = "READ",
	[ST_ERROR_RECOVERY] = "ERROR_RECOVERY",
	[ST_DISABLE] = "PDC_DISABLED",
	[ST_SUSPENDED] = "PDC_SUSPENDED",
};

static const struct smf_state states[];
static int it52xx_ppm_reset(const struct device *dev);
static int it52xx_set_notification_enable(const struct device *dev,
					  union notification_enable_t bits);
static int it52xx_ack_cc_ci(const struct device *dev,
			    union conn_status_change_bits_t ci, bool cc,
			    uint16_t vendor_defined);
static int it52xx_get_info(const struct device *dev, struct pdc_info_t *info,
			   bool live);
static int it52xx_get_error_status(const struct device *dev,
				   union error_status_t *es);
static int it52xx_get_alert_status(const struct device *dev);
static int it52xx_clr_alert_status(const struct device *dev, uint8_t status);

static enum state_t get_state(struct pdc_data_t *data)
{
	return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
	data->last_state = get_state(data);
	smf_set_state(SMF_CTX(data), &states[next_state]);
	k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);
}

/**
 * Atomic flag to suspend sending new commands to chip
 *
 * This flag is shared across driver instances.
 */
static atomic_t suspend_comms_flag = ATOMIC_INIT(0);

static void suspend_comms(void)
{
	atomic_set(&suspend_comms_flag, 1);
}

static void enable_comms(void)
{
	atomic_set(&suspend_comms_flag, 0);
}

static bool check_comms_suspended(void)
{
	return atomic_get(&suspend_comms_flag) != 0;
}

static void print_current_state(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	int st = get_state(data);

	if ((st == ST_WRITE) || (st == ST_WRITE_ACK)) {
		if (data->cmd == CMD_RAW_UCSI) {
			LOG_INF("ITE%d: %s RAW:%s", cfg->connector_number,
				state_names[st],
				get_ucsi_command_name(data->active_ucsi_cmd));
		} else {
			LOG_INF("ITE%d: %s %s", cfg->connector_number,
				state_names[st], cmd_names[data->cmd]);
		}
	} else if (st == ST_ERROR_RECOVERY) {
		LOG_INF("ITE%d: %s %s %d", cfg->connector_number,
			state_names[st], cmd_names[data->cmd],
			data->error_recovery_counter);
	} else {
		LOG_INF("ITE%d: %s", cfg->connector_number,
			state_names[get_state(data)]);
	}
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	const union cci_event_t cci = data->cci_event;

	if (!data->init_done) {
		/*
		 * Preserve the initialization command result (except ACK_CC_CI)
		 * while the driver completes the internal ACK_CC_CI
		 * transaction.
		 */
		if (data->init_local_state == INIT_PDC_CMD_WAIT &&
		    data->cmd != CMD_ACK_CC_CI) {
			data->init_cci_event = cci;
		}
		return;
	}

	/*
	 * Report CCI event to PPM, this value may not be the same as [CCI] of
	 * LPM replied. ex. ACK_CC_CI
	 */
	LOG_INF("ITE%d: CCI event to PPM=0x%x", cfg->connector_number,
		cci.raw_value);

	/*
	 * CC and CI events are separately reported. So, we need to call only
	 * one callback or the other.
	 */
	if (cci.connector_change) {
		/* callback to pdc_ci_handler_cb() */
		pdc_fire_callbacks(&data->ci_cb_list, data->dev, cci);
	} else if (data->cc_cb_tmp) {
		data->cc_cb_tmp->handler(data->dev, data->cc_cb_tmp, cci);
		data->cc_cb_tmp = NULL;
	} else if (data->cc_cb) {
		/* callback to pdc_cc_handler_cb() */
		data->cc_cb->handler(data->dev, data->cc_cb, cci);
	}

	data->cci_event.raw_value = 0;
}

static void perform_pdc_init(struct pdc_data_t *data)
{
	/* Do not return old chip info while a new initialization is running. */
	k_mutex_lock(&data->mtx, K_FOREVER);
	data->info_valid = false;
	k_mutex_unlock(&data->mtx);

	data->init_retry_counter = 0;
	data->error_status.raw_value = 0;
	/* Set initial local state of Init */
	data->init_local_state = INIT_PDC_SET_PPM_RESET;
	set_state(data, ST_INIT);
}

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
		LOG_ERR("ITE%d: %s i2c error", cfg->connector_number,
			(type & I2C_MSG_READ) ? "Read" : "Write");
		/*
		 * The command was not successfully completed,
		 * so set cci_event.error to 1b.
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

/**
 * @brief This function performs a state change, so a return should
 * be placed after its immediate call.
 */
static void transition_to_init_or_idle_state(struct pdc_data_t *data)
{
	if (data->init_done) {
		set_state(data, ST_IDLE);
	} else {
		set_state(data, ST_INIT);
	}
}

/**
 * @brief Get [CCI] message
 */
static int get_cci_message(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t cmd = GET_CCI.cmd;
	uint8_t response[IT52XX_PDC_MAX_CCI_LENGTH];
	int rv;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = response;
	/* W [CCI] - R [04 xx xx xx xx] => length = [5] */
	msg[1].len = IT52XX_PDC_MAX_CCI_LENGTH;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	rv = i2c_transfer_dt(&cfg->i2c, msg, 2);
	if (rv < 0) {
		return rv;
	}

	data->cci_byte_cnt = response[0];
	/* Save [CCI] data except [CCI] byte count field. */
	data->cci_message.raw_value =
		sys_get_le32(response + IT52XX_PDC_CCI_BYTE_CNT_LEN);

	return rv;
}

/**
 * @brief Get [MSGIN] message
 */
static int it52xx_i2c_read(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t cmd = GET_MSGIN.cmd;
	int rv;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->rd_buf;
	/*
	 * [MSGIN] byte count field is equal to [CCI] data length field.
	 * (+ 1 for byte count field)
	 */
	msg[1].len = data->cci_message.data_len + 1;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	rv = i2c_transfer_dt(&cfg->i2c, msg, 2);
	if (rv < 0) {
		return rv;
	}

	/* Not include byte count field */
	data->rd_buf_len = data->cci_message.data_len;

	if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
		pdc_trace_msg_resp(cfg->connector_number,
				   PDC_TRACE_CHIP_TYPE_IT52XX, data->rd_buf,
				   data->cci_message.data_len + 1);
	}

	return rv;
}

/**
 * @brief Set [CONTROL] or [MSGOUT] + [CONTROL] message
 */
static int it52xx_i2c_write(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;

	/* Transmit [MSGOUT] data + [CONTROL] commands */
	if (data->cmd == CMD_SET_PDO) {
		struct i2c_msg msg[2];
		uint32_t len = IT52XX_CONTROL_CMD_BASE_LENGTH;
		int rv;

		/* IT52XX follows UCSI v3: [MSGOUT] -> [CONTROL]. */
		msg[1].buf = &data->wr_buf[IT52XX_CONTROL_CMD_BASE_LENGTH];
		/* [MSGOUT] length = total length - [CONTROL] command length */
		msg[1].len = (data->wr_buf_len - len);
		msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		rv = i2c_transfer_dt(&cfg->i2c, &msg[1], 1);
		if (rv < 0) {
			return rv;
		}

		msg[0].buf = data->wr_buf;
		/* [CONTROL] commands length */
		msg[0].len = len;
		msg[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		return i2c_transfer_dt(&cfg->i2c, &msg[0], 1);
	} else {
		struct i2c_msg msg;

		msg.buf = data->wr_buf;
		msg.len = data->wr_buf_len;
		msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		return i2c_transfer_dt(&cfg->i2c, &msg, 1);
	}
}

static void st_init_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	data->init_done = false;
	/* pdc_init_failed is cleared when init process is complete */
	data->es.pdc_init_failed = 1;
	data->cmd = CMD_NONE;
}

static void init_write_cmd_and_change_state(struct pdc_data_t *data,
					    enum init_state_t next)
{
	data->init_cci_event.raw_value = 0;
	data->init_local_current_state = data->init_local_state;
	data->init_local_next_state = next;
	data->init_local_state = INIT_PDC_CMD_WAIT;
	set_state(data, ST_WRITE);
}

static bool init_command_tx_complete(struct pdc_data_t *data)
{
	/* If PDC_RESET was sent, check the reset_completed flag */
	if (data->init_local_current_state == INIT_PDC_SET_PPM_RESET) {
		return data->init_cci_event.reset_completed;
	}

	return data->cci_event.command_completed;
}

static void init_display_error_status(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	int cnum = cfg->connector_number;

	if (data->es.unrecognized_command) {
		LOG_ERR("ITE%d: Unrecognized Command", cnum);
	}

	if (data->es.non_existent_connector_number) {
		LOG_ERR("ITE%d: Invalid Connector Number", cnum);
	}

	if (data->es.invalid_command_specific_param) {
		LOG_ERR("ITE%d: Invalid Param", cnum);
	}

	if (data->es.incompatible_connector_partner) {
		LOG_ERR("ITE%d: Invalid Connector Partner", cnum);
	}

	if (data->es.cc_communication_error) {
		LOG_ERR("ITE%d: CC Comm Error", cnum);
	}

	if (data->es.cmd_unsuccessful_dead_batt) {
		LOG_ERR("ITE%d: Dead Batt Error", cnum);
	}

	if (data->es.contract_negotiation_failed) {
		LOG_ERR("ITE%d: Contract Negotiation Failed", cnum);
	}
}

static enum smf_state_result init_handle_command_result(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;

	/* [CCI] raise error indicator */
	if (data->init_cci_event.error) {
		/* I2C read Error. No way to recover, so disable the PDC
		 */
		if (data->error_status.i2c_read_error) {
			LOG_INF("ITE%d: PDC I2C problem",
				cfg->connector_number);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}

		/* PDC not responding to Get [CCI]. Try error recovery
		 */
		if (data->error_status.pdc_internal_error) {
			LOG_INF("ITE%d: PDC not responding",
				cfg->connector_number);
			set_state(data, ST_ERROR_RECOVERY);
			return SMF_EVENT_HANDLED;
		}

		/* PDC not responding to Error Status reads. Try error
		 * recovery */
		if (data->init_local_current_state == INIT_ERROR) {
			LOG_INF("ITE%d: PDC error status read fail",
				cfg->connector_number);
			set_state(data, ST_ERROR_RECOVERY);
			return SMF_EVENT_HANDLED;
		}

		/* PDC returned an error */
		data->init_local_state = INIT_ERROR;
	} else {
		/* [CCI] raise complete indicator */

		/* If we comes from GET_ERROR_STATUS cmd */
		if (data->init_local_current_state == INIT_ERROR) {
			/* Display error read from cci_message */
			init_display_error_status(data);
			/* Retry init or disable this port */
			if (data->init_retry_counter <=
			    N_INIT_RETRY_ATTEMPT_MAX) {
				data->init_retry_counter++;
				data->init_local_state = INIT_PDC_SET_PPM_RESET;
			} else {
				set_state(data, ST_DISABLE);
			}
			return SMF_EVENT_HANDLED;
		}

		data->init_local_state = data->init_local_next_state;
	}

	return SMF_EVENT_HANDLED;
}

static enum smf_state_result st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	int cnum = cfg->connector_number;
	int rv;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	switch (data->init_local_state) {
	case INIT_PDC_SET_PPM_RESET:
		/*
		 * it52xx ignores commands which before ppm_reset, so at init,
		 * it needs to ppm_reset first, then set_notification.
		 */
		rv = it52xx_ppm_reset(data->dev);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_PPM_RESET)",
				cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(
			data, INIT_PDC_SET_NOTIFICATION_ENABLE);
		return SMF_EVENT_HANDLED;
	case INIT_PDC_SET_NOTIFICATION_ENABLE:
		rv = it52xx_set_notification_enable(data->dev, cfg->bits);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_NOTIFICATION_ENABLE)",
				cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data,
						INIT_PDC_GET_VDM_IC_STATUS);
		return SMF_EVENT_HANDLED;
	case INIT_PDC_GET_VDM_IC_STATUS:
		/* it52xx supports IC status by UCSI VDM command */
		rv = it52xx_get_info(data->dev, &data->info, true);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_GET_VDM_IC_STATUS)",
				cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_COMPLETE);
		return SMF_EVENT_HANDLED;
	case INIT_PDC_COMPLETE:
		data->es.pdc_init_failed = 0;
		/* Init is complete, so transition to Idle state */
		set_state(data, ST_IDLE);
		data->init_done = true;
		return SMF_EVENT_HANDLED;
	case INIT_ERROR:
		/* Get error status, and re-start the init process */
		it52xx_get_error_status(data->dev, &data->es);
		init_write_cmd_and_change_state(data, INIT_PDC_SET_PPM_RESET);
		return SMF_EVENT_HANDLED;
	case INIT_PDC_CMD_WAIT:
		if (!init_command_tx_complete(data)) {
			return SMF_EVENT_HANDLED;
		}

		return init_handle_command_result(data);
	}

	return SMF_EVENT_HANDLED;
}

static void set_cci_error_event(struct pdc_data_t *data, int type)
{
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/*
	 * The command was not successfully completed,
	 * so set cci_event.error to 1b.
	 */
	data->cci_event.error = 1;
	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Clear busy event */
	data->cci_event.busy = 0;

	if (type & I2C_MSG_READ) {
		data->error_status.i2c_read_error = 1;
	} else {
		data->error_status.i2c_write_error = 1;
	}
	/* Notify system of status change */
	call_cci_event_cb(data);
}

/**
 * @brief Called from the driver thread to handle interrupts
 */
static void handle_irqs(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	uint8_t interrupt_status;
	int rv;

	if (!device_is_ready(data->dev)) {
		/* This PDC instance is not ready. Ignore the interrupt. */
		return;
	}

	/* Read interrupt status register */
	rv = it52xx_get_alert_status(data->dev);
	if (rv) {
		LOG_ERR("ITE%d: IRQ Get interrupt status failed",
			cfg->connector_number);
		set_cci_error_event(data, I2C_MSG_READ);
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}
	interrupt_status = data->rd_buf[0];

	/* We only handle UCSI interrupt events */
	if (interrupt_status & IT52XX_INT_UCSI_STATUS) {
		if (get_state(data) == ST_TASK_WAIT) {
			/* We're waiting [CCI] message in ST_TASK_WAIT state */
			LOG_INF("ITE%d: IRQ-CC", cfg->connector_number);
			set_state(data, ST_GET_CCI);
			return;
		} else {
			LOG_INF("ITE%d: IRQ-CI", cfg->connector_number);
			/* Inform subsystem of the ci interrupt */
			data->ci_interrupt = 1;
			/* Clear the CCI Event */
			data->cci_event.raw_value = 0;
			/* Set the port the CCI Event occurred on */
			data->cci_event.connector_change =
				cfg->connector_number + 1;
			/*
			 * Set CCI EVENT for vendor defined indicator (informs
			 * subsystem that an interrupt occurred)
			 */
			data->cci_event.vendor_defined_indicator = 1;

			/* Notify system of status change */
			call_cci_event_cb(data);
			set_state(data, ST_GET_CCI);
			return;
		}
	} else {
		LOG_INF("ITE%d: IRQ-unknown event", cfg->connector_number);
		/* Just clear status, we don't handle non-UCSI events. */
		rv = it52xx_clr_alert_status(data->dev, interrupt_status);
		if (rv) {
			LOG_ERR("ITE%d: Clear interrupt status failed",
				cfg->connector_number);
			set_cci_error_event(data, I2C_MSG_WRITE);
			set_state(data, ST_ERROR_RECOVERY);
			return;
		}
	}
}

static void st_idle_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	data->cmd = CMD_NONE;
	data->active_ucsi_cmd = 0;
}

static enum smf_state_result st_idle_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	/*
	 * Priority of events:
	 *  1: CMD_TRIGGER_PDC_RESET
	 *  2: Non-Reset command
	 */
	if (data->cmd == CMD_TRIGGER_PDC_RESET) {
		perform_pdc_init(data);
	} else if (data->cmd != CMD_NONE) {
		set_state(data, ST_WRITE);
	}

	return SMF_EVENT_HANDLED;
}

static void st_write_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Init and Idle states */
	assert(data->last_state == ST_INIT || data->last_state == ST_IDLE);

	/* Clear I2C transaction retry counter */
	data->i2c_transaction_retry_counter = 0;
	/* Only clear Error Status if the subsystem isn't going to read it */
	if (data->cmd != CMD_GET_ERROR_STATUS) {
		/* Clear the Error Status */
		data->error_status.raw_value = 0;
	}
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static enum smf_state_result st_write_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	int rv;

	/* Write the [CONTROL] or [MSGOUT] + [CONTROL] command */
	rv = it52xx_i2c_write(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	/*
	 * I2C transaction succeeded. Next, wait for an interrupt, then
	 * get [CCI] message.
	 */
	set_state(data, ST_TASK_WAIT);

	return SMF_EVENT_HANDLED;
}

static void st_write_ack_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Get CCI and Read states */
	assert(data->last_state == ST_GET_CCI || data->last_state == ST_READ);

	/* Clear I2C transaction retry counter */
	data->i2c_transaction_retry_counter = 0;
	/* Only clear Error Status if the subsystem isn't going to read it */
	if (data->cmd != CMD_GET_ERROR_STATUS) {
		/* Clear the Error Status */
		data->error_status.raw_value = 0;
	}
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static enum smf_state_result st_write_ack_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	int rv;

	/* Write the [CONTROL] ACK_CC_CI command */
	rv = it52xx_i2c_write(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	set_state(data, ST_TASK_WAIT);

	return SMF_EVENT_HANDLED;
}

static void st_task_wait_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* Entered only from ST_WRITE, ST_WRITE_ACK, or ST_GET_CCI. */
	assert(data->last_state == ST_WRITE ||
	       data->last_state == ST_WRITE_ACK ||
	       data->last_state == ST_GET_CCI);
}

static enum smf_state_result st_task_wait_run(void *o)
{
	/*
	 * IT5271 firmware guarantees a UCSI interrupt for every command
	 * response. The IRQ handler advances the state to ST_GET_CCI.
	 */
	return SMF_EVENT_HANDLED;
}

static void st_get_cci_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Task Wait or Idle state */
	assert(data->last_state == ST_TASK_WAIT || data->last_state == ST_IDLE);

	/* Clear I2C Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Clear [CCI] Message */
	data->cci_byte_cnt = 0;
	data->cci_message.raw_value = 0;
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static enum smf_state_result st_get_cci_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	union conn_status_change_bits_t ci = { 0 };
	const bool cc = true;
	int rv;

	/* Read the [CCI] message */
	rv = get_cci_message(data->dev);

	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	if (data->cci_message.data_len == 0) {
		/*
		 * If there is no [MSGIN], then clear UCSI interrupt event
		 * after get [CCI] message, and this also clear it52xx
		 * CCI message memory.
		 */
		rv = it52xx_clr_alert_status(data->dev, IT52XX_INT_UCSI_STATUS);

		if (rv < 0) {
			if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
				set_state(data, ST_ERROR_RECOVERY);
			}
			return SMF_EVENT_HANDLED;
		}
	}

	/*
	 * If we comes from change indicator interrupt (asynchronous event),
	 * then just read [CCI] and needn't send ACK_CC_CI to it5271, then PPM
	 * would trigger a CMD_GET_CONNECTOR_STATUS immediately.
	 */
	if (data->ci_interrupt) {
		data->ci_interrupt = 0;
		data->ack_ci = 1;
		LOG_INF("ITE%d: CI INT [CCI] = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }",
			cfg->connector_number, data->cci_byte_cnt,
			IT52XX_BYTE0(data->cci_message.raw_value),
			IT52XX_BYTE1(data->cci_message.raw_value),
			IT52XX_BYTE2(data->cci_message.raw_value),
			IT52XX_BYTE3(data->cci_message.raw_value));
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
	}

	const uint8_t cmd_sts = IT52XX_BYTE3(data->cci_message.raw_value);

	switch (cmd_sts) {
	case IT52XX_CMD_BUSY:
		LOG_ERR("ITE%d: [CCI] Busy indicator", cfg->connector_number);
		/*
		 * If Busy, then set this cci_event.busy to a 1b and all other
		 * fields to zero.
		 */
		data->cci_event.busy = 1;
		/* Notify system of status change */
		call_cci_event_cb(data);
		/*
		 * Once it52xx processes the [CONTROL] commands, it will trigger
		 * interrupt again.
		 */
		set_state(data, ST_TASK_WAIT);
		break;
	case IT52XX_CMD_RESET_COMPLETE:
	case IT52XX_CMD_ACK_COMPLETE:
	case IT52XX_CMD_DONE:
		/* Clear busy event */
		data->cci_event.busy = 0;

		/* If execute ppm reset command */
		if (data->cmd == CMD_PPM_RESET) {
			/* Set cci_event.reset_completed to 1b */
			data->cci_event.reset_completed = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);
			LOG_INF("ITE%d: [CCI] PPM reset PDC complete",
				cfg->connector_number);
			/* All done, return to Init or Idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		}

		LOG_INF("ITE%d: [CCI] = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }",
			cfg->connector_number, data->cci_byte_cnt,
			IT52XX_BYTE0(data->cci_message.raw_value),
			IT52XX_BYTE1(data->cci_message.raw_value),
			IT52XX_BYTE2(data->cci_message.raw_value),
			IT52XX_BYTE3(data->cci_message.raw_value));

		/*
		 * The command completed successfully,
		 * so set cci_event.command_completed to 1b.
		 */
		data->cci_event.command_completed = 1;

		/* If there are [MSGIN] messages that need to be read */
		if (data->cci_message.data_len > 0) {
			set_state(data, ST_READ);
			return SMF_EVENT_HANDLED;
		}

		/* Inform the system of the event */
		call_cci_event_cb(data);

		if (data->cmd != CMD_ACK_CC_CI) {
			/*
			 * It needs to write ACK_CC_CI following [CONTROL]
			 * command to it52xx (except CMD_PPM_RESET).
			 */
			data->cmd = CMD_NONE;
			ci.connect_change = 0;
			rv = it52xx_ack_cc_ci(data->dev, ci, cc, 0);
			if (rv) {
				LOG_ERR("ITE%d: Put ACK_CC_CI to buffer fail",
					cfg->connector_number);
				set_state(data, ST_ERROR_RECOVERY);
				return SMF_EVENT_HANDLED;
			}
			set_state(data, ST_WRITE_ACK);
		} else {
			/*
			 * Already send ACK_CC_CI, return to Idle or Init state
			 */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		}

		return SMF_EVENT_HANDLED;
	case IT52XX_CMD_ERROR:
	case IT52XX_CMD_NOT_SUPPORTED:
	case IT52XX_CMD_NOT_SUPPORTED_ERROR:
		if (cmd_sts == IT52XX_CMD_NOT_SUPPORTED) {
			data->cci_event.not_supported = 1;
			data->cci_event.error = 0;
			LOG_ERR("ITE%d: [CCI] Not-supported indicator",
				cfg->connector_number);
		} else if (cmd_sts == IT52XX_CMD_ERROR) {
			data->cci_event.not_supported = 0;
			data->cci_event.error = 1;
			LOG_ERR("ITE%d: [CCI] Error indicator",
				cfg->connector_number);
		} else {
			data->cci_event.not_supported = 1;
			data->cci_event.error = 1;
			LOG_ERR("ITE%d: [CCI] Unrecognized command",
				cfg->connector_number);
		}
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* Clear busy event */
		data->cci_event.busy = 0;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/*
		 * If the error indicator is set, the subsystem should read the
		 * status register to determine the cause.
		 *
		 * PPM_RESET is always handled by IT5271 firmware and
		 * [CCI] reports IT52XX_CMD_RESET_COMPLETE, as required by UCSI.
		 */
		if (data->cmd != CMD_PPM_RESET) {
			/*
			 * It needs to write ACK_CC_CI following
			 * [CONTROL] command to it52xx (except CMD_PPM_RESET).
			 * [CCI] of ACK_CC_CI can't fill up error indicator,
			 * so [CCI] of ACK_CC_CI never goes to here.
			 */
			data->cmd = CMD_NONE;
			ci.connect_change = 0;
			rv = it52xx_ack_cc_ci(data->dev, ci, cc, 0);
			if (rv) {
				LOG_ERR("ITE%d: Put ACK_CC_CI to buffer fail",
					cfg->connector_number);
				set_state(data, ST_ERROR_RECOVERY);
				return SMF_EVENT_HANDLED;
			}
			set_state(data, ST_WRITE_ACK);
		}
		break;
	default:
		/* Receive an unknown [CCI] status */
		LOG_ERR("ITE%d: unknown [CCI] = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }",
			cfg->connector_number, data->cci_byte_cnt,
			IT52XX_BYTE0(data->cci_message.raw_value),
			IT52XX_BYTE1(data->cci_message.raw_value),
			IT52XX_BYTE2(data->cci_message.raw_value),
			IT52XX_BYTE3(data->cci_message.raw_value));
		/* An error occurred, try to recover */
		set_state(data, ST_ERROR_RECOVERY);
		return SMF_EVENT_HANDLED;
	}

	return SMF_EVENT_HANDLED;
}

static void st_read_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Get [CCI] state */
	assert(data->last_state == ST_GET_CCI);

	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/* Clear I2C Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Set the port the CCI Event occurred on */
}

static enum smf_state_result st_read_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	uint8_t offset;
	uint8_t len;
	union conn_status_change_bits_t ci = { 0 };
	const bool cc = true;
	int rv;

	/* Read [MSGIN] */
	rv = it52xx_i2c_read(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	/*
	 * Clear UCSI interrupt event after read [MSGIN],
	 * and this also clear it52xx CCI message memory.
	 */
	rv = it52xx_clr_alert_status(data->dev, IT52XX_INT_UCSI_STATUS);

	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	/* Get length of data returned */
	len = data->rd_buf[0];

	/* Skip over length byte */
	offset = 1;

	/*
	 * The data->user_buf is checked for NULL before a command is queued.
	 * The check here guards against an erroneous cci_message indicating
	 * data is available for a command that doesn't send data.
	 */
	if (!data->user_buf) {
		LOG_ERR("ITE%d: NULL response buffer for %s",
			cfg->connector_number, cmd_names[data->cmd]);
		data->cci_event.error = 1;
		data->cci_event.data_len = 0;
	} else {
		/* Copy the received data to the user's buffer */
		switch (data->cmd) {
		case CMD_GET_IC_STATUS: {
			/* Fill all fields before updating the cache or user
			 * buffer. */
			struct pdc_info_t response_info = { 0 };
			struct pdc_info_t *info = &response_info;

			it52xx_unpack_get_ic_status_response(data->rd_buf,
							     info);

			/* Project name string is supported on version >= 0.3.x
			 */
			memcpy(info->project_name,
			       &data->rd_buf
					[IT52XX_GET_IC_STATUS_PROG_NAME_STR_INDEX],
			       USB_PD_CHIP_INFO_PROJECT_NAME_LEN);
			info->project_name[USB_PD_CHIP_INFO_PROJECT_NAME_LEN] =
				'\0';

			/* Only print this log on init */
			if (data->init_local_state != INIT_PDC_COMPLETE) {
				LOG_INF("ITE%d: FW Version %u.%u.%u (%s)",
					cfg->connector_number,
					PDC_FWVER_GET_MAJOR(info->fw_version),
					PDC_FWVER_GET_MINOR(info->fw_version),
					PDC_FWVER_GET_PATCH(info->fw_version),
					info->project_name);
				LOG_INF("ITE%d: PD Rev %04x Version %04x",
					cfg->connector_number,
					info->pd_revision, info->pd_version);
			}

			/* Fill in the chip type (driver compat string) */
			strncpy(info->driver_name, STRINGIFY(DT_DRV_COMPAT),
				sizeof(info->driver_name));
			info->driver_name[sizeof(info->driver_name) - 1] = '\0';

			info->no_fw_update = cfg->no_fw_update;
			info->frs_supported = cfg->frs_supported;
			info->usb_comm_capable_as_device = false;

			/* Cached reads must not see a partially updated
			 * structure. */
			k_mutex_lock(&data->mtx, K_FOREVER);
			data->info = *info;
			memcpy(data->user_buf, info, sizeof(*info));
			data->info_valid = true;
			k_mutex_unlock(&data->mtx);

			break;
		}
		case CMD_GET_CONNECTOR_STATUS: {
			memcpy(data->user_buf, data->rd_buf + offset, len);
			union connector_status_t *status =
				(union connector_status_t *)(data->rd_buf +
							     offset);
			data->cached_conn_status = *status;
			break;
		}
		case CMD_GET_VBUS_VOLTAGE: {
			union connector_status_t *status =
				(union connector_status_t *)(data->rd_buf +
							     offset);
			*(uint16_t *)data->user_buf = status->voltage_reading *
						      status->voltage_scale *
						      VOLTAGE_SCALE_FACTOR;
			break;
		}
		case CMD_GET_ERROR_STATUS: {
			union error_status_t *es =
				(union error_status_t *)data->user_buf;

			/*
			 * ITE GET_ERROR_STATUS bits are the same as UCSI spec
			 * defined (vendor-defined field of ITE, so far not
			 * defined).
			 */
			es->raw_value =
				((data->rd_buf[2] << 8) | data->rd_buf[1]);
			break;
		}
		case CMD_GET_IDENTITY_DISCOVERY: {
			/* TODO */
			break;
		}
		case CMD_GET_IS_VCONN_SOURCING: {
			/* TODO */
			break;
		}
		case CMD_GET_DRP_MODE: {
			/* TODO */
			break;
		}
#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
		case CMD_GET_SBU_MUX_MODE: {
			enum pdc_sbu_mux_mode *mode =
				(enum pdc_sbu_mux_mode *)data->user_buf;
			uint8_t raw_mode = data->rd_buf[offset];

			switch (raw_mode) {
			case 0:
				*mode = PDC_SBU_MUX_MODE_NORMAL;
				break;
			case 1:
				*mode = PDC_SBU_MUX_MODE_FORCE_DBG;
				break;
			default:
				*mode = PDC_SBU_MUX_MODE_INVALID;
				LOG_ERR("ITE%d: Unknown raw SBU mux value: 0x%02x",
					cfg->connector_number, raw_mode);
				break;
			}
			break;
		}
#endif /* defined(CONFIG_USBC_PDC_DRIVEN_CCD) */
		default:
			/* No preprocessing needed for the user data */
			memcpy(data->user_buf, data->rd_buf + offset, len);
		}

		/*
		 * Set cci_event.data_len. This will be zero if no
		 * data is available.
		 */
		data->cci_event.data_len = len;
	}

	/* Clear the read buffer */
	memset(data->rd_buf, 0, sizeof(data->rd_buf));

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/*
	 * It needs to write ACK_CC_CI following
	 * [CONTROL] command to it52xx (except CMD_PPM_RESET).
	 */
	data->cmd = CMD_NONE;
	ci.connect_change = data->ack_ci;
	data->ack_ci = 0;
	rv = it52xx_ack_cc_ci(data->dev, ci, cc, 0);
	if (rv) {
		LOG_ERR("ITE%d: Put ACK_CC_CI to buffer fail",
			cfg->connector_number);
		set_state(data, ST_ERROR_RECOVERY);
		return SMF_EVENT_HANDLED;
	}
	set_state(data, ST_WRITE_ACK);

	return SMF_EVENT_HANDLED;
}

static void st_error_recovery_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
	data->error_recovery_counter++;
	data->error_recovery_delay_counter = 0;
}

static enum smf_state_result st_error_recovery_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Don't continue trying if we are suspending communication */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	if (data->error_recovery_counter >= N_MAX_ERROR_RECOVERY_COUNT) {
		set_state(data, ST_DISABLE);
		return SMF_EVENT_HANDLED;
	}

	/* Current recovery is just delaying and performing a PDC init */
	if (data->error_recovery_delay_counter < N_ERROR_RECOVERY_DELAY_COUNT) {
		data->error_recovery_delay_counter++;
		return SMF_EVENT_HANDLED;
	}

	/* Perform PDC Init */
	perform_pdc_init(data);
	return SMF_EVENT_HANDLED;
}

static void st_disable_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
	/* If entering from ST_INIT state */
	data->init_done = true;
	data->error_status.port_disabled = 1;
}

static enum smf_state_result st_disable_run(void *o)
{
	/* Stay here until reset */
	return SMF_EVENT_HANDLED;
}

static void st_suspended_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static enum smf_state_result st_suspended_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Stay here while suspended */
	if (check_comms_suspended()) {
		return SMF_EVENT_HANDLED;
	}

	/* Otherwise, return back to init state...
	 *
	 * Start the driver initialization routine to put everything
	 * back into a known state (This includes a driver + PDC reset)
	 */
	perform_pdc_init(data);
	return SMF_EVENT_HANDLED;
}

/* Populate commands state table */
static const struct smf_state states[] = {
	[ST_INIT] =
		SMF_CREATE_STATE(st_init_entry, st_init_run, NULL, NULL, NULL),
	[ST_IDLE] =
		SMF_CREATE_STATE(st_idle_entry, st_idle_run, NULL, NULL, NULL),
	[ST_WRITE] = SMF_CREATE_STATE(st_write_entry, st_write_run, NULL, NULL,
				      NULL),
	[ST_WRITE_ACK] = SMF_CREATE_STATE(st_write_ack_entry, st_write_ack_run,
					  NULL, NULL, NULL),
	[ST_TASK_WAIT] = SMF_CREATE_STATE(st_task_wait_entry, st_task_wait_run,
					  NULL, NULL, NULL),
	[ST_GET_CCI] = SMF_CREATE_STATE(st_get_cci_entry, st_get_cci_run, NULL,
					NULL, NULL),
	[ST_READ] =
		SMF_CREATE_STATE(st_read_entry, st_read_run, NULL, NULL, NULL),
	[ST_ERROR_RECOVERY] = SMF_CREATE_STATE(st_error_recovery_entry,
					       st_error_recovery_run, NULL,
					       NULL, NULL),
	[ST_DISABLE] = SMF_CREATE_STATE(st_disable_entry, st_disable_run, NULL,
					NULL, NULL),
	[ST_SUSPENDED] = SMF_CREATE_STATE(st_suspended_entry, st_suspended_run,
					  NULL, NULL, NULL),
};

/**
 * @brief Helper method for setting up a command call.
 * @param dev PDC device pointer
 * @param cmd Command to execute
 * @param buf Command payload to copy into write buffer
 * @param len Length of payload buffer
 * @param user_buf Pointer to buffer where response data will be written.
 * @return 0 on success
 * @return -EBUSY if command is already pending.
 * @return -ECONNREFUSED if chip communication is suspended and @p cmd is not
 *         @ref CMD_ACK_CC_CI.
 */
static int it52xx_post_command_with_callback(const struct device *dev,
					     enum cmd_t cmd, const uint8_t *buf,
					     uint8_t len, uint8_t *user_buf,
					     struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/*
	 * ACK_CC_CI completes an in-progress transaction, so allow it after
	 * communication suspension has been requested.
	 */
	if (check_comms_suspended() && cmd != CMD_ACK_CC_CI) {
		return -ECONNREFUSED;
	}

	k_mutex_lock(&data->mtx, K_FOREVER);

	if (data->cmd != CMD_NONE) {
		k_mutex_unlock(&data->mtx);
		return -EBUSY;
	}

	if (buf) {
		assert(len <= sizeof(data->wr_buf));
		memcpy(data->wr_buf, buf, len);
	}

	data->wr_buf_len = len;
	data->user_buf = user_buf;
	data->cmd = cmd;
	data->cc_cb_tmp = callback;

	/* If sending a raw UCSI command, byte[2] is the actual UCSI command
	 * being executed.
	 */
	if (cmd == CMD_RAW_UCSI && buf) {
		data->active_ucsi_cmd = data->wr_buf[2];
	}

	if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
		const struct pdc_config_t *cfg = dev->config;

		pdc_trace_msg_req(cfg->connector_number,
				  PDC_TRACE_CHIP_TYPE_IT52XX, data->wr_buf,
				  data->wr_buf_len);
	}

	k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);

	return 0;
}

static int it52xx_post_command(const struct device *dev, enum cmd_t cmd,
			       const uint8_t *buf, uint8_t len,
			       uint8_t *user_buf)
{
	return it52xx_post_command_with_callback(dev, cmd, buf, len, user_buf,
						 NULL);
}

static int it52xx_get_alert_status(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	struct i2c_msg msg[2];
	uint8_t cmd = GET_ALERT_STATUS.cmd;
	uint8_t len = GET_ALERT_STATUS.len;

	msg[0].buf = &cmd;
	msg[0].len = len;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->rd_buf;
	msg[1].len = IT52XX_INT_READ_STATUS_LENGTH;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, msg, 2);
}

static int it52xx_clr_alert_status(const struct device *dev, uint8_t status)
{
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg;

	uint8_t payload[] = {
		CLR_ALERT_STATUS.cmd,
		status,
	};

	msg.buf = payload;
	msg.len = ARRAY_SIZE(payload);
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
}

static int it52xx_pdc_reset(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) == ST_DISABLE) {
		perform_pdc_init(data);
		return 0;
	}

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * Not UCSI command, it5271 doesn't support, this only sets unused data,
	 * doesn't go to write state. Later, ppm may go to unattached state.
	 * (If needs to support reboot it52xx, then PPM must init it52xx again)
	 */
	return it52xx_post_command(dev, CMD_TRIGGER_PDC_RESET, NULL, 0, NULL);
}

static int it52xx_ppm_reset(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_PPM_RESET.cmd,
		.len = SET_PPM_RESET.len,
		.control = {
			.command = SET_PPM_RESET.sub,
			.data_length = 0x00,
		},
	};

	return it52xx_post_command(dev, CMD_PPM_RESET,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_set_notification_enable(const struct device *dev,
					  union notification_enable_t bits)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_NOTIFICATION_ENABLE.cmd,
		.len = SET_NOTIFICATION_ENABLE.len,
		.control = {
			.command = SET_NOTIFICATION_ENABLE.sub,
			.data_length = 0x00,
			.command_specific = {
				IT52XX_BYTE0(bits.raw_value),
				IT52XX_BYTE1(bits.raw_value),
				IT52XX_BYTE2(bits.raw_value),
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_NOTIFICATION_ENABLE,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_ack_cc_ci(const struct device *dev,
			    union conn_status_change_bits_t ci, bool cc,
			    uint16_t vendor_defined)
{
	struct pdc_data_t *data = dev->data;

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT) &&
	    (get_state(data) != ST_GET_CCI) && (get_state(data) != ST_READ)) {
		return -EBUSY;
	}

	union ack_cc_ci_t request = {
		.connector_change_ack = ci.connect_change,
		.command_complete_ack = cc,
	};
	struct it52xx_control_cmd payload = {
		.ctrl_cmd = ACK_CC_CI.cmd,
		.len = ACK_CC_CI.len,
		.control = {
			.command = ACK_CC_CI.sub,
			.data_length = 0x00,
			.command_specific = {
				request.raw_value[0],
			},
		},
	};

	return it52xx_post_command(dev, CMD_ACK_CC_CI,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_get_info(const struct device *dev, struct pdc_info_t *info,
			   bool live)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte6 = 0;

	if (info == NULL) {
		return -EINVAL;
	}

	/* If caller is OK with a non-live value and we have one, we can
	 * immediately return a cached value.
	 */
	if (!live) {
		k_mutex_lock(&data->mtx, K_FOREVER);

		/* A valid FW version and VID/PID do not mean all fields are
		 * ready. Also require the complete-cache flag.
		 */
		if (!data->info_valid ||
		    data->info.fw_version == PDC_FWVER_INVALID ||
		    data->info.vid == PDC_VID_INVALID ||
		    data->info.pid == PDC_PID_INVALID) {
			k_mutex_unlock(&data->mtx);

			/* No complete cache. Try again later or request a live
			 * read. */
			return -EAGAIN;
		}

		*info = data->info;
		k_mutex_unlock(&data->mtx);

		LOG_DBG("ITE%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(info->fw_version),
			PDC_FWVER_GET_MINOR(info->fw_version),
			PDC_FWVER_GET_PATCH(info->fw_version));
		return 0;
	}

	/* Handle a live read */

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, IT52XX_VDM_VDC_GET_PDC_INFO 1'b */
	/* bit[24:31]: IT52XX_VDM_VDC_GET_PDC_INFO 4'b, IT52XX_VDM_VDC_VER 4'b
	 */
	byte5 = (conn_num & 0x7f) |
		((IT52XX_VDM_VDC_GET_PDC_INFO & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_GET_PDC_INFO >> 1) & 0xf) |
		((IT52XX_VDM_VDC_VER & 0xf) << 4);

	/* Post a command and perform a chip operation */
	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_IC_STATUS.cmd,
		.len = GET_IC_STATUS.len,
		.control = {
			.command = GET_IC_STATUS.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_VDM_VID_L,
				IT52XX_VDM_VID_H,
				0x00,
				/* EC_PID_L --> it52xx don't care */
				0x00,
				/* EC_PID_H --> it52xx don't care */
			},
		},
	};

	LOG_DBG("ITE%d: Get live chip info", cfg->connector_number);

	return it52xx_post_command(dev, CMD_GET_IC_STATUS,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)info);
}

static int it52xx_get_error_status(const struct device *dev,
				   union error_status_t *es)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	if (es == NULL) {
		return -EINVAL;
	}

	/* Port is disabled. Return the last read error_status. */
	if (get_state(data) == ST_DISABLE) {
		es->raw_value = data->error_status.raw_value;
		return 0;
	}

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_ERROR_STATUS.cmd,
		.len = GET_ERROR_STATUS.len,
		.control = {
			.command = GET_ERROR_STATUS.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_ERROR_STATUS,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)es);
}

static int it52xx_set_pdo(const struct device *dev, enum pdo_type_t type,
			  uint32_t *pdo, int count)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;

	if (pdo == NULL) {
		return -EINVAL;
	}

	if (count < 1 || count > IT52XX_SET_PDO_MAX_PDO_COUNT) {
		/* Count == 0 is reserved */
		return -ERANGE;
	}

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	/* bit[24:31]: reserved 2'b, SRC or SNK PDO 1'b, num of pdo 4'b, data
	 * index 1'b */
	/* bit[32:39]: data index 6'b, end of message 1'b, reserved 1'b */
	union ucsi_set_pdos_t request = {
		.connector_number = conn_num,
		.pdo_type = type,
		.number_of_pdos = count,
		.end_of_message = IT52XX_END_OF_MESSAGE,
	};

	/* CONTROL followed by the MSGOUT header and PDO data. */
	struct {
		struct it52xx_control_cmd ctrl;
		uint8_t msgout_cmd;
		uint8_t msgout_len;
		uint8_t pdos[sizeof(uint32_t) * IT52XX_SET_PDO_MAX_PDO_COUNT];
	} __packed payload = {
		.ctrl = {
			.ctrl_cmd = SET_PDO.cmd,
			.len = SET_PDO.len,
			.control = {
				.command = SET_PDO.sub,
				.data_length = sizeof(uint32_t) * count,
			},
		},
		.msgout_cmd = SET_MSGOUT.cmd,
		.msgout_len = sizeof(uint32_t) * count,
	};

	memcpy(payload.ctrl.control.command_specific, request.raw_value,
	       sizeof(request.raw_value));

	/* Compute actual length given number of PDOs being set */
	uint8_t payload_len =
		IT52XX_SET_PDO_CMD_BASE_LENGTH + sizeof(uint32_t) * count;

	/* Copy PDOs into payload buffer */
	memcpy(payload.pdos, pdo, sizeof(uint32_t) * count);

	return it52xx_post_command(dev, CMD_SET_PDO, (const uint8_t *)&payload,
				   payload_len, NULL);
}

static int it52xx_get_connector_status(const struct device *dev,
				       union connector_status_t *cs)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cs == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_CONNECTOR_STATUS.cmd,
		.len = GET_CONNECTOR_STATUS.len,
		.control = {
			.command = GET_CONNECTOR_STATUS.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_CONNECTOR_STATUS,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)cs);
}

static int it52xx_set_sink_path(const struct device *dev, bool en)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, sink path enable/disable 1'b */
	union set_sink_path_t request = {
		.connector_number = conn_num,
		.sink_path_enable = en,
	};

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_SINK_PATH.cmd,
		.len = SET_SINK_PATH.len,
		.control = {
			.command = SET_SINK_PATH.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				request.raw_value,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_SINK_PATH,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

/*
 * This api isn't compliant with the UCSI spec, ppm/mgmt only set Rp value
 * through this command.
 */
static int it52xx_set_power_level(const struct device *dev,
				  enum usb_typec_current_t tcc)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte7 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, source or sink 1'b */
	byte5 = ((conn_num & 0x7f) | (IT52XX_SET_SRC_PWR_LVL << 7));
	switch (tcc) {
	case TC_CURRENT_3_0A:
	case TC_CURRENT_1_5A:
	case TC_CURRENT_USB_DEFAULT:
		/* bit[32:34]: usb type-c current 3'b */
		byte7 = tcc;
		break;
	case TC_CURRENT_PPM_DEFINED:
	default:
		return -EINVAL;
	}

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_TPC_RP.cmd,
		.len = SET_TPC_RP.len,
		.control = {
			.command = SET_TPC_RP.sub,
			.data_length = 0x00,
			/* data
									 length
									 --> set
									 to 0x00
								       */
			.command_specific = {
				byte5,
				0x00,
				byte7,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_TPC_RP,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_read_power_level(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte6, byte7 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * UCSI v3.0 spec: READ_POWER_LEVEL command is used by the OPM to read
	 * a port’s peak power and average power levels from the LPM that is in
	 * the sourcing mode (SRC role). So if we're not SRC, return not
	 * support.
	 */
	if (!data->cached_conn_status.power_direction) {
		return -ENOSYS;
	}

	/* bit[16:23]: connector num 7'b, time to read power 1'b */
	/* bit[24:31]: time to read power 4'b, reserved 3'b, time interval 1'b
	 */
	/* bit[32]: time interval 1'b */
	byte5 = (conn_num & 0x7f);
	/*
	 * TODO: Input parameter of it52xx_read_power_level() doesn't have these
	 * values, so temporarily give a fix value for 100ms to read power &
	 * 10ms time interval.
	 *
	 *       b/435301650, it52xx only support voltage measurement (not
	 * current), and so far it's okay for PPM.
	 */
	byte6 = 0x0;
	byte7 = 0x1;

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = READ_POWER_LEVEL.cmd,
		.len = READ_POWER_LEVEL.len,
		.control = {
			.command = READ_POWER_LEVEL.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				byte7,
			},
		},
	};

	return it52xx_post_command(dev, CMD_READ_POWER_LEVEL,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_set_ccom(const struct device *dev, enum ccom_t ccom)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint16_t conn_opmode = 0;
	const uint8_t opmode_offset = 7;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * bit[16:23]: connector num 7'b, CC operation mode 1'b.
	 * bit[24:31]: CC operation mode 3'b, reserved 5'b.
	 */
	switch (ccom) {
	case CCOM_RP:
		conn_opmode = 1 << (opmode_offset + 0);
		break;
	case CCOM_RD:
		conn_opmode = 1 << (opmode_offset + 1);
		break;
	case CCOM_DRP: /* it52xx default is TrySRC DRP, and can't be selected at
			  run-time. */
		conn_opmode = 1 << (opmode_offset + 2);
		break;
	}

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_CCOM.cmd,
		.len = SET_CCOM.len,
		.control = {
			.command = SET_CCOM.sub,
			.data_length = 0x00,
			/* data length --> set to 0x00 */
			.command_specific = {
				((conn_num & 0x7f) | (conn_opmode & 0xff)),
				(conn_opmode >> 8) & 0xff,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_CCOM, (const uint8_t *)&payload,
				   sizeof(payload), NULL);
}

static int it52xx_get_connector_capability(const struct device *dev,
					   union connector_capability_t *caps)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (caps == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_CONNECTOR_CAPABILITY.cmd,
		.len = GET_CONNECTOR_CAPABILITY.len,
		.control = {
			.command = GET_CONNECTOR_CAPABILITY.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_CONNECTOR_CAPABILITY,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)caps);
}

static int it52xx_set_uor(const struct device *dev, union uor_t uor)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, DRS to DFP 1'b */
	/* bit[24:31]: DRS to UFP 1'b, Accept/Reject DRS 1'b, reserved 6'b */
	uor.connector_number = conn_num;
	/*
	 * Set accept DR_Swap, it527x only accepts DR_Swap when UFP -> DFP
	 * (match VIF declare), also fix CTS TEST.PD.PROT.SNK.10: UUT send
	 * reject DR_Swap UFP -> DFP.
	 */
	uor.accept_dr_swap = 1;

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_UOR.cmd,
		.len = SET_UOR.len,
		.control = {
			.command = SET_UOR.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				uor.raw_value & 0xff,
				(uor.raw_value >> 8) & 0xff,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_UOR, (const uint8_t *)&payload,
				   sizeof(payload), NULL);
}

static int it52xx_set_pdr(const struct device *dev, union pdr_t pdr)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, PRS to SRC 1'b */
	/* bit[24:31]: PRS to SNK 1'b, Enable/Disable PRS 1'b, reserved 6'b */
	pdr.connector_number = conn_num;

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_PDR.cmd,
		.len = SET_PDR.len,
		.control = {
			.command = SET_PDR.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				pdr.raw_value & 0xff,
				(pdr.raw_value >> 8) & 0xff,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_PDR, (const uint8_t *)&payload,
				   sizeof(payload), NULL);
}

static int it52xx_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			  uint8_t *vdo_req_list, uint32_t *vdo)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint16_t vdo_type = 0;
	uint8_t vdc_cmd, byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL || vdo_req_list == NULL) {
		return -EINVAL;
	}

	switch (vdo_req.vdo_origin) {
	case VDO_ORIGIN_PORT:
		vdc_cmd = IT52XX_VDM_VDC_GET_VDO_PDC;
		break;
	case VDO_ORIGIN_SOP:
		vdc_cmd = IT52XX_VDM_VDC_GET_VDO_PARTNER;
		break;
	case VDO_ORIGIN_SOP_PRIME:
	case VDO_ORIGIN_SOP_PRIME_PRIME:
		vdc_cmd = IT52XX_VDM_VDC_GET_VDO_CABLE;
		break;
	default:
		LOG_ERR("Unsupported VDO origin");
		return -EINVAL;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (conn_num & 0x7f) | ((vdc_cmd & BIT(0)) << 7);
	byte6 = ((vdc_cmd >> 1) & 0xf) | ((IT52XX_VDM_VDC_VER & 0xf) << 4);

	/*
	 * IT52XX uses a bitmap and returns VDOs in increasing type order.
	 * Require the request list to use the same order.
	 */
	for (int i = 0; i < vdo_req.num_vdos; i++) {
		if (vdo_req_list[i] < VDO_ID_HEADER ||
		    vdo_req_list[i] > VDO_PD_DP_CFG) {
			return -EINVAL;
		}

		if (i > 0 && vdo_req_list[i] <= vdo_req_list[i - 1]) {
			return -EINVAL;
		}

		vdo_type |= BIT(vdo_req_list[i]);
		LOG_DBG("vdo_req_list %d", vdo_req_list[i]);
	}

	/* Post a command and perform a chip operation */
	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_VDO.cmd,
		.len = GET_VDO.len,
		.control = {
			.command = GET_VDO.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_VDM_VID_L,
				IT52XX_VDM_VID_H,
				vdo_type & 0xff,
				/* bit map to enum vdo_type_t */
				(vdo_type >> 8) & 0xff,
				/* bit map to enum vdo_type_t */
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_VDO, (const uint8_t *)&payload,
				   sizeof(payload), (uint8_t *)vdo);
}

static int it52xx_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			   enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			   enum pdo_source_t source, uint32_t *pdos)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	union get_pdos_t *get_pdo;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdos == NULL) {
		return -EINVAL;
	}

	/* The vendor specific GET_PDO command fails to generate
	 * the appropriate PD message if the requested PDO type has not
	 * been received.
	 *
	 * Use the UCSI version which has the correct behavior.
	 */
	memset((uint8_t *)pdos, 0, sizeof(uint32_t) * num_pdos);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_PDOS.cmd,
		.len = GET_PDOS.len,
		.control = {
			.command = GET_PDOS.sub,
			.data_length = 0x00,
			/* Data Length
								   --> set to
								   0x00 */
		},
	};

	BUILD_ASSERT(sizeof(payload) == IT52XX_CONTROL_CMD_BASE_LENGTH);

	get_pdo = (union get_pdos_t *)payload.control.command_specific;
	get_pdo->connector_number = conn_num;
	get_pdo->pdo_source = source;
	get_pdo->pdo_offset = pdo_offset;
	get_pdo->number_of_pdos = num_pdos - 1;
	get_pdo->pdo_type = pdo_type;
	get_pdo->source_caps = CURRENT_SUPPORTED_SOURCE_CAPS;
	get_pdo->range = SPR_RANGE;

	return it52xx_post_command(dev, CMD_GET_PDOS, (const uint8_t *)&payload,
				   sizeof(payload), (uint8_t *)pdos);
}

static int it52xx_set_rdo(const struct device *dev, uint32_t rdo)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (conn_num & 0x7f) | ((IT52XX_VDM_VDC_SET_RDO & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_SET_RDO >> 1) & 0xf) |
		((IT52XX_VDM_VDC_VER & 0xf) << 4);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_RDO.cmd,
		.len = SET_RDO.len,
		.control = {
			.command = SET_RDO.sub,
			.data_length = 0x00,
			/* Data Length -->
			 set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_BYTE0(rdo),
				IT52XX_BYTE1(rdo),
				IT52XX_BYTE2(rdo),
				IT52XX_BYTE3(rdo),
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_RDO, (const uint8_t *)&payload,
				   sizeof(payload), NULL);
}

static int it52xx_get_cable_property(const struct device *dev,
				     union cable_property_t *cp)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cp == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_CABLE_PROPERTY.cmd,
		.len = GET_CABLE_PROPERTY.len,
		.control = {
			.command = GET_CABLE_PROPERTY.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_CABLE_PROPERTY,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)cp);
}

static int it52xx_get_rdo(const struct device *dev, uint32_t *rdo)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (rdo == NULL) {
		return -EINVAL;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (conn_num & 0x7f) | ((IT52XX_VDM_VDC_GET_RDO & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_GET_RDO >> 1) & 0xf) |
		((IT52XX_VDM_VDC_VER & 0xf) << 4);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_RDO.cmd,
		.len = GET_RDO.len,
		.control = {
			.command = GET_RDO.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_VDM_VID_L,
				IT52XX_VDM_VID_H,
				0x00,
				/* EC_PID_L --> it52xx don't care */
				0x00,
				/* EC_PID_H --> it52xx don't care */
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_RDO, (const uint8_t *)&payload,
				   sizeof(payload), (uint8_t *)rdo);
}

static int it52xx_connector_reset(const struct device *dev,
				  union connector_reset_t reset)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	reset.connector_number = conn_num;

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_CONNECTOR_RESET.cmd,
		.len = SET_CONNECTOR_RESET.len,
		.control = {
			.command = SET_CONNECTOR_RESET.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				reset.raw_value,
			},
		},
	};

	return it52xx_post_command(dev, CMD_CONNECTOR_RESET,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}

static int it52xx_get_capability(const struct device *dev,
				 struct capability_t *caps)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (caps == NULL) {
		return -EINVAL;
	}

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_CAPABILITY.cmd,
		.len = GET_CAPABILITY.len,
		.control = {
			.command = GET_CAPABILITY.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
		},
	};

	return it52xx_post_command(dev, CMD_GET_CAPABILITY,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)caps);
}

static int it52xx_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (voltage == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_CONNECTOR_STATUS.cmd,
		.len = GET_CONNECTOR_STATUS.len,
		.control = {
			.command = GET_CONNECTOR_STATUS.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_VBUS_VOLTAGE,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)voltage);
}

static int it52xx_get_lpm_ppm_info(const struct device *dev,
				   struct lpm_ppm_info_t *info)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (info == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_LPM_PPM_INFO.cmd,
		.len = GET_LPM_PPM_INFO.len,
		.control = {
			.command = GET_LPM_PPM_INFO.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_LPM_PPM_INFO,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)info);
}

static int it52xx_get_attention_vdo(const struct device *dev,
				    union get_attention_vdo_t *vdo)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (conn_num & 0x7f);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_ATTENTION_VDO.cmd,
		.len = GET_ATTENTION_VDO.len,
		.control = {
			.command = GET_ATTENTION_VDO.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_ATTENTION_VDO,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)vdo);
}

/* PPM directly call standard UCSI cmd to LPM (not through mgmt) */
static int it52xx_execute_ucsi_cmd(const struct device *dev,
				   uint8_t ucsi_command, uint8_t data_size,
				   uint8_t *command_specific,
				   uint8_t *lpm_data_out,
				   struct pdc_callback *callback)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t cmd_buffer[IT52XX_SMBUS_MAX_BLOCK_SIZE];
	uint8_t conn_num = cfg->port_index_on_chip;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	cmd_buffer[0] = SET_CONTROL.cmd;
	cmd_buffer[1] = data_size + 2;
	cmd_buffer[2] = ucsi_command; /* sub-cmd */
	cmd_buffer[3] = 0; /* data length of sub-cmd */
	memcpy(&cmd_buffer[4], command_specific, data_size);

	/* Convert port number of standard UCSI sub-command to specific value */
	switch (ucsi_command) {
	case UCSI_CONNECTOR_RESET:
	case UCSI_GET_CONNECTOR_CAPABILITY:
	case UCSI_GET_CAM_SUPPORTED:
	case UCSI_GET_CURRENT_CAM:
	case UCSI_GET_PDOS:
	case UCSI_GET_CABLE_PROPERTY:
	case UCSI_GET_CONNECTOR_STATUS:
	case UCSI_GET_ERROR_STATUS:
	case UCSI_GET_PD_MESSAGE:
	case UCSI_GET_ATTENTION_VDO:
	case UCSI_GET_LPM_PPM_INFO:
	case UCSI_GET_CAM_CS:
	case UCSI_SET_CCOM:
	case UCSI_SET_UOR:
	case UCSI_SET_PDR:
	case UCSI_SET_POWER_LEVEL:
	case UCSI_SET_RETIMER_MODE:
	case UCSI_SET_SINK_PATH:
	case UCSI_SET_PDOS:
	case UCSI_SET_NEW_CAM:
	case UCSI_SET_USB:
		cmd_buffer[4] &= ~0x7f;
		cmd_buffer[4] |= (conn_num & 0x7f);
		break;
	case UCSI_GET_ALTERNATE_MODES:
		cmd_buffer[5] &= ~0x7f;
		cmd_buffer[5] |= (conn_num & 0x7f);
		break;
	default:
		LOG_INF("ITE%d: needn't assign port number ucsi sub-command",
			cfg->connector_number);
	}

	return it52xx_post_command_with_callback(dev, CMD_RAW_UCSI, cmd_buffer,
						 data_size + 4, lpm_data_out,
						 callback);
}

#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
static int it52xx_get_sbu_mux_mode(const struct device *dev,
				   enum pdc_sbu_mux_mode *mode)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t byte5, byte6 = 0;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (mode == NULL) {
		return -EINVAL;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (conn_num & 0x7f) |
		((IT52XX_VDM_VDC_GET_SBU_MUX_MODE & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_GET_SBU_MUX_MODE >> 1) & 0xf) |
		((IT52XX_VDM_VDC_VER & 0xf) << 4);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = GET_SBU_MUX_MODE.cmd,
		.len = GET_SBU_MUX_MODE.len,
		.control = {
			.command = GET_SBU_MUX_MODE.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_VDM_VID_L,
				IT52XX_VDM_VID_H,
				0x00,
				/* EC_PID_L --> it52xx don't care */
				0x00,
				/* EC_PID_H --> it52xx don't care */
			},
		},
	};

	return it52xx_post_command(dev, CMD_GET_SBU_MUX_MODE,
				   (const uint8_t *)&payload, sizeof(payload),
				   (uint8_t *)mode);
}

static int it52xx_set_sbu_mux_mode(const struct device *dev,
				   enum pdc_sbu_mux_mode mode)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t conn_num = cfg->port_index_on_chip;
	uint8_t setting, byte5, byte6 = 0;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	switch (mode) {
	case PDC_SBU_MUX_MODE_NORMAL:
		setting = 0;
		break;
	case PDC_SBU_MUX_MODE_FORCE_DBG:
		setting = 1;
		break;
	default:
		return -ERANGE;
	}

	/* Support by [CONTROL] VDM */
	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (conn_num & 0x7f) |
		((IT52XX_VDM_VDC_SET_SBU_MUX_MODE & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_SET_SBU_MUX_MODE >> 1) & 0xf) |
		((IT52XX_VDM_VDC_VER & 0xf) << 4);

	struct it52xx_control_cmd payload = {
		.ctrl_cmd = SET_SBU_MUX_MODE.cmd,
		.len = SET_SBU_MUX_MODE.len,
		.control = {
			.command = SET_SBU_MUX_MODE.sub,
			.data_length = 0x00,
			/* Data Length --> set to 0x00 */
			.command_specific = {
				byte5,
				byte6,
				IT52XX_VDM_VID_L,
				IT52XX_VDM_VID_H,
				setting,
			},
		},
	};

	return it52xx_post_command(dev, CMD_SET_SBU_MUX_MODE,
				   (const uint8_t *)&payload, sizeof(payload),
				   NULL);
}
#endif /* defined(CONFIG_USBC_PDC_DRIVEN_CCD) */

static int it52xx_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	if (version == NULL) {
		return -EINVAL;
	}

	*version = UCSI_VERSION;

	return 0;
}

static int it52xx_get_hw_config(const struct device *dev,
				struct pdc_hw_config_t *config)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;

	if (config == NULL) {
		return -EINVAL;
	}

	config->bus_type = PDC_BUS_TYPE_I2C;
	config->i2c = cfg->i2c;
	config->ccd = cfg->ccd;

	return 0;
}

static bool it52xx_get_hw_frs_support(const struct device *dev)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;

	return cfg->frs_supported;
}

static int it52xx_set_comms_state(const struct device *dev, bool comms_active)
{
	struct pdc_data_t *data = dev->data;

	if (comms_active) {
		/* Re-enable communications. Clearing the suspend flag will
		 * trigger a reset. Note: if the driver is in the disabled
		 * state due to a previous comms failure, it will remain
		 * disabled. (Thus, suspending/resuming comms on a disabled
		 * PDC driver is a no-op)
		 */
		enable_comms();

	} else {
		/* Request communication to be stopped. This allows in-progress
		 * operations to complete first.
		 */
		suspend_comms();

		if (get_state(data) == ST_DISABLE) {
			/* The driver is already permanently shut down. */
			return 0;
		}

		/* Wait for driver to enter the suspended state */
		if (!WAIT_FOR((get_state(data) == ST_SUSPENDED),
			      SUSPEND_TIMEOUT_USEC,
			      k_sleep(K_MSEC(T_TASK_TIMEOUT_EVENT)))) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int it52xx_set_handler_cb(const struct device *dev,
				 struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/* Register the PDC management callback pdc_cc_handler_cb(). */
	data->cc_cb = callback;

	return 0;
}

static int it52xx_manage_callback(const struct device *dev,
				  struct pdc_callback *callback, bool set)
{
	struct pdc_data_t *const data = dev->data;

	/* Register or unregister the PDC management callback
	 * pdc_ci_handler_cb(). */
	return pdc_manage_callbacks(&data->ci_cb_list, callback, set);
}

static void it52xx_start_thread(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	k_thread_start(data->thread);
}

static enum pdc_driver_init_state_t
it52xx_is_init_done(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	return data->init_done ? PDC_DRIVER_INIT_STATE_SUCCESS :
				 PDC_DRIVER_INIT_STATE_WAIT;
}

static DEVICE_API(pdc, pdc_driver_api) = {
	.start_thread = it52xx_start_thread,
	.is_init_done = it52xx_is_init_done,
	.get_ucsi_version = it52xx_get_ucsi_version,
	.reset = it52xx_pdc_reset,
	.connector_reset = it52xx_connector_reset,
	.get_capability = it52xx_get_capability,
	.get_connector_capability = it52xx_get_connector_capability,
	.set_ccom = it52xx_set_ccom,
	.set_drp_mode = NULL, /* it52xx is trysrc DRP by build config */
	.get_drp_mode = NULL,
	.set_uor = it52xx_set_uor,
	.set_pdr = it52xx_set_pdr,
	.set_sink_path = it52xx_set_sink_path,
	.get_connector_status = it52xx_get_connector_status,
	.get_pdos = it52xx_get_pdos,
	.get_rdo = it52xx_get_rdo,
	.set_rdo = it52xx_set_rdo,
	.get_error_status = it52xx_get_error_status,
	.get_vbus_voltage = it52xx_get_vbus_voltage,
	.get_current_pdo = NULL, /* Duplicated function with .get_pdos */
	.set_handler_cb = it52xx_set_handler_cb,
	.read_power_level = it52xx_read_power_level,
	.get_info = it52xx_get_info,
	.get_hw_config = it52xx_get_hw_config,
	.get_hw_frs_support = it52xx_get_hw_frs_support,
	.set_power_level = it52xx_set_power_level,
	.reconnect = NULL,
	.update_retimer = NULL,
	.get_cable_property = it52xx_get_cable_property,
	.get_vdo = it52xx_get_vdo,
	.get_identity_discovery = NULL,
	.set_comms_state = it52xx_set_comms_state,
	.is_vconn_sourcing = NULL,
	.set_pdos = it52xx_set_pdo,
	.get_pch_data_status = NULL,
	.execute_ucsi_cmd = it52xx_execute_ucsi_cmd,
	.manage_callback = it52xx_manage_callback,
	.ack_cc_ci = NULL, /* The IT52XX driver sends ACK_CC_CI internally. */
	.get_lpm_ppm_info = it52xx_get_lpm_ppm_info,
	.set_frs = NULL,
	.get_attention_vdo = it52xx_get_attention_vdo,
	.set_battery_capability = NULL,
	.set_battery_status = NULL,
#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
	.get_sbu_mux_mode = it52xx_get_sbu_mux_mode,
	.set_sbu_mux_mode = it52xx_set_sbu_mux_mode,
#endif /* define(CONFIG_USBC_PDC_DRIVEN_CCD) */
	.set_bbr_cts = NULL,
	.set_ap_power_state = NULL,
	.get_vendor_status = NULL,
	.get_alert = NULL,
	.set_max_pdp = NULL,
};

static int pdc_init(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	int rv;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("device %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->irq_gpios)) {
		LOG_ERR("device %s not ready", cfg->irq_gpios.port->name);
		return -ENODEV;
	}

	k_event_init(&data->driver_event);

	rv = gpio_pin_configure_dt(&cfg->irq_gpios, GPIO_INPUT);
	if (rv < 0) {
		LOG_ERR("Unable to configure GPIO");
		return rv;
	}

	gpio_init_callback(&data->gpio_cb, cfg->callback_handler,
			   BIT(cfg->irq_gpios.pin));

	rv = gpio_add_callback(cfg->irq_gpios.port, &data->gpio_cb);
	if (rv < 0) {
		LOG_ERR("Unable to add callback");
		return rv;
	}

	rv = gpio_pin_interrupt_configure_dt(&cfg->irq_gpios,
					     GPIO_INT_EDGE_FALLING);
	if (rv < 0) {
		LOG_ERR("Unable to configure interrupt");
		return rv;
	}

	/* Trigger IRQ on startup to read any pending interrupts */
	k_event_post(&data->driver_event, IT52XX_IRQ_EVENT);

	k_mutex_init(&data->mtx);

	data->cmd = CMD_NONE;
	data->error_recovery_counter = 0;
	data->init_retry_counter = 0;
	data->info.fw_version = PDC_FWVER_INVALID;
	data->info_valid = false;

	/* Set initial state */
	data->init_local_state = INIT_PDC_SET_PPM_RESET;
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	LOG_INF("ITE%d: ITE it52xx PDC DRIVER", cfg->connector_number);

	return 0;
}

static void it52xx_thread(void *dev, void *unused1, void *unused2)
{
	const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
	struct pdc_data_t *data = ((const struct device *)dev)->data;
	enum state_t state;
	uint32_t events;
	bool irq_pending = false;

	while (1) {
		smf_run_state(SMF_CTX(data));

		events =
			k_event_wait(&data->driver_event,
				     IT52XX_IRQ_EVENT | IT52XX_NEXT_STATE_READY,
				     false, K_MSEC(T_TASK_TIMEOUT_EVENT));

		if (events & IT52XX_IRQ_EVENT) {
			irq_pending = true;
		}

		k_event_clear(&data->driver_event, events);

		state = get_state(data);

		/*
		 * Handle command-completion (CC) IRQs while a command
		 * transaction is in progress. This includes the response to
		 * ACK_CC_CI, allowing the transaction to complete before
		 * communication is suspended.
		 */
		if (state == ST_TASK_WAIT && irq_pending) {
			irq_pending = false;
			handle_irqs(data);
		}

		/*
		 * Handle asynchronous connector-change indicator (CI) IRQs only
		 * while communication is active. Do not start a new CI
		 * transaction after communication suspension has been
		 * requested.
		 */
		if (state == ST_IDLE && irq_pending) {
			irq_pending = false;
			if (check_comms_suspended()) {
				LOG_INF("ITE%d: Ignoring CI interrupt",
					cfg->connector_number);
				continue;
			}
			handle_irqs(data);
		}
	}
}

#define PDC_DATA_STRUCT_NAME(inst) pdc_data_##inst

BUILD_ASSERT(
	EC_TASK_PRIORITY(EC_TASK_USBC_PDC_PRIO) ==
		CONFIG_USBC_PDC_IT52XX_THREAD_PRIORITY,
	"EC_TASK_USBC_PDC_PRIO does not match USBC_PDC_IT52XX_THREAD_PRIORITY.");

#define IT52XX_PDC_DEFINE(inst)                                               \
	K_THREAD_STACK_DEFINE(it52xx_thread_stack_area_##inst,                \
			      CONFIG_USBC_PDC_IT52XX_STACK_SIZE);             \
                                                                              \
	static void create_thread_##inst(const struct device *dev)            \
	{                                                                     \
		struct pdc_data_t *data = dev->data;                          \
                                                                              \
		data->thread = k_thread_create(                               \
			&data->thread_data, it52xx_thread_stack_area_##inst,  \
			K_THREAD_STACK_SIZEOF(                                \
				it52xx_thread_stack_area_##inst),             \
			it52xx_thread, (void *)dev, 0, 0,                     \
			CONFIG_USBC_PDC_IT52XX_THREAD_PRIORITY, K_ESSENTIAL,  \
			K_FOREVER);                                           \
		k_thread_name_set(data->thread, "IT52XX" STRINGIFY(inst));    \
	}                                                                     \
                                                                              \
	static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst);                  \
                                                                              \
	static void pdc_interrupt_callback##inst(const struct device *dev,    \
						 struct gpio_callback *cb,    \
						 uint32_t pins)               \
	{                                                                     \
		k_event_post(&PDC_DATA_STRUCT_NAME(inst).driver_event,        \
			     IT52XX_IRQ_EVENT);                               \
	}                                                                     \
                                                                              \
	static const struct pdc_config_t pdc_config##inst = {                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                            \
		.irq_gpios = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),          \
		.connector_number =                                           \
			USBC_PORT_FROM_PDC_DRIVER_NODE(DT_DRV_INST(inst)),    \
		.bits.command_completed = 1,                                  \
		.bits.external_supply_change = 1,                             \
		.bits.power_operation_mode_change = 1,                        \
		.bits.attention = 1,                                          \
		.bits.fw_update_request = 0,                                  \
		.bits.provider_capability_change_supported = 1,               \
		.bits.negotiated_power_level_change = 1,                      \
		.bits.pd_reset_complete = 1,                                  \
		.bits.support_cam_change = 1,                                 \
		.bits.battery_charging_status_change = 1,                     \
		.bits.security_request_from_port_partner = 0,                 \
		.bits.connector_partner_change = 1,                           \
		.bits.power_direction_change = 1,                             \
		.bits.set_retimer_mode = 0,                                   \
		.bits.connect_change = 1,                                     \
		.bits.error = 1,                                              \
		.bits.sink_path_status_change = 1,                            \
		.create_thread = create_thread_##inst,                        \
		.no_fw_update = DT_INST_PROP(inst, no_fw_update),             \
		.ccd = DT_INST_PROP(inst, ccd),                               \
		.frs_supported = DT_INST_PROP(inst, frs_supported),           \
		.callback_handler = pdc_interrupt_callback##inst,             \
		.port_index_on_chip = DT_INST_PROP(inst, port_index_on_chip), \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL,                           \
			      &PDC_DATA_STRUCT_NAME(inst), &pdc_config##inst, \
			      POST_KERNEL, CONFIG_PDC_DRIVER_INIT_PRIORITY,   \
			      &pdc_driver_api);                               \
                                                                              \
	static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst) = {               \
		.dev = DEVICE_DT_INST_GET(inst),                              \
	};

DT_INST_FOREACH_STATUS_OKAY(IT52XX_PDC_DEFINE)

#ifdef CONFIG_ZTEST
/* LCOV_EXCL_START */
/* For tests only: mark the chip info cache unavailable without changing it. */
void pdc_it52xx_test_invalidate_chip_info(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	k_mutex_lock(&data->mtx, K_FOREVER);
	data->info_valid = false;
	k_mutex_unlock(&data->mtx);
}
/* LCOV_EXCL_STOP */
#endif /* CONFIG_ZTEST */

#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
/* If PDC-driven CCD is used, one of the PDC driver nodes for each driver compat
 * type must be marked with the `ccd` property.
 */
CHECK_ONE_CCD_PORT_COUNT_FOR_DRIVER();
#endif /* CONFIG_USBC_PDC_DRIVEN_CCD */
