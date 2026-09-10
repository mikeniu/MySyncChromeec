/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE IT52XX Power Delivery Controller emulator. */

#include "drivers/pdc.h"
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
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT ite_it52xx_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(ite_it52xx_emul);

#define IT52XX_CMD_CCI 0x80
#define IT52XX_CMD_CONTROL 0x81
/* Message response from the IT52XX emulator to the EC */
#define IT52XX_CMD_MSGIN 0x82
#define IT52XX_CMD_CLEAR_ALERT 0xbc
#define IT52XX_CMD_GET_ALERT 0xbd

#define IT52XX_CCI_LENGTH 5

struct it52xx_emul_data {
	struct i2c_common_emul_data common;
	struct gpio_dt_spec irq_gpio;
	uint8_t request[PDC_MAX_DATA_LENGTH];
	size_t request_length;
	uint8_t alert_status;
	uint8_t cci[IT52XX_CCI_LENGTH];
	uint8_t msgin[PDC_MAX_DATA_LENGTH];
	size_t msgin_length;
};

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
		/* Command handling is added with individual command support. */
		return 0;
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

/* API functions */
static void it52xx_emul_reset_data(struct it52xx_emul_data *data)
{
	data->request_length = 0;
	data->alert_status = 0;
	memset(data->cci, 0, sizeof(data->cci));
	memset(data->msgin, 0, sizeof(data->msgin));
	data->msgin_length = 0;
}

static int emul_it52xx_reset(const struct emul *emul)
{
	struct it52xx_emul_data *data = it52xx_emul_get_data(emul);

	it52xx_emul_reset_data(data);

	return it52xx_emul_set_irq(data, false);
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
