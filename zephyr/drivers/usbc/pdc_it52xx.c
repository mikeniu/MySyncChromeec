/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * ITE it52xx Power Delivery Controller Driver
 */
#include "drivers/ucsi_v3.h"
#include "pdc_it52xx.h"

#include <assert.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>
LOG_MODULE_REGISTER(pdc_it52xx, CONFIG_USBC_LOG_LEVEL);
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"

#include <drivers/pdc.h>

#define DT_DRV_COMPAT ite_it52xx_pdc

#define BYTE0(n) ((n) & 0xff)
#define BYTE1(n) (((n) >> 8) & 0xff)
#define BYTE2(n) (((n) >> 16) & 0xff)
#define BYTE3(n) (((n) >> 24) & 0xff)


/**
 * @brief Time from writing command to first get [CCI] message
 */
#define T_INITIAL_GET_CCI_MSG 5

/**
 * @brief Polling interval for subsequent get [CCI] message
 */
#define T_GET_CCI_MSG 20

/**
 * @brief Error Recovery Delay Counter (time delay is 60mS)
 */
#define N_ERROR_RECOVERY_DELAY_COUNT (60 / T_GET_CCI_MSG)

/**
 * @brief Max number of error recovery attempts
 */
#define N_MAX_ERROR_RECOVERY_COUNT 4

/**
 * @brief Number of times to try an I2C transaction
 */
#define N_I2C_TRANSACTION_COUNT 10

/**
 * @brief Number of times to send a get [CCI] message
 */
#define N_RETRY_COUNT 200

/**
 * @brief Number of times to try and initialize the driver
 */
#define N_INIT_RETRY_ATTEMPT_MAX 2

/**
 * @brief Connector Status VBUS Voltage Scale Factor is 5mV
 */
#define VOLTAGE_SCALE_FACTOR 5

/**
 * @brief Allow 3 seconds for the driver to suspend itself
 */
#define SUSPEND_TIMEOUT_USEC (3 * USEC_PER_SEC)

/**
 * @brief SMbus max block size
 */
#define SMBUS_MAX_BLOCK_SIZE 32

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
 * is running on, we always set the connector number to physical port number on board.
 * Also, it5271 always replies to connector number 1 to EC.
 * TODO: change it5271 FW UCSI_PORT_TABLE & using cfg->connector_number + 1 instead of IT5271_CONNECTOR_NUMBER
 */
#define IT5271_CONNECTOR_NUMBER 1
/*
 * Constants for VDM
 */
#define IT52XX_VDM_VDC_GET_PDC_INFO 0
#define IT52XX_VDM_VDC_GET_VDO_PDC 1
#define IT52XX_VDM_VDC_GET_VDO_PARTNER 2
#define IT52XX_VDM_VDC_GET_VDO_CABLE 3
#define IT52XX_VDM_VDC_SET_RDO 4
#define IT52XX_VDM_VDC_GET_RDO 5
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
/*
 * Constants for SET_POWER_LEVEL
 */
#define IT52XX_SET_SRC_PWR_LVL 1

/**
 * @brief Macro to transition to init or idle state and return
 */
#define TRANSITION_TO_INIT_OR_IDLE_STATE(data)  \
	transition_to_init_or_idle_state(data); \
	return SMF_EVENT_HANDLED

/**
 * @brief IRQ Event set by the interrupt handler.
 */
#define IT52XX_IRQ_EVENT BIT(0)

/**
 * @brief Event set to run next state of state machine.
 */
#define IT52XX_NEXT_STATE_READY BIT(1)

/**
 * @brief Number of IT52XX ports detected
 */
#define NUM_PDC_IT52XX_PORTS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/**
 * @brief SMbus Command struct for it52xx commands
 */
struct smbus_cmd_t {
	/* Command ex. [CONTROL], [MSGOUT], [CCI], [MSGIN] */
	uint8_t cmd;
	/* Byte count */
	uint8_t len;
	/* Sub-Command */
	uint8_t sub;
};

/** @brief ITE SMbus commands */
//define REALTEK_PD_COMMAND 0x0e

//static const struct smbus_cmd_t VENDOR_CMD_ENABLE = { 0x01, 0x03, 0xDA };
//static const struct smbus_cmd_t SET_PDO = { 0x08, 0x03, 0x03 };
//static const struct smbus_cmd_t SET_RDO = { 0x08, 0x06, 0x04 };
//static const struct smbus_cmd_t SET_TPC_RP = { 0x08, 0x03, 0x05 };
////static const struct smbus_cmd_t SET_TPC_CSD_OPERATION_MODE = { 0x08, 0x03,
////							       0x1D };
////static const struct smbus_cmd_t SET_TPC_RECONNECT = { 0x08, 0x03, 0x1F };
//static const struct smbus_cmd_t FORCE_SET_POWER_SWITCH = { 0x08, 0x03, 0x21 };
//static const struct smbus_cmd_t GET_RDO = { 0x08, 0x02, 0x84 };
//static const struct smbus_cmd_t GET_VDO = { 0x08, 0x03, 0x9A };
////static const struct smbus_cmd_t GET_CURRENT_PARTNER_SRC_PDO = { 0x08, 0x02,
////								0xA7 };
////static const struct smbus_cmd_t RTS_SET_FRS_FUNCTION = { 0x08, 0x03, 0xE1 };
////static const struct smbus_cmd_t GET_TPC_CSD_OPERATION_MODE = { 0x08, 0x02,
////							       0x9D };
//static const struct smbus_cmd_t GET_RTK_STATUS = { 0x09, 0x03 };
//static const struct smbus_cmd_t RTS_UCSI_CONNECTOR_RESET = { 0x0E, 0x03, 0x03 };
//static const struct smbus_cmd_t RTS_UCSI_GET_CAPABILITY = { 0x0E, 0x02, 0x06 };
//static const struct smbus_cmd_t RTS_UCSI_GET_CONNECTOR_CAPABILITY = { 0x0E,
//								      0x03,
//								      0x07 };
//static const struct smbus_cmd_t RTS_UCSI_SET_UOR = { 0x0E, 0x04, 0x09 };
//static const struct smbus_cmd_t RTS_UCSI_SET_PDR = { 0x0E, 0x04, 0x0B };
//static const struct smbus_cmd_t RTS_UCSI_GET_PDOS = { .cmd = 0x0E,
//						      .len = 0x05,
//						      .sub = 0x10 };
//static const struct smbus_cmd_t RTS_UCSI_GET_CONNECTOR_STATUS = { 0x0E, 0x3,
//								  0x12 };
//static const struct smbus_cmd_t RTS_UCSI_GET_ERROR_STATUS = { 0x0E, 0x03,
//							      0x13 };
//static const struct smbus_cmd_t RTS_UCSI_READ_POWER_LEVEL = { 0x0E, 0x05,
//							      0x1E };
//static const struct smbus_cmd_t RTS_UCSI_SET_CCOM = { 0x0E, 0x04, 0x08 };
////static const struct smbus_cmd_t SET_RETIMER_FW_UPDATE_MODE = { 0x20, 0x03,
////							       0x00 };
//static const struct smbus_cmd_t RTS_UCSI_GET_CABLE_PROPERTY = { 0x0E, 0x03,
//								0x11 };
////static const struct smbus_cmd_t GET_PCH_DATA_STATUS = { 0x08, 0x02, 0xE0 };
//static const struct smbus_cmd_t ACK_CC_CI = { 0x0A, 0x07, 0x00 };
//static const struct smbus_cmd_t RTS_UCSI_GET_LPM_PPM_INFO = { 0x0E, 0x03,
//							      0x22 };
//static const struct smbus_cmd_t RTS_UCSI_GET_ATTENTION_VDO = { 0x0E, 0x03,
//							       0x16 };
////__maybe_unused static const struct smbus_cmd_t RTS_SET_SBU_MUX_MODE = { 0x30,
////									0x01 };
////static const struct smbus_cmd_t SET_BBR_CTS = { 0x08, 0x03, 0x27 };


static const struct smbus_cmd_t GET_ALERT_STATUS = { 0xBD, 0x01 };
static const struct smbus_cmd_t CLR_ALERT_STATUS = { 0xBC };
static const struct smbus_cmd_t SET_CONTROL = { 0x81 };
static const struct smbus_cmd_t SET_MSGOUT = { 0x83 };
static const struct smbus_cmd_t GET_CCI = { 0x80 };
static const struct smbus_cmd_t GET_MSGIN = { 0x82 };

//static const struct smbus_cmd_t VENDOR_CMD_RESET = { 0xF4, 0xA0/*global reset*/, 0x7F };
static const struct smbus_cmd_t SET_PPM_RESET = { 0x81, 0x08, 0x01 };
static const struct smbus_cmd_t SET_NOTIFICATION_ENABLE = { 0x81, 0x08, 0x05 };
static const struct smbus_cmd_t GET_IC_STATUS = { 0x81, 0x08, 0x20 };
static const struct smbus_cmd_t GET_ERROR_STATUS = { 0x81, 0x08, 0x13 };
static const struct smbus_cmd_t SET_PDO = { 0x81, 0x08, 0x1D };
static const struct smbus_cmd_t GET_CONNECTOR_STATUS = { 0x81, 0x08, 0x12 };
static const struct smbus_cmd_t SET_SINK_PATH = { 0x81, 0x08, 0x1C };
static const struct smbus_cmd_t SET_CCOM = { 0x81, 0x08, 0x08 };
static const struct smbus_cmd_t SET_TPC_RP = { 0x81, 0x08, 0x14 };
static const struct smbus_cmd_t READ_POWER_LEVEL = { 0x81, 0x08, 0x1E };
static const struct smbus_cmd_t GET_CONNECTOR_CAPABILITY = { 0x81, 0x08, 0x07 };
static const struct smbus_cmd_t SET_UOR = { 0x81, 0x08, 0x09 };
static const struct smbus_cmd_t SET_PDR = { 0x81, 0x08, 0x0B };
static const struct smbus_cmd_t GET_VDO = { 0x81, 0x08, 0x20 };
static const struct smbus_cmd_t GET_PDOS = { 0x81, 0x08, 0x10 };
static const struct smbus_cmd_t SET_RDO = { 0x81, 0x08, 0x20 };
static const struct smbus_cmd_t GET_CABLE_PROPERTY = { 0x81, 0x08, 0x11 };
static const struct smbus_cmd_t GET_RDO = { 0x81, 0x08, 0x20 };
static const struct smbus_cmd_t SET_CONNECTOR_RESET = { 0x81, 0x08, 0x03 };
static const struct smbus_cmd_t GET_CAPABILITY = { 0x81, 0x08, 0x06 };
static const struct smbus_cmd_t GET_LPM_PPM_INFO = { 0x81, 0x08, 0x22 };
static const struct smbus_cmd_t GET_ATTENTION_VDO = { 0x81, 0x08, 0x16 };

static const struct smbus_cmd_t ACK_CC_CI = { 0x81, 0x08, 0x04 };

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
	/** Set the PDC ACK of Notifications */
	//INIT_PDC_SET_ACK_CC_CI_TO_NTF, /* embedded to ST_WRITE_ACK */
	/** Get the PDC IC Status by VDM */
	INIT_PDC_GET_VDM_IC_STATUS,
	/** Set the PDC ACK of VDM */
	//INIT_PDC_SET_ACK_CC_CI_TO_VDM, /* embedded to ST_WRITE_ACK */
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
};

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
	/** Whether or not this port supports FRS */
	bool frs_supported;
	/** Pointer to the device-specific callback function */
	gpio_callback_handler_t callback_handler;
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
	union cci_message_t cci_message; //most same as cci_event_t
	/** Timepoint for when we can next call get [CCI] message. */
	k_timepoint_t next_cci_message;
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
	/** CC Event callback */
	struct pdc_callback *cc_cb;      //pdc_cc_handler_cb(): handle cmd complete indicator, called in handle_irqs()
	/** CC Event one-time callback */
	struct pdc_callback *cc_cb_tmp;  //assign in it52xx_post_command_with_callback(), called in handle_irqs()
	/** Asynchronous (CI) Event callbacks */
	sys_slist_t ci_cb_list;          //list: pdc_ci_handler_cb(): handle connect change & vendor indicator, called in handle_irqs()
	/** Asynchronous (CI) interrupt flag */
	bool ci_interrupt;
	/** Set "change indicator" field of CMD_ACK_CC_CI to 1b */
	bool ack_ci;
	/** Information about the PDC */
	struct pdc_info_t info;
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
	[CMD_SET_DRP_MODE] = "SET_DRP_MODE",
	[CMD_GET_DRP_MODE] = "GET_DRP_MODE",
	[CMD_SET_SINK_PATH] = "SET_SINK_PATH",
	[CMD_READ_POWER_LEVEL] = "READ_POWER_LEVEL",
	[CMD_GET_RDO] = "GET_RDO",
	[CMD_SET_TPC_RP] = "SET_TPC_RP",
	[CMD_SET_TPC_RECONNECT] = "SET_TPC_RECONNECT",
	[CMD_SET_RDO] = "SET_RDO",
	[CMD_GET_CURRENT_PARTNER_SRC_PDO] = "GET_CURRENT_PARTNER_SRC_PDO",
	[CMD_SET_FRS_FUNCTION] = "SET_FRS_FUNCTION",
	[CMD_SET_RETIMER_FW_UPDATE_MODE] = "SET_RETIMER_FW_UPDATE_MODE",
	[CMD_GET_CABLE_PROPERTY] = "GET_CABLE_PROPERTY",
	[CMD_GET_VDO] = "GET VDO",
	[CMD_GET_IDENTITY_DISCOVERY] = "CMD_GET_IDENTITY_DISCOVERY",
	[CMD_GET_IS_VCONN_SOURCING] = "CMD_GET_IS_VCONN_SOURCING",
	[CMD_SET_PDO] = "CMD_SET_PDO",
	[CMD_GET_PCH_DATA_STATUS] = "CMD_GET_PCH_DATA_STATUS",
	[CMD_ACK_CC_CI] = "CMD_ACK_CC_CI",
	[CMD_RAW_UCSI] = "CMD_RAW_UCSI",
	[CMD_GET_LPM_PPM_INFO] = "CMD_GET_LPM_PPM_INFO",
	[CMD_GET_ATTENTION_VDO] = "CMD_GET_ATTENTION_VDO",
	[CMD_GET_SBU_MUX_MODE] = "CMD_GET_SBU_MUX_MODE",
	[CMD_SET_SBU_MUX_MODE] = "CMD_SET_SBU_MUX_MODE",
	[CMD_SET_BBR_CTS] = "CMD_SET_BBR_CTS",
};

/**
 * @brief List of human readable state names for console debugging
 */
/* TODO(b/325128262): Bug to explore simplifying the the state machine */
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

static struct gpio_dt_spec
	it52xx_irq_list[DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)];
static const struct smf_state states[];
//static int it52xx_pdc_reset(const struct device *dev);
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

/**
 * @brief PDC port data (pointer array to pdc_data_##inst) used in interrupt handler
 */
static struct pdc_data_t *const pdc_data[NUM_PDC_IT52XX_PORTS];

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
 *
 * TODO(b/323371550) When more than one PDC is supported, this flag will need
 * to be tracked per-chip.
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
		return;
	}

	/* Report CCI event to PPM, this value may not be the same as [CCI] of LPM replied. ex. ACK_CC_CI */
	LOG_INF("ITE%d: CCI event to PPM=0x%x", cfg->connector_number, cci.raw_value);

	/*
	 * CC and CI events are separately reported. So, we need to call only
	 * one callback or the other.
	 */
	if (cci.connector_change) {
		pdc_fire_callbacks(&data->ci_cb_list, data->dev, cci); //pdc_ci_handler_cb()
	} else if (data->cc_cb_tmp) {
		data->cc_cb_tmp->handler(data->dev, data->cc_cb_tmp, cci);
	} else if (data->cc_cb) {
		data->cc_cb->handler(data->dev, data->cc_cb, cci); //pdc_cc_handler_cb()
	}

	data->cci_event.raw_value = 0;
}

#if 0 //it5271 doesn't support ARA
static int get_ara(const struct device *dev, uint8_t *ara)
{
	const struct pdc_config_t *cfg = dev->config;

	return i2c_read(cfg->i2c.bus, ara, 1, SMBUS_ADDRESS_ARA);
}
#endif

static void perform_pdc_init(struct pdc_data_t *data)
{
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

/* Get [CCI] */
static int get_cci_message(const struct device *dev)
{
	//struct pdc_data_t *data = dev->data;
	//const struct pdc_config_t *cfg = dev->config;
	//struct i2c_msg msg;

	/* only read 1 byte CCI */
	//msg.buf = &data->cci_message.raw_value;
	//msg.len = 1;
	//msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	//return i2c_transfer_dt(&cfg->i2c, &msg, 1);

	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t cmd = GET_CCI.cmd;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->cci_message.raw_value; /* original 1byte (read rtk reg), we need 5byte space */
	msg[1].len = 5; /* W CCI - R [04 xx xx xx xx] => len = [5] */
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, msg, 2);
}

/* Get [MSGIN] */
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
				   PDC_TRACE_CHIP_TYPE_RTS54XX, data->rd_buf,
				   data->cci_message.data_len + 1);
	}

	return rv;
}

/* Set [CONTROL]/[MSGOUT] */
static int it52xx_i2c_write(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;

	//Transmit MSGOUT data + CONTROL cmd
	if (data->cmd == CMD_SET_PDO) {
		struct i2c_msg msg[2];
		uint32_t len = IT52XX_CONTROL_CMD_BASE_LENGTH;

		/* TODO: [CONTROL] -> [MSGOUT], or [MSGOUT] -> [CONTROL] */
		msg[1].buf = &data->wr_buf[IT52XX_CONTROL_CMD_BASE_LENGTH];
		/* MSGOUT len = total len - CONTROL cmd len */
		msg[1].len = (data->wr_buf_len - len);
		msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		i2c_transfer_dt(&cfg->i2c, &msg[1], 1);

		msg[0].buf = data->wr_buf;
		/* CONTROL cmd len */
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
	data->init_local_current_state = data->init_local_state;
	data->init_local_next_state = next;
	data->init_local_state = INIT_PDC_CMD_WAIT;
	set_state(data, ST_WRITE);
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

static enum smf_state_result st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	int cnum = cfg->connector_number;
	//union conn_status_change_bits_t ci;
	//bool cc = 1;
	int rv;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	switch (data->init_local_state) {
	case INIT_PDC_SET_PPM_RESET: /* flow ppm reset -> set ntf (spec): it5271 ignore cmd that prior ppm_reset */
#if 0 /* TODO: Not UCSI cmd, [ST_SUSPENDED] & [ST_ERROR_RECOVERY] call to here(reset driver + PDC),
       *       check by console cmd, rtk don't re-connect AC, so can't reboot it52xx in here
       */
		rv = it52xx_pdc_reset(data->dev);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_RESET)", cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		/* it52xx boot to ready */
		crec_usleep(200*MSEC);
#endif
		rv = it52xx_ppm_reset(data->dev);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_PPM_RESET)", cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_SET_NOTIFICATION_ENABLE);
		return SMF_EVENT_HANDLED;
	case INIT_PDC_SET_NOTIFICATION_ENABLE:
		rv = it52xx_set_notification_enable(data->dev, cfg->bits);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_NOTIFICATION_ENABLE)",
				cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_GET_VDM_IC_STATUS /* INIT_PDC_SET_ACK_CC_CI_TO_NTF */);
		return SMF_EVENT_HANDLED;
#if 0 //embedded to [ST_WRITE_ACK] state
	case INIT_PDC_SET_ACK_CC_CI_TO_NTF: /* it5271 is waiting ACK and ignore other [CONTROL] cmd */
		ci.connect_change = 0;
		rv = it52xx_ack_cc_ci(data->dev, ci, cc, 0);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_ACK_CC_CI_TO_NTF)", cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_GET_VDM_IC_STATUS);
		return SMF_EVENT_HANDLED;
#endif
	case INIT_PDC_GET_VDM_IC_STATUS: /* 1. Not UCSI cmd, so can't run sm: [ST_WRITE] -> [ST_GET_CCI] -> [ST_READ] */
					 /* 2. it5271 add support: [CONTROL] VDM to get pdc_info_t data (LPM_PPM info not enough data), after set PPM_RESET */
		rv = it52xx_get_info(data->dev, &data->info, true);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_GET_VDM_IC_STATUS)", cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_COMPLETE /* INIT_PDC_SET_ACK_CC_CI_TO_VDM */);
		return SMF_EVENT_HANDLED;
#if 0 //embedded to [ST_WRITE_ACK] state
	case INIT_PDC_SET_ACK_CC_CI_TO_VDM: /* it5271 is waiting ACK and ignore other [CONTROL] cmd  */
		rv = it52xx_ack_cc_ci(data->dev, ci, cc, 0);
		if (rv) {
			LOG_ERR("ITE%d: Internal(INIT_PDC_SET_ACK_CC_CI_TO_VDM)", cnum);
			set_state(data, ST_DISABLE);
			return SMF_EVENT_HANDLED;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_COMPLETE);
		return SMF_EVENT_HANDLED;
#endif
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
		/* If PDC_RESET was sent, check the reset_completed flag */
		if (data->init_local_current_state == INIT_PDC_SET_PPM_RESET) {
			//LOG_ERR("ST1: INIT_PDC_CMD_WAIT, RC %d", data->cci_event.reset_completed);
			if (!data->cci_event.reset_completed) {
				return SMF_EVENT_HANDLED;
			}
		} else if (!data->cci_event.command_completed) {
			return SMF_EVENT_HANDLED;
		}

		if (data->cci_event.error) {
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
				LOG_INF("ITE%d: PDC error status read fail ",
					cfg->connector_number);
				set_state(data, ST_ERROR_RECOVERY);
				return SMF_EVENT_HANDLED;
			}

			/* PDC returned an error */
			data->init_local_state = INIT_ERROR;
		} else {
			/* PDC Error status was read */
			if (data->init_local_current_state == INIT_ERROR) {
				/* Display error read from cci_message */
				init_display_error_status(data);
				/* Retry init or disable this port */
				if (data->init_retry_counter <=
				    N_INIT_RETRY_ATTEMPT_MAX) {
					data->init_retry_counter++;
					data->init_local_state =
						INIT_PDC_SET_PPM_RESET;
				} else {
					set_state(data, ST_DISABLE);
				}
				return SMF_EVENT_HANDLED;
			}

			//LOG_INF("ITE1: INIT_PDC_CMD_WAIT set st");
			data->init_local_state = data->init_local_next_state;
		}
		break;
	}

	return SMF_EVENT_HANDLED;
}

/**
 * @brief Called from the main thread to handle interrupts
 */
/* TODO: rtk driver get PDC command status by polling,
 *       Work around: change it by read 0xBD reg when it5271 trigger alert
 *       when it5271 can de-assert by rx ACK_CC_CI, then we should change back by polling.
 *       it5271 doesn't support ARA
 */
static void handle_irqs(struct pdc_data_t *data)
{
	uint8_t interrupt_status;
	int rv;

	for (int i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		struct pdc_data_t *const pdc_int_data = pdc_data[i];

		if ((pdc_int_data == NULL) || !device_is_ready(pdc_int_data->dev)) {
			/* This PDC is not initialized. Ignore it. */
			continue;
		}

		const struct pdc_config_t *cfg = pdc_int_data->dev->config;

		/* Read the interrupt status register to determine which port generated the interrupt */
		rv = it52xx_get_alert_status(pdc_int_data->dev);
		if (rv) {
			LOG_ERR("ITE%d: IRQ Get interrupt status failed", cfg->connector_number);
			/* TODO: Refer to st_get_cci_entry & run() but not do retry, not verified */
			/* Clear the CCI Event */
			pdc_int_data->cci_event.raw_value = 0;
			/*
			 * The command was not successfully completed,
			 * so set cci_event.error to 1b.
			 */
			pdc_int_data->cci_event.error = 1;
			/* Command has completed */
			pdc_int_data->cci_event.command_completed = 1;
			/* Clear busy event */
			pdc_int_data->cci_event.busy = 0;
			/* Set error, I2C read error */
			pdc_int_data->error_status.i2c_read_error = 1;
			/* Notify system of status change */
			call_cci_event_cb(pdc_int_data);
			set_state(pdc_int_data, ST_ERROR_RECOVERY);
			/* done with this port */
			break;
		}
		interrupt_status = pdc_int_data->rd_buf[0];

		/* We only handle UCSI interrupt events */
		if (interrupt_status & IT52XX_INT_UCSI_STATUS) {
			if (get_state(pdc_int_data) == ST_TASK_WAIT) {
				LOG_INF("ITE%d: IRQ-CC", cfg->connector_number);
				/* We're waiting [CCI] message in ST_TASK_WAIT state */
#if 0
				/* Clear it52xx the pending interrupt events,
				 * this won't clear it5271 CCI message buffer (TODO: clear before get [CCI] race condition? or clear after get [CCI]) */
				rv = it52xx_clr_alert_status(pdc_int_data->dev, interrupt_status);
				if (rv) {
					LOG_ERR("ITE%d: Clear interrupt status failed", cfg->connector_number);
					/* TODO: Refer to st_write_entry & run() but not do retry, not verified */
					/* Clear the CCI Event */
					pdc_int_data->cci_event.raw_value = 0;
					/*
					 * The command was not successfully completed,
					 * so set cci_event.error to 1b.
					 */
					pdc_int_data->cci_event.error = 1;
					/* Command has completed */
					pdc_int_data->cci_event.command_completed = 1;
					/* Clear busy event */
					pdc_int_data->cci_event.busy = 0;
					/* Set error, I2C write error */
					pdc_int_data->error_status.i2c_write_error = 1;
					/* Notify system of status change */
					call_cci_event_cb(pdc_int_data);
					set_state(pdc_int_data, ST_ERROR_RECOVERY);
					/* done with this port */
					break;
				}
#endif
				set_state(pdc_int_data, ST_GET_CCI);
				/* done with this port */
				break;
			} else {
				LOG_INF("ITE%d: IRQ-CI", cfg->connector_number);
				/* Inform subsystem of the ci interrupt */
				pdc_int_data->ci_interrupt = 1;
				/* Clear the CCI Event */
				pdc_int_data->cci_event.raw_value = 0;
				/* Set the port the CCI Event occurred on */
				pdc_int_data->cci_event.connector_change = cfg->connector_number + 1;
				/* Set CCI EVENT for vendor defined indicator
				 * (informs subsystem that an interrupt occurred) */
				pdc_int_data->cci_event.vendor_defined_indicator = 1;
#if 0
				/* Clear it52xx the pending interrupt events,
				 * this won't clear it5271 CCI message buffer (TODO: clear before get [CCI] race condition? or clear after get [CCI]) */
				rv = it52xx_clr_alert_status(pdc_int_data->dev, interrupt_status);
				if (rv) {
					LOG_ERR("ITE%d: Clear interrupt status failed", cfg->connector_number);
					/* TODO: Refer to st_write_entry & run() but not do retry, not verified */
					/* Clear the CCI Event */
					pdc_int_data->cci_event.raw_value = 0;
					/*
					 * The command was not successfully completed,
					 * so set cci_event.error to 1b.
					 */
					pdc_int_data->cci_event.error = 1;
					/* Command has completed */
					pdc_int_data->cci_event.command_completed = 1;
					/* Clear busy event */
					pdc_int_data->cci_event.busy = 0;
					/* Set error, I2C write error */
					pdc_int_data->error_status.i2c_write_error = 1;
					/* Notify system of status change */
					call_cci_event_cb(pdc_int_data);
					set_state(pdc_int_data, ST_ERROR_RECOVERY);
					/* done with this port */
					break;
				}
#endif
				/* Notify system of status change */
				call_cci_event_cb(pdc_int_data);
				set_state(pdc_int_data, ST_GET_CCI);
				/* done with this port */
				break;
			}
		} else {
			LOG_INF("ITE%d: IRQ-unknown event", cfg->connector_number);
			/* Just clear status, we don't handle non-UCSI events. */
			rv = it52xx_clr_alert_status(pdc_int_data->dev, interrupt_status);
			if (rv) {
				LOG_ERR("ITE%d: Clear interrupt status failed", cfg->connector_number);
				/* TODO: Refer to st_write_entry & run() but not do retry, not verified */
				/* Clear the CCI Event */
				pdc_int_data->cci_event.raw_value = 0;
				/*
				 * The command was not successfully completed,
				 * so set cci_event.error to 1b.
				 */
				pdc_int_data->cci_event.error = 1;
				/* Command has completed */
				pdc_int_data->cci_event.command_completed = 1;
				/* Clear busy event */
				pdc_int_data->cci_event.busy = 0;
				/* Set error, I2C write error */
				pdc_int_data->error_status.i2c_write_error = 1;
				/* Notify system of status change */
				call_cci_event_cb(pdc_int_data);
				set_state(pdc_int_data, ST_ERROR_RECOVERY);
				/* done with this port */
				break;
			}
		}
	}
}

#if 0
static int handle_irqs(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt pdc_interrupt;
	int rv;
	int i;
	bool interrupt_pending = false;

	/* Read the pending interrupt events */
	rv = tps_rd_interrupt_event(&cfg->i2c, &pdc_interrupt);
	if (rv) {
		LOG_ERR("Read interrupt events failed");
		return rv;
	}

	/* All raw_value data uses byte-0 for contains the register data was
	 * written too, or read from, and byte-1 contains the length of said
	 * data. The actual data starts at index 2. */
	LOG_DBG("IRQ PORT %d", cfg->connector_number);
	for (i = 0; i < sizeof(union reg_interrupt); i++) {
		LOG_DBG("Byte%d: %02x", i, pdc_interrupt.raw_value[i]);
		if (pdc_interrupt.raw_value[i]) {
			interrupt_pending = true;
		}
	}
	LOG_DBG("\n");

	if (interrupt_pending && pdc_interrupt.patch_loaded) {
		/* patch_loaded is a shared interrupt bit which is not cleared
		 * individually so set ST_INIT state to all ports to avoid
		 * clearing it before handling irq on other ports. */
		set_all_ports_to_init(/*delay_ms=*/0);
		return 0;
	}

	if (!interrupt_pending) {
		return 0;
	}

	/* Set CCI EVENT for not supported */
	data->cci_event.not_supported = pdc_interrupt.not_supported_received;

	/* Set CCI EVENT for vendor defined indicator (informs subsystem
	 * that an interrupt occurred */
	data->cci_event.vendor_defined_indicator = 1;

	/* If a UCSI event is seen, stop using the cached connector
	 * status change bits and re-read from PDC and set CCI_EVENT for
	 * connector change.
	 */
	if (pdc_interrupt.ucsi_connector_status_change_notification) {
		data->use_cached_conn_status_change = false;
		data->cci_event.connector_change = cfg->connector_number + 1;
	}

	if (pdc_interrupt.plug_insert_or_removal) {
		atomic_set(&data->set_rdo_possible, 0);
		atomic_set(&data->sink_enable_possible, 0);
	}

	if (pdc_interrupt.sink_ready) {
		atomic_set(&data->set_rdo_possible, 1);
		k_work_reschedule(&data->new_power_contract,
				  K_MSEC(PDC_TI_NEW_POWER_CONTRACT_DELAY_MS));
	}

	if (pdc_interrupt.new_contract_as_consumer) {
		atomic_set(&data->sink_enable_possible, 1);
		k_work_reschedule(&data->new_power_contract,
				  K_MSEC(PDC_TI_NEW_POWER_CONTRACT_DELAY_MS));
	}

	/* TODO(b/345783692): Handle other interrupt bits. */

	/* Clear the pending interrupt events */
	rv = tps_rw_interrupt_clear(&cfg->i2c, &pdc_interrupt, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Clear interrupt events failed");
		return rv;
	}

	/* Inform the subsystem of the event */
	call_cci_event_cb(data);

	/*
	 * Check if interrupt is still active from any of the ports.
	 * It's possible that the PDC will set another bit in the
	 * interrupt status register of any of the port between the time
	 * when EC reads this register and clears these status bits
	 * above. If there is still another interrupt pending, then the
	 * interrupt line will still be active.
	 */
	tps_check_and_notify_irq();

	return 0;
}
#endif

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

	/* Write the [CONTROL] &| [MSGOUT] command */
	rv = it52xx_i2c_write(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	/* TODO: need T_INITIAL_GET_CCI_MSG/T_GET_CCI_MSG delay to avoid starvation?
	 * Needn't, it5271 trigger INT when [CCI] is ready to read (not by polling).
	 * Also it5271 needs ACK cmd following [CTRL] cmd, removing these delays can speed up it5271 to execute cmd, ex.it52xx_execute_ucsi_cmd()
	 */
	/* I2C transaction succeeded. Set timepoint for next get [CCI] message. */
	//data->next_cci_message = sys_timepoint_calc(K_MSEC(T_INITIAL_GET_CCI_MSG));
	//set_state(data, ST_GET_CCI);
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

	/* This state can only be entered from the Write or Get [CCI] state */
	assert(data->last_state == ST_WRITE || data->last_state == ST_WRITE_ACK ||
	       data->last_state == ST_GET_CCI);
}

static enum smf_state_result st_task_wait_run(void *o)
{
	/* Wait [CCI] interrupt from PDC */
	return SMF_EVENT_HANDLED;
}

static void st_get_cci_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Task Wait or Idle state */
	assert(data->last_state == ST_TASK_WAIT || data->last_state == ST_IDLE);

	/* Clear I2c Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Clear [CCI] Message */
	for (int i = 0; i < PDC_MAX_CCI_LENGTH; i++) {
		data->cci_message.raw_value[i] = 0;
	}
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static enum smf_state_result st_get_cci_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	union conn_status_change_bits_t ci;
	bool cc = 1;
	int rv;

	/*
	 * Make sure that we've waited sufficient time before re-reading [CCI]
	 * message. Otherwise PDC may be starved of time to execute commands.
	 */
	//if (!sys_timepoint_expired(data->next_cci_message)) {
	//	k_sleep(sys_timepoint_timeout(data->next_cci_message));
	//}

	/* Read the [CCI] message */
	rv = get_cci_message(data->dev);

	/* Reset time until next get [CCI] message */
	//data->next_cci_message = sys_timepoint_calc(K_MSEC(T_GET_CCI_MSG));

	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	if (data->cci_message.data_len == 0) {
		/*
		 * If there is no [MSGIN], then clear UCSI interrupt event
		 * after get [CCI] message,
		 * and this also clear it52xx CCI message memory.
		 */
		rv = it52xx_clr_alert_status(data->dev, IT52XX_INT_UCSI_STATUS);

		if (rv < 0) {
			/* Clear the CCI Event */
			//data->cci_event.raw_value = 0; //needn't?
			/* TODO: so far use the same retry counter, create another retry counter? not verified */
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
			cfg->connector_number,
			data->cci_message.raw_value[0],
			data->cci_message.raw_value[1],
			data->cci_message.raw_value[2],
			data->cci_message.raw_value[3],
			data->cci_message.raw_value[4]);
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
	}

	switch (data->cci_message.cmd_sts) {
	case CMD_BUSY:
		LOG_ERR("ITE%d: [CCI] Busy indicator", cfg->connector_number);
		/*
		 * Busy and Deferred are handled the same,
		 * so fall through
		 */
	//	__attribute__((fallthrough));
	//case CMD_DEFERRED:
		/*
		 * If Busy, then set this cci_event.busy to a 1b and all other
		 * fields to zero. Only notify subsystem of busy event
		 * for the first time.
		 */
		data->cci_event.busy = 1;
		/* Notify system of status change */
		call_cci_event_cb(data);
		/* Once it52xx processes the [CONTROL] cmd, it will trigger INT again. */
		set_state(data, ST_TASK_WAIT);
		break;
	case CMD_RESET_COMPLETE:
	case CMD_ACK_COMPLETE:
	case CMD_DONE:
		/* Clear busy event */
		data->cci_event.busy = 0;

		if (data->cmd == CMD_PPM_RESET) {
			/* The PDC has been reset,
			 * so set cci_event.reset_completed to 1b.
			 */
			data->cci_event.reset_completed = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);
			LOG_INF("ITE%d: [CCI] PPM reset PDC complete",
				cfg->connector_number);
			/* All done, return to Init or Idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		} else {
			LOG_INF("ITE%d: [CCI] = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }",
				cfg->connector_number,
				data->cci_message.raw_value[0],
				data->cci_message.raw_value[1],
				data->cci_message.raw_value[2],
				data->cci_message.raw_value[3],
				data->cci_message.raw_value[4]);
			/*
			 * The command completed successfully,
			 * so set cci_event.command_completed to 1b.
			 */
			data->cci_event.command_completed = 1;

			if (data->cci_message.data_len > 0) {
				/* Data is available, so read it */
				set_state(data, ST_READ);
			} else {
				/* Inform the system of the event */
				call_cci_event_cb(data);

				if (data->cmd != CMD_ACK_CC_CI) {
					/*
					 * It needs to write ACK_CC_CI following
					 * [CONTROL] cmd to it52xx (except CMD_PPM_RESET).
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
					 * Already send ACK_CC_CI, return to Idle
					 * or Init state
					 */
					TRANSITION_TO_INIT_OR_IDLE_STATE(data);
				}
			}
		}
		break;
	case CMD_ERROR:
		LOG_ERR("ITE%d: [CCI] Error indicator", cfg->connector_number);
		/*
		 * The command was not successfully completed,
		 * so set cci_event.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* Clear busy event */
		data->cci_event.busy = 0;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/*
		 * A command error occurred, the subsystem should read
		 * the status_register to determine the cause.
		 */
		if (data->cmd != CMD_PPM_RESET) {
			/*
			 * It needs to write ACK_CC_CI following
			 * [CONTROL] cmd to it52xx (except CMD_PPM_RESET).
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
		/* [CCI] returned an unknown command */
		LOG_ERR("ITE%d: unknown [CCI] = { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }",
				cfg->connector_number,
				data->cci_message.raw_value[0],
				data->cci_message.raw_value[1],
				data->cci_message.raw_value[2],
				data->cci_message.raw_value[3],
				data->cci_message.raw_value[4]);
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
	/* Clear I2c Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Set the port the CCI Event occurred on */
}

static enum smf_state_result st_read_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	uint8_t offset;
	uint8_t len;
	union conn_status_change_bits_t ci;
	bool cc = 1;
	int rv;

	/*
	 * The data->user_buf is checked for NULL before a command is queued.
	 * The check here guards against an erroneous cci_message indicating
	 * data is available for a command that doesn't send data.
	 */
	if (!data->user_buf) {
		LOG_ERR("NULL read buffer pointer");
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* Null buffer error */
		data->error_status.null_buffer_error = 1;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/* An error occurred, return to idle state */
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
	}

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
		/* Clear the CCI Event */
		//data->cci_event.raw_value = 0; //needn't?
		/* TODO: so far use the same retry counter, create another retry counter? not verified */
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return SMF_EVENT_HANDLED;
	}

	/* Get length of data returned */
	len = data->rd_buf[0];

	/* Skip over length byte */
	offset = 1;

	/* Copy the received data to the user's buffer */
	switch (data->cmd) {
	case CMD_GET_IC_STATUS: {
		struct pdc_info_t *info = (struct pdc_info_t *)data->user_buf;

		it52xx_unpack_get_ic_status_response(data->rd_buf, info);

		/* Project name string is supported on version >= 0.3.x */
		memcpy(info->project_name,
		       &data->rd_buf[IT52XX_GET_IC_STATUS_PROG_NAME_STR_INDEX],
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
				cfg->connector_number, info->pd_revision,
				info->pd_version);
		}

		/* Fill in the chip type (driver compat string) */
		strncpy(info->driver_name, STRINGIFY(DT_DRV_COMPAT),
			sizeof(info->driver_name));
		info->driver_name[sizeof(info->driver_name) - 1] = '\0';

		info->no_fw_update = cfg->no_fw_update;
		info->frs_supported = cfg->frs_supported;

		/* Retain a cached copy of this data */
		data->info = *info;

		break;
	}

	case CMD_GET_CONNECTOR_STATUS:{
		memcpy(data->user_buf, data->rd_buf + offset, len);
		union connector_status_t *status =
			(union connector_status_t *)(data->rd_buf + offset);
		data->cached_conn_status = *status;
		break;
	}

	case CMD_GET_VBUS_VOLTAGE: {
		union connector_status_t *status =
			(union connector_status_t *)(data->rd_buf + offset);
		*(uint16_t *)data->user_buf = status->voltage_reading *
					      status->voltage_scale *
					      VOLTAGE_SCALE_FACTOR;
		break;
	}
	case CMD_GET_ERROR_STATUS: {
		union error_status_t *es =
			(union error_status_t *)data->user_buf;

		/*
		 * ITE GET_ERROR_STATUS bits are the same as UCSI spec defined
		 * (vendor-defined field of ITE, so far not defined).
		 */
		es->raw_value = ((data->rd_buf[2] << 8) | data->rd_buf[1]);
		/*
		 * NOTE: Vendor Specific Error were already set in previous
		 * states
		 */
		break;
	}
	case CMD_GET_IDENTITY_DISCOVERY: { //optional api
		bool *disc_state = (bool *)data->user_buf;

		/* Realtek Altmode related state, Byte 14 bits 0-2*/
		*disc_state = (data->rd_buf[14] & 0x07);
		break;
	}
	case CMD_GET_IS_VCONN_SOURCING: { //optional api
		bool *vconn_sourcing = (bool *)data->user_buf;

		/* Realtek PD Sourcing VCONN, Byte 11, bit 5 */
		*vconn_sourcing = (data->rd_buf[11] & 0x20);
		break;
	}
	case CMD_GET_DRP_MODE: { //optional api
		enum drp_mode_t *drp_mode = (enum drp_mode_t *)data->user_buf;

		switch ((data->rd_buf[1] & GENMASK(5, 3)) >> 3) {
		case 0x0:
			*drp_mode = DRP_NORMAL;
			break;
		case 0x1:
			*drp_mode = DRP_TRY_SRC;
			break;
		case 0x2:
			*drp_mode = DRP_TRY_SNK;
			break;
		}
		break;
	}
#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
	case CMD_GET_SBU_MUX_MODE: { //optional api
		/* This is parsing a partial GET_IC_STATUS response (offset of
		 * 39, 1 byte) */

		enum pdc_sbu_mux_mode *mode_out =
			(enum pdc_sbu_mux_mode *)data->user_buf;
		uint8_t raw_mode = data->rd_buf[offset];

		switch (raw_mode) {
		case RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_NORMAL:
			*mode_out = PDC_SBU_MUX_MODE_NORMAL;
			break;
		case RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_FORCE_DBG:
			*mode_out = PDC_SBU_MUX_MODE_FORCE_DBG;
			break;
		default:
			*mode_out = PDC_SBU_MUX_MODE_INVALID;
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

	/* Clear the read buffer */
	memset(data->rd_buf, 0, sizeof(data->rd_buf));

	/*
	 * Set cci_event.data_len. This will be zero if no
	 * data is available.
	 */
	data->cci_event.data_len = len;
	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/*
	 * It needs to write ACK_CC_CI following
	 * [CONTROL] cmd to it52xx (except CMD_PPM_RESET).
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

	/*TODO: ADD ERROR RECOVERY CODE */
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
	/* TODO(b/325633531): Investigate using timestamps instead of counters
	 */
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

/* Populate cmd state table */
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
	[ST_GET_CCI] = SMF_CREATE_STATE(
		st_get_cci_entry, st_get_cci_run, NULL, NULL, NULL),
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
 * @param len Length of paylaod buffer
 * @param user_buf Pointer to buffer where response data will be written.
 * @return 0 on success
 * @return -EBUSY if command is already pending.
 * @return -ECONNREFUSED if chip communication is disabled
 */
static int it52xx_post_command_with_callback(const struct device *dev,
					    enum cmd_t cmd, const uint8_t *buf,
					    uint8_t len, uint8_t *user_buf,
					    struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/* Return an error if chip communication is suspended */
	if (check_comms_suspended()) {
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
				  PDC_TRACE_CHIP_TYPE_RTS54XX, data->wr_buf,
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

	/* Return an error if chip communication is suspended */
	if (check_comms_suspended()) {
		return -ECONNREFUSED;
	}

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

	/* Return an error if chip communication is suspended */
	if (check_comms_suspended()) {
		return -ECONNREFUSED;
	}

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

#if 1
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
	 * After it52xx reboot, it must needs UCSI init again, but mgmt seems not do...
	 */
	return it52xx_post_command(dev, CMD_TRIGGER_PDC_RESET, NULL, 0, NULL);
#else   /* Test only, needs UCSI init again, but mgmt seems not do... */
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	/* Reboot it52xx */
	uint8_t payload[] = {
		VENDOR_CMD_RESET.cmd,
		VENDOR_CMD_RESET.len, /* global reset */
		VENDOR_CMD_RESET.sub,
	};

	memcpy(data->wr_buf, payload, ARRAY_SIZE(payload));

	/* TODO: directly set payload to msg.buf (not through data->wr_buf_len) */
	data->wr_buf_len = ARRAY_SIZE(payload);
	//data->user_buf = NULL;            shouldn't overwrite (keep last CONTROL cmd user_buf)
	data->cmd = CMD_TRIGGER_PDC_RESET;
	//data->cc_cb_tmp = NULL;           shouldn't overwrite (keep last cmd cc_cb_tmp)

	//k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	//k_event_post(&data->driver_event, IT52XX_NEXT_STATE_READY);

	msg.buf = data->wr_buf;
	msg.len = data->wr_buf_len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
#endif
}

static int it52xx_ppm_reset(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

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

	return it52xx_post_command(dev, CMD_PPM_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_set_notification_enable(const struct device *dev,
					 union notification_enable_t bits)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_NOTIFICATION_ENABLE.cmd,
		SET_NOTIFICATION_ENABLE.len,
		SET_NOTIFICATION_ENABLE.sub,
		0x00,
		BYTE0(bits.raw_value),
		BYTE1(bits.raw_value),
		BYTE2(bits.raw_value),
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_NOTIFICATION_ENABLE, payload,
				  ARRAY_SIZE(payload), NULL);
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

	return it52xx_post_command(dev, CMD_ACK_CC_CI, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_get_info(const struct device *dev, struct pdc_info_t *info,
			  bool live)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte6 = 0;

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

		LOG_DBG("ITE%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(data->info.fw_version),
			PDC_FWVER_GET_MINOR(data->info.fw_version),
			PDC_FWVER_GET_PATCH(data->info.fw_version));
		return 0;
	}

	/* Handle a live read */

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, IT52XX_VDM_VDC_GET_PDC_INFO 1'b */
	/* bit[24:31]: IT52XX_VDM_VDC_GET_PDC_INFO 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) | ((IT52XX_VDM_VDC_GET_PDC_INFO & BIT(0)) << 7);
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

	LOG_DBG("ITE%d: Get live chip info", cfg->connector_number);

	return it52xx_post_command(dev, CMD_GET_IC_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)info);
}

static int it52xx_get_error_status(const struct device *dev,
				  union error_status_t *es)
{
	struct pdc_data_t *data = dev->data;
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
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_ERROR_STATUS.cmd,
		GET_ERROR_STATUS.len,
		GET_ERROR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_ERROR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)es);
}

static int it52xx_set_pdo(const struct device *dev, enum pdo_type_t type,
			 uint32_t *pdo, int count)
{
	struct pdc_data_t *data = dev->data;
	uint32_t pdo_info = 0;

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
	/* bit[24:31]: reserved 2'b, SRC or SNK PDO 1'b, num of pdo 4'b, data index 1'b */
	/* bit[32:39]: data index 6'b, end of message 1'b, reserved 1'b */
	pdo_info = (IT5271_CONNECTOR_NUMBER) | (type << 10) | ((count & 0xF) << 11) |
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

	return it52xx_post_command(dev, CMD_SET_PDO, payload, payload_len, NULL);
}

static int it52xx_get_connector_status(const struct device *dev,
				      union connector_status_t *cs)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cs == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_CONNECTOR_STATUS.cmd,
		GET_CONNECTOR_STATUS.len,
		GET_CONNECTOR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_CONNECTOR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)cs);
}

static int it52xx_set_sink_path(const struct device *dev, bool en)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, sink path enable/disable 1'b */
	byte5 = ((IT5271_CONNECTOR_NUMBER & 0x7f) | (en << 7));

	uint8_t payload[] = {
		SET_SINK_PATH.cmd,
		SET_SINK_PATH.len,
		SET_SINK_PATH.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_SINK_PATH, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_set_ccom(const struct device *dev, enum ccom_t ccom)
{
	struct pdc_data_t *data = dev->data;
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
	case CCOM_DRP: /* it52xx default is TrySRC DRP, and can't be selected at run-time. */
		conn_opmode = 1 << (opmode_offset + 2);
		break;
	}

	uint8_t payload[] = {
		SET_CCOM.cmd,
		SET_CCOM.len,
		SET_CCOM.sub,
		0x00, /* data length --> set to 0x00 */
		((IT5271_CONNECTOR_NUMBER & 0x7f) | (conn_opmode & 0xff)),
		(conn_opmode >> 8) & 0xff,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_CCOM, payload,
				  ARRAY_SIZE(payload), NULL);
}

#if 0 //optional api, Notify the PDC of the current AP power state and then?
static int it52xx_set_ap_power_state(const struct device *dev,
				    enum power_state state)
{
	struct pdc_data_t *data = dev->data;
	int byte;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (state == POWER_S0) {
		byte = SX_S0;
	} else if (state == POWER_S5) {
		byte = SX_S5;
	} else {
		return -EINVAL;
	}

	uint8_t payload[] = {
		SET_SYS_PWR_STATE.cmd,
		SET_SYS_PWR_STATE.len,
		SET_SYS_PWR_STATE.sub,
		0x00,
		byte,
	};

	return rts54_post_command(dev, CMD_SET_SYS_PWR_STATE, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

#if 0 //optional api, set it5271 DRP/ trysrc DRP (default)/ trysnk DRP
static int it52xx_set_drp_mode(const struct device *dev, enum drp_mode_t dm)
{
	struct pdc_data_t *data = dev->data;
	uint8_t opmode = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* Set CSD mode to DRP */
	opmode = 0x01;
	switch (dm) {
	case DRP_NORMAL:
		/* No Try.Src or Try.Snk
		 * opmode |= (0 << 3);
		 */
		break;
	case DRP_TRY_SRC:
		opmode |= (1 << 3);
		break;
	case DRP_TRY_SNK:
		opmode |= (2 << 3);
		break;
	case DRP_INVALID:
	default:
		LOG_ERR("Invalid DRP mode: %d", dm);
		break;
	}

	/* We always want Accessory Support */
	opmode |= (1 << 2);

	uint8_t payload[] = {
		SET_TPC_CSD_OPERATION_MODE.cmd,
		SET_TPC_CSD_OPERATION_MODE.len,
		SET_TPC_CSD_OPERATION_MODE.sub,
		0x00,
		opmode,
	};

	return it52xx_post_command(dev, CMD_SET_DRP_MODE, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

/*
 * This api isn't compliant with the UCSI spec, ppm/mgmt only set Rp value
 * through this command.
 */
static int it52xx_set_power_level(const struct device *dev,
				 enum usb_typec_current_t tcc)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte7 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, source or sink 1'b */
	byte5 = ((IT5271_CONNECTOR_NUMBER & 0x7f) | (IT52XX_SET_SRC_PWR_LVL << 7));
	/* bit[32:34]: usb type-c current 3'b */
	byte7 = tcc;

	uint8_t payload[] = {
		SET_TPC_RP.cmd,
		SET_TPC_RP.len,
		SET_TPC_RP.sub,
		0x00, /* data length --> set to 0x00 */
		byte5,
		0x00,
		byte7,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_TPC_RP, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_read_power_level(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte6, byte7 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * UCSI v3.0 spec: READ_POWER_LEVEL command is used by the OPM to read
	 * a port’s peak power and average power levels from the LPM that is in
	 * the sourcing mode (SRC role). So if we're not SRC, return not support.
	 */
	if (!data->cached_conn_status.power_direction) {
		return -ENOSYS;
	}

	/* bit[16:23]: connector num 7'b, time to read power 1'b */
	/* bit[24:31]: time to read power 4'b, reserved 3'b, time interval 1'b */
	/* bit[32]: time interval 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);
	/*
	 * TODO: Input parameter of it52xx_read_power_level() doesn't have these values,
	 *       so temporarily give a fix value for 100ms to read power & 10ms time interval.
	 *
	 *       b/435301650, it52xx only support voltage measurement (not current),
	 *       and so far it's okay for PPM.
	 */
	byte6 = 0x0;
	byte7 = 0x1;


	/*
	 * TODO(b/326276531): The implementation of this command is not yet
	 * complete. The fields 'time to read power` and `time interval between
	 * readings` are not being set and need to be both passed into this
	 * function from the PDC subsys API and set below.
	 */
	uint8_t payload[] = {
		READ_POWER_LEVEL.cmd,
		READ_POWER_LEVEL.len,
		READ_POWER_LEVEL.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		byte6,
		byte7,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_READ_POWER_LEVEL, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_get_connector_capability(const struct device *dev,
					  union connector_capability_t *caps)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (caps == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_CONNECTOR_CAPABILITY.cmd,
		GET_CONNECTOR_CAPABILITY.len,
		GET_CONNECTOR_CAPABILITY.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_CONNECTOR_CAPABILITY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)caps);
}

static int it52xx_set_uor(const struct device *dev, union uor_t uor)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, DRS to DFP 1'b */
	/* bit[24:31]: DRS to UFP 1'b, Enable/Disable DRS 1'b, reserved 6'b */
	uor.raw_value &= ~0x7f;
	uor.raw_value |= IT5271_CONNECTOR_NUMBER;

	uint8_t payload[] = {
		SET_UOR.cmd,
		SET_UOR.len,
		SET_UOR.sub,
		0x00, /* Data Length --> set to 0x00 */
		uor.raw_value & 0xff,
		(uor.raw_value >> 8) & 0xff,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_UOR, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_set_pdr(const struct device *dev, union pdr_t pdr)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, PRS to SRC 1'b */
	/* bit[24:31]: PRS to SNK 1'b, Enable/Disable PRS 1'b, reserved 6'b */
	pdr.raw_value &= ~0x7f;
	pdr.raw_value |= IT5271_CONNECTOR_NUMBER;

	uint8_t payload[] = {
		SET_PDR.cmd,
		SET_PDR.len,
		SET_PDR.sub,
		0x00, /* Data Length --> set to 0x00 */
		pdr.raw_value & 0xff,
		(pdr.raw_value >> 8) & 0xff,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_SET_PDR, payload,
				  ARRAY_SIZE(payload), NULL);
}

#if 0 //optional api
static int it52xx_set_frs(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		[0] = RTS_SET_FRS_FUNCTION.cmd,
		[1] = RTS_SET_FRS_FUNCTION.len,
		[2] = RTS_SET_FRS_FUNCTION.sub,
		[3] = 0x00,
		[4] = enable,
	};

	return it52xx_post_command(dev, CMD_SET_FRS_FUNCTION, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

static int it52xx_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			 uint8_t *vdo_req_list, uint32_t *vdo)
{
	struct pdc_data_t *data = dev->data;
	uint16_t vdo_type = 0;
	uint8_t vdc_cmd, byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL) {
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
	}

	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) | ((vdc_cmd & BIT(0)) << 7);
	byte6 = ((vdc_cmd >> 1) & 0xf) | ((IT52XX_VDM_VDC_VER & 0xf) << 4);

	/* bit map to enum vdo_type_t */
	for (int i = 0; i < vdo_req.num_vdos; i++) {
		vdo_type |= BIT(vdo_req_list[i]);
		LOG_DBG("vdo_req_list %d", vdo_req_list[i]);
	}

	/* Post a command and perform a chip operation */
	uint8_t payload[] = {
		GET_VDO.cmd,
		GET_VDO.len,
		GET_VDO.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		byte6,
		ITE_VDM_VID_L,
		ITE_VDM_VID_H,
		vdo_type & 0xff, /* bit map to enum vdo_type_t */
		(vdo_type >> 8) & 0xff, /* bit map to enum vdo_type_t */
	};

	/* Copy the list of VDO types being requested in the cmd message */
	//memcpy(&payload[5], vdo_req_list, vdo_req.num_vdos);

	return it52xx_post_command(dev, CMD_GET_VDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)vdo);
}

static int it52xx_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			  enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			  enum pdo_source_t source, uint32_t *pdos)
{
	struct pdc_data_t *data = dev->data;
	union get_pdos_t *get_pdo;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdos == NULL) {
		return -EINVAL;
	}

	/* b/366470065 - The vendor specific GET_PDO command fails to generate
	 * the appropriate PD message if the requested PDO type has not
	 * been received.
	 *
	 * Use the UCSI version which has the correct behavior.
	 */
	memset((uint8_t *)pdos, 0, sizeof(uint32_t) * num_pdos);

	uint8_t payload[] = {
		GET_PDOS.cmd,
		GET_PDOS.len,
		GET_PDOS.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	BUILD_ASSERT(ARRAY_SIZE(payload) == IT52XX_CONTROL_CMD_BASE_LENGTH);

	get_pdo = (union get_pdos_t *)&payload[4];
	get_pdo->connector_number = IT5271_CONNECTOR_NUMBER;
	get_pdo->pdo_source = source;
	get_pdo->pdo_offset = pdo_offset;
	get_pdo->number_of_pdos = num_pdos - 1;
	get_pdo->pdo_type = pdo_type;
	get_pdo->source_caps = CURRENT_SUPPORTED_SOURCE_CAPS;
	get_pdo->range = SPR_RANGE;

	return it52xx_post_command(dev, CMD_GET_PDOS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)pdos);
}

static int it52xx_set_rdo(const struct device *dev, uint32_t rdo)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) | ((IT52XX_VDM_VDC_SET_RDO & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_SET_RDO >> 1) & 0xf) | ((IT52XX_VDM_VDC_VER & 0xf) << 4);

	uint8_t payload[] = {
		SET_RDO.cmd,
		SET_RDO.len,
		SET_RDO.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		byte6,
		BYTE0(rdo),
		BYTE1(rdo),
		BYTE2(rdo),
		BYTE3(rdo),
	};

	return it52xx_post_command(dev, CMD_SET_RDO, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int it52xx_get_cable_property(const struct device *dev,
				    union cable_property_t *cp)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cp == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) ;

	uint8_t payload[] = {
		GET_CABLE_PROPERTY.cmd,
		GET_CABLE_PROPERTY.len,
		GET_CABLE_PROPERTY.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_CABLE_PROPERTY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)cp);
}

static int it52xx_get_rdo(const struct device *dev, uint32_t *rdo)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5, byte6 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (rdo == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, vdc_cmd 1'b */
	/* bit[24:31]: vdc_cmd 4'b, IT52XX_VDM_VDC_VER 4'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f) | ((IT52XX_VDM_VDC_GET_RDO & BIT(0)) << 7);
	byte6 = ((IT52XX_VDM_VDC_GET_RDO >> 1) & 0xf) | ((IT52XX_VDM_VDC_VER & 0xf) << 4);

	uint8_t payload[] = {
		GET_RDO.cmd,
		GET_RDO.len,
		GET_RDO.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		byte6,
		ITE_VDM_VID_L,
		ITE_VDM_VID_H,
		0x00, /* EC_PID_L --> it52xx don't care */
		0x00, /* EC_PID_H --> it52xx don't care */
	};

	return it52xx_post_command(dev, CMD_GET_RDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)rdo);
}

static int it52xx_connector_reset(const struct device *dev,
				 union connector_reset_t reset)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	reset.connector_number = IT5271_CONNECTOR_NUMBER;

	uint8_t payload[] = {
		SET_CONNECTOR_RESET.cmd,
		SET_CONNECTOR_RESET.len,
		SET_CONNECTOR_RESET.sub,
		0x00, /* Data Length --> set to 0x00 */
		reset.raw_value,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_CONNECTOR_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
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

	uint8_t payload[] = {
		GET_CAPABILITY.cmd,
		GET_CAPABILITY.len,
		GET_CAPABILITY.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_CAPABILITY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)caps);
}

static int it52xx_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (voltage == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_CONNECTOR_STATUS.cmd,
		GET_CONNECTOR_STATUS.len,
		GET_CONNECTOR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_VBUS_VOLTAGE, payload,
				  ARRAY_SIZE(payload), (uint8_t *)voltage);
}

static int it52xx_get_lpm_ppm_info(const struct device *dev,
				  struct lpm_ppm_info_t *info)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (info == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_LPM_PPM_INFO.cmd,
		GET_LPM_PPM_INFO.len,
		GET_LPM_PPM_INFO.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_LPM_PPM_INFO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)info);
}

static int it52xx_get_attention_vdo(const struct device *dev,
				   union get_attention_vdo_t *vdo)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte5 = 0;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL) {
		return -EINVAL;
	}

	/* bit[16:23]: connector num 7'b, reserved 1'b */
	byte5 = (IT5271_CONNECTOR_NUMBER & 0x7f);

	uint8_t payload[] = {
		GET_ATTENTION_VDO.cmd,
		GET_ATTENTION_VDO.len,
		GET_ATTENTION_VDO.sub,
		0x00, /* Data Length --> set to 0x00 */
		byte5,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_ATTENTION_VDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)vdo);
}

#if 0 //rtk only
/**
 * @param offset Starting location in PD Status information payload.
 *               Note that offset values refer to the payload data
 *               following the byte-count byte present in all response
 *               messages. For example, the 4 PD status bytes are at
 *               offset 0, not 1.
 */
static int rts54_get_rtk_status(const struct device *dev, uint8_t offset,
				uint8_t len, enum cmd_t cmd, uint8_t *buf)
{
	if (buf == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_RTK_STATUS.cmd, GET_RTK_STATUS.len, offset, 0x00, len,
	};

	return it52xx_post_command(dev, cmd, payload, ARRAY_SIZE(payload), buf);
}
#endif

#if 0 //optional api
static int it52xx_set_retimer_update_mode(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* 0: FW update starts, 1: FW update ends */
	enable = !enable;

	uint8_t payload[] = {
		SET_RETIMER_FW_UPDATE_MODE.cmd,
		SET_RETIMER_FW_UPDATE_MODE.len,
		SET_RETIMER_FW_UPDATE_MODE.sub,
		0x00,
		enable,
	};

	return it52xx_post_command(dev, CMD_SET_RETIMER_FW_UPDATE_MODE, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

#if 0 //optional api
static int it52xx_reconnect(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_TPC_RECONNECT.cmd,
		SET_TPC_RECONNECT.len,
		SET_TPC_RECONNECT.sub,
		0x00,
		0x01,
	};

	return it52xx_post_command(dev, CMD_SET_TPC_RECONNECT, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

#if 0 //optional api
static int it52xx_get_drp_mode(const struct device *dev, enum drp_mode_t *dm)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		GET_TPC_CSD_OPERATION_MODE.cmd,
		GET_TPC_CSD_OPERATION_MODE.len,
		GET_TPC_CSD_OPERATION_MODE.sub,
		0x00,
	};
	return it52xx_post_command(dev, CMD_GET_DRP_MODE, payload,
				  ARRAY_SIZE(payload), (uint8_t *)dm);
}
#endif

#if 0 //optional api
static int it52xx_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdo == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_CURRENT_PARTNER_SRC_PDO.cmd,
		GET_CURRENT_PARTNER_SRC_PDO.len,
		GET_CURRENT_PARTNER_SRC_PDO.sub,
		0x00,
	};

	return it52xx_post_command(dev, CMD_GET_CURRENT_PARTNER_SRC_PDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)pdo);
}
#endif

#if 0 //optional api
static int it52xx_get_identity_discovery(const struct device *dev,
					bool *disc_state)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (disc_state == NULL) {
		return -EINVAL;
	}

	return rts54_get_rtk_status(dev, 0, 14, CMD_GET_IDENTITY_DISCOVERY,
				    (uint8_t *)disc_state);
}
#endif

#if 0 //optional api
static int it52xx_is_vconn_sourcing(const struct device *dev,
				   bool *vconn_sourcing)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vconn_sourcing == NULL) {
		return -EINVAL;
	}

	return rts54_get_rtk_status(dev, 0, 11, CMD_GET_IS_VCONN_SOURCING,
				    (uint8_t *)vconn_sourcing);
}
#endif

#if 0 //optional api
static int it52xx_get_pch_data_status(const struct device *dev, uint8_t port_num,
				     uint8_t *status_reg)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (status_reg == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_PCH_DATA_STATUS.cmd,
		GET_PCH_DATA_STATUS.len,
		GET_PCH_DATA_STATUS.sub,
		port_num,
	};

	it52xx_post_command(dev, CMD_GET_PCH_DATA_STATUS, payload,
			   ARRAY_SIZE(payload), status_reg);
	return 0;
}
#endif

#if 1 //optional api
/* PPM directly call standard UCSI cmd to LPM (not through mgmt) */
static int it52xx_execute_ucsi_cmd(const struct device *dev,
				  uint8_t ucsi_command, uint8_t data_size,
				  uint8_t *command_specific,
				  uint8_t *lpm_data_out,
				  struct pdc_callback *callback)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	uint8_t cmd_buffer[SMBUS_MAX_BLOCK_SIZE];
	uint8_t conn = IT5271_CONNECTOR_NUMBER; /* 1:port=0, 2:port=1, ... */

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
		cmd_buffer[4] &= ~0x7F;
		cmd_buffer[4] |= (conn & 0x7F);
		break;
	case UCSI_GET_ALTERNATE_MODES:
		cmd_buffer[5] &= ~0x7F;
		cmd_buffer[5] |= (conn & 0x7F);
		break;
	default:
		LOG_ERR("ITE%d: Unknown ucsi sub-command", cfg->connector_number);
	}

	/* When PPM directly calls it52xx_execute_ucsi_cmd(), not through mgmt, then need callback */
	return it52xx_post_command_with_callback(dev, CMD_RAW_UCSI, cmd_buffer,
						data_size + 4, lpm_data_out,
						callback);
}
#endif

#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
#if 0 //optional api
static int it52xx_get_sbu_mux_mode(const struct device *dev,
				  enum pdc_sbu_mux_mode *mode)
{
	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (mode == NULL) {
		return -EINVAL;
	}

	/* SBU mux mode is encoded in an extension of the GET_IC_STATUS
	 * response. Read one byte starting at an offset of 38. */
	uint8_t payload[] = {
		GET_IC_STATUS.cmd,
		GET_IC_STATUS.len,
		/* Subtract one to account for leading length byte in response
		 */
		RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_OFFSET - 1,
		0x00,
		1,
	};

	return it52xx_post_command(dev, CMD_GET_SBU_MUX_MODE, payload,
				  ARRAY_SIZE(payload), (uint8_t *)mode);
}
#endif

#if 0 //optional api
static int it52xx_set_sbu_mux_mode(const struct device *dev,
				  enum pdc_sbu_mux_mode mode)
{
	struct pdc_data_t *data = dev->data;
	uint8_t setting;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
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

	uint8_t payload[] = { RTS_SET_SBU_MUX_MODE.cmd,
			      RTS_SET_SBU_MUX_MODE.len, setting };

	return it52xx_post_command(dev, CMD_SET_SBU_MUX_MODE, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif
#endif /* defined(CONFIG_USBC_PDC_DRIVEN_CCD) */

#if 0 //optional api
static int it52xx_set_bbr_cts(const struct device *dev, bool enable)
{
	const struct pdc_config_t *cfg = dev->config;

	struct pdc_data_t *data = dev->data;

	//not implemented yet
	return -EBUSY;

	/* Can only be called from Idle State */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_BBR_CTS.cmd,      SET_BBR_CTS.len,
		SET_BBR_CTS.sub,      0x00, /* Port */
		enable ? 0x01 : 0x00,
	};

	LOG_INF("ITE%d: SET_BBR_CTS = %d", cfg->connector_number, enable);

	return it52xx_post_command(dev, CMD_SET_BBR_CTS, payload,
				  ARRAY_SIZE(payload), NULL);
}
#endif

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
			      k_sleep(K_MSEC(T_GET_CCI_MSG)))) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int it52xx_set_handler_cb(const struct device *dev,
				struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/* call mgmt: pdc_cc_handler_cb() */
	data->cc_cb = callback;

	return 0;
}

static int it52xx_manage_callback(const struct device *dev,
				 struct pdc_callback *callback, bool set)
{
	struct pdc_data_t *const data = dev->data;

	/* call mgmt: pdc_ci_handler_cb() */
	return pdc_manage_callbacks(&data->ci_cb_list, callback, set);
}

static void it52xx_start_thread(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	k_thread_start(data->thread);
}

static bool it52xx_is_init_done(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	return data->init_done;
}

static DEVICE_API(pdc, pdc_driver_api) = {
	.start_thread = it52xx_start_thread,
	.is_init_done = it52xx_is_init_done,
	.get_ucsi_version = it52xx_get_ucsi_version,
	.reset = it52xx_pdc_reset, //do nothing or support by i2c cmd
	.connector_reset = it52xx_connector_reset,
	.get_capability = it52xx_get_capability, //verify by test code, mgmt never call this api
	.get_connector_capability = it52xx_get_connector_capability,
	.set_ccom = it52xx_set_ccom,
	//.set_drp_mode = it52xx_set_drp_mode, //optional api, it5271 default is trySRC DRP by build
	//.get_drp_mode = it52xx_get_drp_mode, //optional api
	.set_uor = it52xx_set_uor,
	.set_pdr = it52xx_set_pdr,
	.set_sink_path = it52xx_set_sink_path,
	.get_connector_status = it52xx_get_connector_status,
	.get_pdos = it52xx_get_pdos,
	.get_rdo = it52xx_get_rdo, //support by [CONTROL] VDM
	.set_rdo = it52xx_set_rdo, //support by [CONTROL] VDM
	.get_error_status = it52xx_get_error_status,
	.get_vbus_voltage = it52xx_get_vbus_voltage, //verify with CMD_PDC_READ_POWER_LEVEL
	//.get_current_pdo = it52xx_get_current_pdo, //optional api, = it52xx_get_pdos()
	.set_handler_cb = it52xx_set_handler_cb,
	.read_power_level = it52xx_read_power_level, //give fix parameter to verify (mgmt api not done)
	.get_info = it52xx_get_info, //support by [CONTROL] VDM
	.get_hw_config = it52xx_get_hw_config,
	.get_hw_frs_support = it52xx_get_hw_frs_support,
	.set_power_level = it52xx_set_power_level,
	//.reconnect = it52xx_reconnect, //optional api
	//.update_retimer = it52xx_set_retimer_update_mode, //optional api, != [CONTROL] set retimer mode?
	.get_cable_property = it52xx_get_cable_property,
	.get_vdo = it52xx_get_vdo, //support by [CONTROL] VDM
	//.get_identity_discovery = it52xx_get_identity_discovery, //optional api
	.set_comms_state = it52xx_set_comms_state,
	//.is_vconn_sourcing = it52xx_is_vconn_sourcing, //optional api
	.set_pdos = it52xx_set_pdo,
	//.get_pch_data_status = it52xx_get_pch_data_status, //optional api
	.execute_ucsi_cmd = it52xx_execute_ucsi_cmd, //optional api, PPM directly call standard UCSI cmd to LPM
	.manage_callback = it52xx_manage_callback,
	//.ack_cc_ci = it52xx_ack_cc_ci, //optional api, for compatible it5271, driver handle all ack_cc_ci reply
	.get_lpm_ppm_info = it52xx_get_lpm_ppm_info,
	//.set_frs = it52xx_set_frs, //optional api
	.get_attention_vdo = it52xx_get_attention_vdo,
	//.set_battery_capability = rts54_set_battery_capability, //optional api
	//.set_battery_status = rts54_set_battery_status, //optional api
#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
	//.get_sbu_mux_mode = it52xx_get_sbu_mux_mode, //optional api
	//.set_sbu_mux_mode = it52xx_set_sbu_mux_mode, //optional api, console command for forcing the PDC's SBU mux into the debug acc path (in debug mode can flash PDC FW by google servo board & console cmd)
#endif /* define(CONFIG_USBC_PDC_DRIVEN_CCD) */
	//.set_bbr_cts = it52xx_set_bbr_cts, //optional api
	//.set_ap_power_state = it52xx_set_ap_power_state, //optional api, what purpose?
	//.get_vendor_status = rts54_get_vendor_status, //optional api
	//.get_alert = rts54_get_alert, //optional api
};

static int pdc_init(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	int rv;
	bool irq_init_done = false;

	rv = i2c_is_ready_dt(&cfg->i2c);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	rv = gpio_is_ready_dt(&cfg->irq_gpios);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->irq_gpios.port->name);
		return -ENODEV;
	}

	k_event_init(&data->driver_event);

	for (int i = 0; i < ARRAY_SIZE(it52xx_irq_list); i++) {
		if (it52xx_irq_list[i].port == cfg->irq_gpios.port &&
		    it52xx_irq_list[i].pin == cfg->irq_gpios.pin) {
			irq_init_done = true;
			break;
		}

		if (it52xx_irq_list[i].port == NULL) {
			it52xx_irq_list[i] = cfg->irq_gpios;
			break;
		}
	}

	if (!irq_init_done) {
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
	}

	k_mutex_init(&data->mtx);

	data->cmd = CMD_NONE;
	data->error_recovery_counter = 0;
	data->init_retry_counter = 0;
	data->info.fw_version = PDC_FWVER_INVALID;

	/* Set initial state */
	data->init_local_state = INIT_PDC_SET_PPM_RESET;
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	LOG_INF("ITE%d: ITE it52xx PDC DRIVER", cfg->connector_number);

	return 0;
}

#if 0
static void tps_thread(void *dev, void *unused1, void *unused2)
{
	struct pdc_data_t *data = ((const struct device *)dev)->data;
	const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
	bool irq_pending_for_idle = false;

	while (1) {
		smf_run_state(SMF_CTX(data));

		/* Wait for event to handle */
		data->events = k_event_wait(&data->pdc_event, PDC_ALL_EVENTS,
					    false, K_FOREVER);
		LOG_INF("tps_thread[%d][%s]: events=0x%X",
			cfg->connector_number, state_names[get_state(data)],
			data->events);

		k_event_clear(&data->pdc_event, PDC_INTERNAL_EVENT);

		if (data->events & PDC_IRQ_EVENT) {
			k_event_clear(&data->pdc_event, PDC_IRQ_EVENT);

			if (!check_comms_suspended()) {
				irq_pending_for_idle = true;
			}
		}

		/* We only handle IRQs on idle. */
		if (get_state(data) == ST_IDLE && irq_pending_for_idle) {
			if (handle_irqs(data) < 0) {
				k_work_reschedule(
					&data->delayed_post,
					K_MSEC(PDC_HANDLE_IRQ_RETRY_DELAY));
			} else {
				irq_pending_for_idle = false;
			}
		}
	}
}
#endif

static void it52xx_thread(void *dev, void *unused1, void *unused2)
{
	const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
	struct pdc_data_t *data = ((const struct device *)dev)->data;
	uint32_t events;
	bool irq_pending_for_idle = false;

	while (1) {
		smf_run_state(SMF_CTX(data));

		events = k_event_wait(&data->driver_event,
				      IT52XX_IRQ_EVENT |
					      IT52XX_NEXT_STATE_READY,
				      false, K_MSEC(T_GET_CCI_MSG)); /* TODO: need others event? */
								     /* TODO: T_GET_CCI_MSG or K_FOREVER? */

		if (events & IT52XX_IRQ_EVENT) {
			irq_pending_for_idle = true;
		}

		k_event_clear(&data->driver_event, events);

		/* We only handle irq on idle or task wait (transferring msg). */
		if (((get_state(data) == ST_IDLE) || (get_state(data) == ST_TASK_WAIT)) &&
		     irq_pending_for_idle) {
			irq_pending_for_idle = false;
			if (check_comms_suspended()) {
				LOG_INF("ITE%d: Ignoring interrupt",
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

/* .bits.command_completed = 1,       ti doesn't support */
/* .bits.attention = 1,               ti doesn't support */

#define PDC_DATA_PTR_ENTRY(inst) &PDC_DATA_STRUCT_NAME(inst),

/* Populate the pdc_data struct with a pointer to each ITE PDC port's device
 * struct. */
static struct pdc_data_t *const pdc_data[] = { DT_INST_FOREACH_STATUS_OKAY(
	PDC_DATA_PTR_ENTRY) };

#ifdef CONFIG_ZTEST

/*
 * Wait for drivers to become idle.
 */
/* LCOV_EXCL_START */
bool pdc_it52xx_test_idle_wait(void)
{
	int num_finished;

	/* Wait for up to 20 * 100ms for all drivers to become idle. */
	for (int i = 0; i < 20; i++) {
		num_finished = 0;

		k_msleep(100);
		for (int port = 0; port < ARRAY_SIZE(pdc_data); port++) {
			if (!device_is_ready(pdc_data[port]->dev)) {
				/* This port is not in use. Consider it finished
				 * so we do not wait on it. */
				num_finished++;
			}
			if (get_state(pdc_data[port]) == ST_IDLE &&
			    pdc_data[port]->cmd == CMD_NONE) {
				/* Driver is in the idle state with no pending
				 * commands. */
				num_finished++;
			}
		}

		if (num_finished == ARRAY_SIZE(pdc_data)) {
			return true;
		}
	}

	return false;
}
/* LCOV_EXCL_STOP */

#endif
