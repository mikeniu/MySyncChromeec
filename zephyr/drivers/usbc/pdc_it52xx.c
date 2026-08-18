/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE IT52XX Power Delivery Controller driver. */

#include "drivers/ucsi_v3.h"
#include "pdc_it52xx.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/sys/slist.h>

#include <drivers/pdc.h>

#define DT_DRV_COMPAT ite_it52xx_pdc

/** IT52XX [CONTROL] command byte count. */
#define IT52XX_CONTROL_BYTE_CNT sizeof(struct ucsi_control_t)

BUILD_ASSERT(IT52XX_CONTROL_BYTE_CNT == 0x8);

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
	uint8_t byte_count;
	/*
	 * UCSI command (ex. GET_CONNECTOR_STATUS), data length,
	 * and command-specific fields.
	 */
	struct ucsi_control_t control;
} __packed;

BUILD_ASSERT(sizeof(struct it52xx_control_cmd) == 10);

/** Build a CONTROL packet with all other UCSI fields cleared. */
static inline __unused struct it52xx_control_cmd
it52xx_make_control_cmd(enum ucsi_command_t command)
{
	return (struct it52xx_control_cmd){
		.ctrl_cmd = 0x81,
		.byte_count = IT52XX_CONTROL_BYTE_CNT,
		.control = {
			.command = command,
		},
	};
}

/** ITE SMBus commands. */
static const struct smbus_cmd_t __unused GET_ALERT_STATUS = { 0xBD, 0x01 };
static const struct smbus_cmd_t __unused CLR_ALERT_STATUS = { 0xBC };
static const struct smbus_cmd_t __unused SET_CONTROL = { 0x81 };
static const struct smbus_cmd_t __unused SET_MSGOUT = { 0x83 };
static const struct smbus_cmd_t __unused GET_CCI = { 0x80 };
static const struct smbus_cmd_t __unused GET_MSGIN = { 0x82 };

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

/** Internal driver operation IDs, not UCSI command opcodes. */
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

static int it52xx_pdc_reset(const struct device *dev)
{
	return -ENOSYS;
}

static int it52xx_get_info(const struct device *dev, struct pdc_info_t *info,
			   bool live)
{
	return -ENOSYS;
}

static int it52xx_get_error_status(const struct device *dev,
				   union error_status_t *es)
{
	return -ENOSYS;
}

static int it52xx_get_connector_status(const struct device *dev,
				       union connector_status_t *cs)
{
	return -ENOSYS;
}

static int it52xx_set_sink_path(const struct device *dev, bool en)
{
	return -ENOSYS;
}

static int it52xx_read_power_level(const struct device *dev)
{
	return -ENOSYS;
}

static int it52xx_get_connector_capability(const struct device *dev,
					   union connector_capability_t *caps)
{
	return -ENOSYS;
}

static int it52xx_set_uor(const struct device *dev, union uor_t uor)
{
	return -ENOSYS;
}

static int it52xx_set_pdr(const struct device *dev, union pdr_t pdr)
{
	return -ENOSYS;
}

static int it52xx_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			  uint8_t *vdo_req_list, uint32_t *vdo)
{
	return -ENOSYS;
}

static int it52xx_set_rdo(const struct device *dev, uint32_t rdo)
{
	return -ENOSYS;
}

static int it52xx_get_cable_property(const struct device *dev,
				     union cable_property_t *cp)
{
	return -ENOSYS;
}

static int it52xx_get_rdo(const struct device *dev, uint32_t *rdo)
{
	return -ENOSYS;
}

static int it52xx_connector_reset(const struct device *dev,
				  union connector_reset_t reset)
{
	return -ENOSYS;
}

static int it52xx_get_capability(const struct device *dev,
				 struct capability_t *caps)
{
	return -ENOSYS;
}

static int it52xx_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	return -ENOSYS;
}

static int it52xx_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	return -ENOSYS;
}

static int it52xx_get_hw_config(const struct device *dev,
				struct pdc_hw_config_t *config)
{
	return -ENOSYS;
}

static bool it52xx_get_hw_frs_support(const struct device *dev)
{
	return false;
}

static int it52xx_set_comms_state(const struct device *dev, bool comms_active)
{
	return -ENOSYS;
}

static int it52xx_set_handler_cb(const struct device *dev,
				 struct pdc_callback *callback)
{
	return -ENOSYS;
}

static void it52xx_start_thread(const struct device *dev)
{
}

static enum pdc_driver_init_state_t
it52xx_is_init_done(const struct device *dev)
{
	return PDC_DRIVER_INIT_STATE_WAIT;
}

static DEVICE_API(pdc, pdc_driver_api) = {
	.start_thread = it52xx_start_thread,
	.is_init_done = it52xx_is_init_done,
	.get_ucsi_version = it52xx_get_ucsi_version,
	.reset = it52xx_pdc_reset,
	.connector_reset = it52xx_connector_reset,
	.get_capability = it52xx_get_capability,
	.get_connector_capability = it52xx_get_connector_capability,
	.set_uor = it52xx_set_uor,
	.set_pdr = it52xx_set_pdr,
	.set_sink_path = it52xx_set_sink_path,
	.get_connector_status = it52xx_get_connector_status,
	.get_rdo = it52xx_get_rdo,
	.set_rdo = it52xx_set_rdo,
	.get_error_status = it52xx_get_error_status,
	.get_vbus_voltage = it52xx_get_vbus_voltage,
	.set_handler_cb = it52xx_set_handler_cb,
	.read_power_level = it52xx_read_power_level,
	.get_info = it52xx_get_info,
	.get_hw_config = it52xx_get_hw_config,
	.get_hw_frs_support = it52xx_get_hw_frs_support,
	.get_cable_property = it52xx_get_cable_property,
	.get_vdo = it52xx_get_vdo,
	.set_comms_state = it52xx_set_comms_state,
};

static int pdc_init(const struct device *dev)
{
	return 0;
}

#define IT52XX_PDC_DEFINE(inst)                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL, NULL, NULL, POST_KERNEL, \
			      CONFIG_APPLICATION_INIT_PRIORITY,              \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IT52XX_PDC_DEFINE)
