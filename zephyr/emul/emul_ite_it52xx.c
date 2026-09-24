/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE IT52XX Power Delivery Controller emulator. */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_ite_it52xx.h"
#include "emul/emul_pdc.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT ite_it52xx_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(ite_it52xx_emul);

/* Test-only driver helper, matching the RTK/TI emulator cache-reset behavior.
 */
void pdc_it52xx_test_invalidate_chip_info(const struct device *dev);

/* IT52XX SMBus commands */
#define IT52XX_CMD_CCI 0x80
#define IT52XX_CMD_CONTROL 0x81
/* Message response from the IT52XX emulator to the EC */
#define IT52XX_CMD_MSGIN 0x82
#define IT52XX_CMD_CLEAR_ALERT 0xbc
#define IT52XX_CMD_GET_ALERT 0xbd

/* Include SMBus command, byte count, UCSI command, UCSI data length */
#define IT52XX_CONTROL_MIN_LENGTH 4
/* Include IT52XX_CONTROL_MIN_LENGTH + UCSI command-specific max 6 bytes */
#define IT52XX_CONTROL_MAX_LENGTH 10
/* Include a one-byte count and four bytes of data */
#define IT52XX_CCI_LENGTH 5
#define IT52XX_GET_ERROR_STATUS_LENGTH 16
#define IT52XX_GET_PDC_INFO_LENGTH 29
#define IT52XX_ALERT_UCSI BIT(1)

/* UCSI commands supported by the emulator. */
#define IT52XX_UCSI_PPM_RESET 0x01
#define IT52XX_UCSI_ACK_CC_CI 0x04
#define IT52XX_UCSI_SET_NOTIFICATION_ENABLE 0x05
#define IT52XX_UCSI_GET_CONNECTOR_STATUS 0x12
#define IT52XX_UCSI_GET_ERROR_STATUS 0x13
#define IT52XX_UCSI_VENDOR_DEFINED 0x20

#define IT52XX_VENDOR_ID 0x048d
#define IT52XX_NOTIFICATION_ENABLE_MASK GENMASK(16, 0)

/* States of the emulated IT5271 LPM UCSI command interface */
enum it52xx_emul_lpm_state {
	/* State before notification enabled */
	IT52XX_EMUL_LPM_IDLE_0,
	/* State after notification enabled */
	IT52XX_EMUL_LPM_IDLE_1,
	IT52XX_EMUL_LPM_WAIT_CMD_ACK,
	IT52XX_EMUL_LPM_WAIT_ASYNC_EVENT_ACK,
};

struct it52xx_emul_data {
	struct i2c_common_emul_data common;
	struct gpio_dt_spec irq_gpio;

	uint8_t request[PDC_MAX_DATA_LENGTH];
	size_t request_length;
	uint8_t alert_status;
	uint8_t cci[IT52XX_CCI_LENGTH];
	uint8_t msgin[IT52XX_GET_PDC_INFO_LENGTH + 1];
	size_t msgin_length;

	enum it52xx_emul_lpm_state lpm_state;
	bool ppm_reset_seen;
	bool notification_enable_seen;
	bool get_pdc_info_seen;
	union notification_enable_t notification_enable;
	union connector_status_t connector_status;
	union error_status_t error_status;
	struct pdc_info_t info;
};

static int it52xx_emul_process_control(struct it52xx_emul_data *data);

static struct it52xx_emul_data *it52xx_emul_get_data(const struct emul *emul)
{
	return emul->data;
}

static int it52xx_emul_set_irq(struct it52xx_emul_data *data, bool asserted)
{
	return gpio_emul_input_set(data->irq_gpio.port, data->irq_gpio.pin,
				   asserted ? 0 : 1);
}

/**
 * @brief Handle I2C start write message.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg First byte of write message, usually selected command
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int it52xx_emul_start_write(const struct emul *emul, int reg)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	memset(data->request, 0, sizeof(data->request));
	data->request[0] = reg;
	data->request_length = 1;

	LOG_DBG("start_write cmd=%d", reg);

	return 0;
}

/**
 * @brief Function called for each byte of write message which is saved in
 *        data->request
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg First byte of write message, usually selected command
 * @param value Received value of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int it52xx_emul_write_byte(const struct emul *emul, int reg,
				  uint8_t value, int bytes)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (bytes < 0 || (size_t)bytes >= ARRAY_SIZE(data->request)) {
		LOG_ERR("write byte invalid bytes %d", bytes);
		return -EINVAL;
	}

	data->request[bytes] = value;
	data->request_length = bytes + 1;

	LOG_DBG("write_byte reg=%d, value=0x%X, bytes=%d", reg, value, bytes);

	return 0;
}

/**
 * @brief Function which finalize write messages.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg First byte of write message, usually selected command
 * @param bytes Number of bytes received in data->request
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int it52xx_emul_finish_write(const struct emul *emul, int reg, int bytes)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	LOG_DBG("finish_write reg=%d, bytes=%d", reg, bytes);

	switch (reg) {
	case IT52XX_CMD_CONTROL:
		return it52xx_emul_process_control(data);
	case IT52XX_CMD_CLEAR_ALERT:
		if (data->request_length != 2) {
			return -EINVAL;
		}
		/* Clear alert status */
		data->alert_status &= ~data->request[1];
		/* If no alerts remain, set the active-low IRQ to high. */
		if (data->alert_status == 0) {
			return it52xx_emul_set_irq(data, false);
		}
		return 0;
	case IT52XX_CMD_CCI:
	case IT52XX_CMD_MSGIN:
	case IT52XX_CMD_GET_ALERT:
		/* These writes only select the following read command. */
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * @brief Validate the register selected for an I2C read.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg Register selected by the preceding I2C write
 *
 * @return 0 on success
 * @return -EINVAL if the selected register is not readable
 */
static int it52xx_emul_start_read(const struct emul *emul, int reg)
{
	ARG_UNUSED(emul);

	if (reg != IT52XX_CMD_CCI && reg != IT52XX_CMD_MSGIN &&
	    reg != IT52XX_CMD_GET_ALERT) {
		LOG_ERR("start read invalid reg 0x%02x", reg);
		return -EINVAL;
	}

	LOG_DBG("start_read reg=0x%X", reg);

	return 0;
}

/**
 * @brief Return one byte from the selected IT52XX response buffer.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg Register selected by the preceding I2C write
 * @param value Pointer where the response byte is stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EINVAL on an invalid register or byte offset
 */
static int it52xx_emul_read_byte(const struct emul *emul, int reg,
				 uint8_t *value, int bytes)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (bytes < 0) {
		return -EINVAL;
	}

	switch (reg) {
	case IT52XX_CMD_GET_ALERT:
		if (bytes != 0) {
			return -EINVAL;
		}
		*value = data->alert_status;
		return 0;
	case IT52XX_CMD_CCI:
		if ((size_t)bytes >= ARRAY_SIZE(data->cci)) {
			return -EINVAL;
		}
		*value = data->cci[bytes];
		return 0;
	case IT52XX_CMD_MSGIN:
		if ((size_t)bytes >= data->msgin_length) {
			return -EINVAL;
		}
		*value = data->msgin[bytes];
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * @brief Finish an I2C read message.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg Register selected by the preceding I2C write
 * @param bytes Number of bytes returned in the I2C read message
 *
 * @return 0 on success
 */
static int it52xx_emul_finish_read(const struct emul *emul, int reg, int bytes)
{
	ARG_UNUSED(emul);
	ARG_UNUSED(reg);
	ARG_UNUSED(bytes);

	return 0;
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        command.
 *
 * @param emul Pointer to IT52XX emulator
 * @param reg First byte of last write message, usually selected command
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int it52xx_emul_access_reg(const struct emul *emul, int reg, int bytes,
				  bool read)
{
	ARG_UNUSED(emul);
	ARG_UNUSED(bytes);
	ARG_UNUSED(read);

	return reg;
}

static int it52xx_emul_set_cci(struct it52xx_emul_data *data,
			       union cci_event_t cci, const uint8_t *payload,
			       size_t payload_length)
{
	if (payload_length > IT52XX_GET_PDC_INFO_LENGTH) {
		return -EINVAL;
	}

	/* Fillup [CCI] messages */
	memset(data->cci, 0, sizeof(data->cci));
	data->cci[0] = IT52XX_CCI_LENGTH - 1;
	cci.data_len = payload_length;
	sys_put_le32(cci.raw_value, &data->cci[1]);

	/* Fillup [MSGIN] messages */
	memset(data->msgin, 0, sizeof(data->msgin));
	data->msgin[0] = payload_length;
	if (payload_length > 0) {
		memcpy(&data->msgin[1], payload, payload_length);
	}
	data->msgin_length = payload_length + 1;

	/* Assert the UCSI alert to notify the EC */
	data->alert_status |= IT52XX_ALERT_UCSI;
	return it52xx_emul_set_irq(data, true);
}

static int it52xx_emul_send_command_result(struct it52xx_emul_data *data,
					   union cci_event_t cci,
					   const uint8_t *payload,
					   size_t payload_length)
{
	enum it52xx_emul_lpm_state previous_state = data->lpm_state;
	int rv;

	data->lpm_state = IT52XX_EMUL_LPM_WAIT_CMD_ACK;
	rv = it52xx_emul_set_cci(data, cci, payload, payload_length);
	if (rv != 0) {
		data->lpm_state = previous_state;
	}

	return rv;
}

static int it52xx_emul_ppm_reset(struct it52xx_emul_data *data)
{
	union cci_event_t cci = { .reset_completed = 1 };

	data->notification_enable.raw_value = 0;
	data->error_status.raw_value = 0;
	data->lpm_state = IT52XX_EMUL_LPM_IDLE_0;
	data->ppm_reset_seen = true;
	data->notification_enable_seen = false;
	data->get_pdc_info_seen = false;

	return it52xx_emul_set_cci(data, cci, NULL, 0);
}

static int it52xx_emul_set_notification_enable(struct it52xx_emul_data *data)
{
	union cci_event_t cci = { .command_completed = 1 };
	int rv;

	/* IT52XX implements the UCSI 2.1/3.0 notification bits 0 through 16. */
	data->notification_enable.raw_value = data->request[4] |
					      (data->request[5] << 8) |
					      (data->request[6] << 16);
	data->notification_enable.raw_value &= IT52XX_NOTIFICATION_ENABLE_MASK;
	if (data->notification_enable.raw_value != 0) {
		data->notification_enable.command_completed = 1;
	}

	rv = it52xx_emul_send_command_result(data, cci, NULL, 0);
	if (rv == 0) {
		data->notification_enable_seen = true;
	}

	return rv;
}

static void it52xx_emul_build_info_response(struct it52xx_emul_data *data,
					    uint8_t *response)
{
	response[0] = data->info.vid;
	response[1] = data->info.vid >> 8;
	response[2] = data->info.pid;
	response[3] = data->info.pid >> 8;

	/* The IT52XX response carries firmware version as little endian. */
	response[4] = PDC_FWVER_GET_MINOR(data->info.fw_version);
	response[5] = PDC_FWVER_GET_MAJOR(data->info.fw_version);
	response[6] = PDC_FWVER_GET_PATCH(data->info.fw_version);
	response[7] = 0;

	response[8] = data->info.pd_revision;
	response[9] = data->info.pd_revision >> 8;
	response[10] = data->info.pd_version;
	response[11] = data->info.pd_version >> 8;
	response[12] = data->info.is_running_flash_code;
	response[13] = data->info.running_in_flash_bank;
	memcpy(&response[14], data->info.project_name,
	       sizeof(data->info.project_name));
	response[27] = data->info.extra;
	response[28] = data->info.extra >> 8;
}

static int it52xx_emul_get_pdc_info(struct it52xx_emul_data *data)
{
	union cci_event_t cci = {
		.vendor_defined_indicator = 1,
		.command_completed = 1,
	};
	uint8_t response[IT52XX_GET_PDC_INFO_LENGTH] = { 0 };
	uint8_t connector = data->request[4] & 0x7f;
	uint8_t vendor_command = ((data->request[5] & 0x0f) << 1) |
				 (data->request[4] >> 7);
	int rv;

	/* IT5271 accepts only connector number 1 */
	if (connector != 1) {
		union cci_event_t error_cci = {
			.error = 1,
			.command_completed = 1,
		};
		union error_status_t error = {
			.non_existent_connector_number = 1,
		};

		/* IT5271 replaces the stored error when a command reports an
		 * error. */
		data->error_status = error;
		return it52xx_emul_send_command_result(data, error_cci, NULL,
						       0);
	}

	/* IT5271 supports only vendor command 0 */
	if (vendor_command != 0) {
		union cci_event_t error_cci = {
			.error = 1,
			.command_completed = 1,
		};
		union error_status_t error = {
			.invalid_command_specific_param = 1,
		};

		/* IT5271 replaces the stored error when a command reports an
		 * error. */
		data->error_status = error;
		return it52xx_emul_send_command_result(data, error_cci, NULL,
						       0);
	}

	it52xx_emul_build_info_response(data, response);
	rv = it52xx_emul_send_command_result(data, cci, response,
					     sizeof(response));
	if (rv == 0) {
		data->get_pdc_info_seen = true;
	}

	return rv;
}

static int it52xx_emul_get_connector_status(struct it52xx_emul_data *data)
{
	union cci_event_t cci = { .command_completed = 1 };
	int rv;

	/* IT5271 accepts only connector number 1 */
	if ((data->request[4] & 0x7f) != 1) {
		union cci_event_t error_cci = {
			.error = 1,
			.command_completed = 1,
		};
		union error_status_t error = {
			.non_existent_connector_number = 1,
		};

		/* IT5271 replaces the stored error when a command reports an
		 * error. */
		data->error_status = error;
		return it52xx_emul_send_command_result(data, error_cci, NULL,
						       0);
	}

	rv = it52xx_emul_send_command_result(
		data, cci, data->connector_status.raw_value,
		sizeof(data->connector_status.raw_value));
	if (rv == 0) {
		/*
		 * IT5271 clears connector-status change bits while processing
		 * the command.
		 */
		data->connector_status.raw_conn_status_change_bits = 0;
	}

	return rv;
}

static int it52xx_emul_get_error_status(struct it52xx_emul_data *data)
{
	union cci_event_t cci = { .command_completed = 1 };
	uint8_t response[IT52XX_GET_ERROR_STATUS_LENGTH] = { 0 };
	uint16_t error_status;
	int rv;

	/* IT5271 accepts only connector number 1 */
	if ((data->request[4] & 0x7f) != 1) {
		union cci_event_t error_cci = {
			.error = 1,
			.command_completed = 1,
		};
		union error_status_t error = {
			.non_existent_connector_number = 1,
		};

		/* IT5271 replaces the stored error when a command reports an
		 * error. */
		data->error_status = error;
		return it52xx_emul_send_command_result(data, error_cci, NULL,
						       0);
	}

	error_status = data->error_status.raw_value & GENMASK(14, 0);
	/* IT5271 supports error-status from bit0 to bit14. */
	response[0] = error_status;
	response[1] = error_status >> 8;

	rv = it52xx_emul_send_command_result(data, cci, response,
					     sizeof(response));
	if (rv == 0) {
		/* IT5271 clears error information while processing command */
		data->error_status.raw_value = 0;
	}

	return rv;
}

static int it52xx_emul_ack_cc_ci(struct it52xx_emul_data *data)
{
	union cci_event_t cci = { .acknowledge_command = 1 };
	enum it52xx_emul_lpm_state previous_state = data->lpm_state;
	int rv;

	if (data->lpm_state != IT52XX_EMUL_LPM_WAIT_CMD_ACK &&
	    data->lpm_state != IT52XX_EMUL_LPM_WAIT_ASYNC_EVENT_ACK &&
	    data->lpm_state != IT52XX_EMUL_LPM_IDLE_1) {
		return -EINVAL;
	}

	data->lpm_state = IT52XX_EMUL_LPM_IDLE_1;
	rv = it52xx_emul_set_cci(data, cci, NULL, 0);
	if (rv != 0) {
		data->lpm_state = previous_state;
	}

	return rv;
}

static int it52xx_emul_process_idle_1(struct it52xx_emul_data *data,
				      uint8_t command)
{
	switch (command) {
	case IT52XX_UCSI_SET_NOTIFICATION_ENABLE:
		return it52xx_emul_set_notification_enable(data);
	case IT52XX_UCSI_ACK_CC_CI:
		return it52xx_emul_ack_cc_ci(data);
	case IT52XX_UCSI_GET_CONNECTOR_STATUS:
		return it52xx_emul_get_connector_status(data);
	case IT52XX_UCSI_GET_ERROR_STATUS:
		return it52xx_emul_get_error_status(data);
	case IT52XX_UCSI_VENDOR_DEFINED:
		return it52xx_emul_get_pdc_info(data);
	default: {
		union cci_event_t cci = {
			.not_supported = 1,
			.error = 1,
			.command_completed = 1,
		};
		union error_status_t error = {
			.unrecognized_command = 1,
		};

		/* IT5271 replaces the stored error when a command reports an
		 * error. */
		data->error_status = error;
		return it52xx_emul_send_command_result(data, cci, NULL, 0);
	}
	}
}

static int it52xx_emul_process_control(struct it52xx_emul_data *data)
{
	/* Validate the [CTRL] message length and fields */
	if (data->request_length < IT52XX_CONTROL_MIN_LENGTH ||
	    data->request_length > IT52XX_CONTROL_MAX_LENGTH ||
	    data->request[1] != data->request_length - 2 ||
	    data->request[3] != 0) {
		return -EINVAL;
	}

	uint8_t command = data->request[2];

	/* IT5271 accepts PPM_RESET in every PPM state. */
	if (command == IT52XX_UCSI_PPM_RESET) {
		return it52xx_emul_ppm_reset(data);
	}

	switch (data->lpm_state) {
	case IT52XX_EMUL_LPM_IDLE_0:
		if (command == IT52XX_UCSI_SET_NOTIFICATION_ENABLE) {
			return it52xx_emul_set_notification_enable(data);
		}
		/* IT5271 ignores other commands until notifications are set. */
		return 0;
	case IT52XX_EMUL_LPM_IDLE_1:
		return it52xx_emul_process_idle_1(data, command);
	case IT52XX_EMUL_LPM_WAIT_CMD_ACK:
	case IT52XX_EMUL_LPM_WAIT_ASYNC_EVENT_ACK:
		if (command == IT52XX_UCSI_ACK_CC_CI) {
			return it52xx_emul_ack_cc_ci(data);
		}
		/* IT5271 waits for ACK_CC_CI before accepting another command.
		 */
		return 0;
	}

	return -EINVAL;
}

/* API functions */
static void it52xx_emul_reset_data(struct it52xx_emul_data *data)
{
	data->request_length = 0;
	data->alert_status = 0;
	memset(data->cci, 0, sizeof(data->cci));
	memset(data->msgin, 0, sizeof(data->msgin));
	data->msgin_length = 0;
	data->notification_enable.raw_value = 0;
	memset(&data->connector_status, 0, sizeof(data->connector_status));
	data->error_status.raw_value = 0;
	data->lpm_state = IT52XX_EMUL_LPM_IDLE_0;
	data->ppm_reset_seen = false;
	data->notification_enable_seen = false;
	data->get_pdc_info_seen = false;
}

static int emul_it52xx_reset(const struct emul *emul)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	it52xx_emul_reset_data(data);

	return it52xx_emul_set_irq(data, false);
}

static int emul_it52xx_set_connector_status(
	const struct emul *emul,
	const union connector_status_t *connector_status)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (connector_status == NULL) {
		return -EINVAL;
	}

	i2c_common_emul_lock_data(&data->common, K_FOREVER);
	data->connector_status = *connector_status;
	i2c_common_emul_unlock_data(&data->common);

	return 0;
}

static int
emul_it52xx_set_error_status(const struct emul *emul,
			     const union error_status_t *error_status)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (error_status == NULL) {
		return -EINVAL;
	}

	i2c_common_emul_lock_data(&data->common, K_FOREVER);
	data->error_status = *error_status;
	i2c_common_emul_unlock_data(&data->common);

	return 0;
}

static int emul_it52xx_set_info(const struct emul *emul,
				const struct pdc_info_t *info)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (info == NULL) {
		/* NULL invalidates the driver cache, not the emulated PDC info.
		 */
		pdc_it52xx_test_invalidate_chip_info(emul->dev);
		return 0;
	}

	data->info = *info;
	return 0;
}

static int emul_it52xx_idle_wait(const struct emul *emul)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	for (int i = 0; i < 200; ++i) {
		bool emulator_idle;

		i2c_common_emul_lock_data(&data->common, K_FOREVER);
		emulator_idle = data->lpm_state == IT52XX_EMUL_LPM_IDLE_1 &&
				data->alert_status == 0;
		i2c_common_emul_unlock_data(&data->common);

		if (emulator_idle) {
			return 0;
		}

		k_msleep(10);
	}

	return -ETIMEDOUT;
}

/* Unit test functions */
bool emul_it52xx_init_complete(const struct emul *emul)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);
	bool complete;

	i2c_common_emul_lock_data(&data->common, K_FOREVER);
	/* Check whether the driver completed the initialization flow */
	complete = data->lpm_state == IT52XX_EMUL_LPM_IDLE_1 &&
		   data->ppm_reset_seen && data->notification_enable_seen &&
		   data->get_pdc_info_seen;
	i2c_common_emul_unlock_data(&data->common);

	return complete;
}

int emul_it52xx_get_notification_enable(
	const struct emul *emul, union notification_enable_t *notifications)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	if (notifications == NULL) {
		return -EINVAL;
	}

	i2c_common_emul_lock_data(&data->common, K_FOREVER);
	/* Return the notification mask received during initialization */
	*notifications = data->notification_enable;
	i2c_common_emul_unlock_data(&data->common);

	return 0;
}

/* Device instantiation */

/**
 * @brief Set up a new IT52XX emulator
 *
 * This is called by the emulator framework for each IT52XX instance. It
 * initializes the common I2C emulator data.
 *
 * @param emul Emulation information
 * @param parent Parent I2C controller
 *
 * @return 0 indicating success (always)
 */
static int it52xx_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	data->info = (struct pdc_info_t){
		.fw_version = 0x010200,
		.pd_revision = 0x0302,
		.pd_version = 0x0101,
		.vid = IT52XX_VENDOR_ID,
		.pid = 0x5271,
		.is_running_flash_code = 1,
		.running_in_flash_bank = 1,
		.project_name = "GOOGLE",
	};

	/*
	 * The PDC driver has not configured the IRQ GPIO as an input yet.
	 * Calling gpio_emul_input_set() here would return -EINVAL. The IRQ GPIO
	 * is pulled high when the PDC driver initializes it.
	 */
	it52xx_emul_reset_data(data);

	return 0;
}

static DEVICE_API(emul_pdc, emul_it52xx_api) = {
	.reset = emul_it52xx_reset,
	.set_connector_status = emul_it52xx_set_connector_status,
	.set_error_status = emul_it52xx_set_error_status,
	.set_info = emul_it52xx_set_info,
	.idle_wait = emul_it52xx_idle_wait,
};

#define IT52XX_EMUL_DEFINE(inst)                                              \
	static struct it52xx_emul_data it52xx_emul_data_##inst = {         \
		.common = {                                                  \
			.start_write = it52xx_emul_start_write,                \
			.write_byte = it52xx_emul_write_byte,                  \
			.finish_write = it52xx_emul_finish_write,              \
			.start_read = it52xx_emul_start_read,                  \
			.read_byte = it52xx_emul_read_byte,                    \
			.finish_read = it52xx_emul_finish_read,                \
			.access_reg = it52xx_emul_access_reg,                  \
		},                                                             \
		.irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
	};  \
	static const struct i2c_common_emul_cfg it52xx_emul_cfg_##inst = {    \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(inst)),            \
		.data = &it52xx_emul_data_##inst.common,                      \
		.addr = DT_INST_REG_ADDR(inst),                               \
	};                                                                    \
	EMUL_DT_INST_DEFINE(inst, it52xx_emul_init, &it52xx_emul_data_##inst, \
			    &it52xx_emul_cfg_##inst, &i2c_common_emul_api,    \
			    &emul_it52xx_api)

DT_INST_FOREACH_STATUS_OKAY(IT52XX_EMUL_DEFINE)

struct i2c_common_emul_data *
emul_it52xx_get_i2c_common_data(const struct emul *emul)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	return &data->common;
}
