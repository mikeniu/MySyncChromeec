// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <emul/emul_elan_series.h>

#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
#define DT_DRV_COMPAT elan_elan80sg
#else
#error "No elan_series sensor emulator is enabled."
#endif

LOG_MODULE_REGISTER(emul_elan_series, LOG_LEVEL_INF);

struct elan_series_emul_data {
	struct gpio_callback irq_cb;
	const struct emul *target;
	bool stop_spi;
	uint8_t hwid_lo;
	uint8_t hwid_hi;
};

struct elan_series_emul_cfg {
	struct gpio_dt_spec interrupt_pin;
	struct gpio_dt_spec reset_pin;
};

enum elan_series_cmd {
	ELAN_SERIES_CMD_HWID_LO = 0x44,
	ELAN_SERIES_CMD_HWID_HI = 0x42,
};

void elan_series_stop_spi(const struct emul *target)
{
	struct elan_series_emul_data *data = target->data;

	data->stop_spi = true;
}

void elan_series_start_spi(const struct emul *target)
{
	struct elan_series_emul_data *data = target->data;

	data->stop_spi = false;
}

void elan_series_set_hwid(const struct emul *target, uint8_t hwid_lo,
			  uint8_t hwid_hi)
{
	struct elan_series_emul_data *data = target->data;

	data->hwid_lo = hwid_lo;
	data->hwid_hi = hwid_hi;
}

static void elan_series_write_response(const struct spi_buf_set *rx_bufs,
				       const uint8_t *resp, size_t resp_size)
{
	size_t idx = 0;

	for (size_t i = 0; i < rx_bufs->count; i++) {
		const struct spi_buf *rx = &rx_bufs->buffers[i];

		for (size_t j = 0; j < rx->len; j++) {
			((uint8_t *)rx->buf)[j] =
				idx < resp_size ? resp[idx++] : 0;
		}
	}
}

static int elan_series_emul_io(const struct emul *target,
			       const struct spi_config *config,
			       const struct spi_buf_set *tx_bufs,
			       const struct spi_buf_set *rx_bufs)
{
	const struct elan_series_emul_cfg *cfg = target->cfg;
	struct elan_series_emul_data *data = target->data;
	uint8_t cmd;

	if (data->stop_spi) {
		return -EINVAL;
	}

	if (gpio_emul_output_get(cfg->reset_pin.port, cfg->reset_pin.pin) ==
	    0) {
		return -EAGAIN;
	}

	ARG_UNUSED(config);

	__ASSERT_NO_MSG(tx_bufs != NULL);
	__ASSERT_NO_MSG(tx_bufs->buffers != NULL);
	__ASSERT_NO_MSG(tx_bufs->count > 0);
	__ASSERT_NO_MSG(tx_bufs->buffers[0].len > 0);

	/* The first byte contains the command. */
	cmd = *(uint8_t *)tx_bufs->buffers[0].buf;
	uint16_t resp;

	switch (cmd) {
	case ELAN_SERIES_CMD_HWID_LO:
		__ASSERT_NO_MSG(rx_bufs != NULL);
		/*
		 * The first must be 0x00, because it's received when MCU is
		 * transmitting the command.
		 */
		resp = ((uint16_t)data->hwid_lo) << 8;

		elan_series_write_response(rx_bufs, (uint8_t *)&resp, 2);
		break;

	case ELAN_SERIES_CMD_HWID_HI:
		__ASSERT_NO_MSG(rx_bufs != NULL);
		/*
		 * The first must be 0x00, because it's received when MCU is
		 * transmitting the command.
		 */
		resp = ((uint16_t)data->hwid_hi) << 8;

		elan_series_write_response(rx_bufs, (uint8_t *)&resp, 2);
		break;

	default:
		LOG_WRN("Unimplemented command 0x%x", cmd);
	}

	return 0;
}

static struct spi_emul_api elan_series_emul_api = {
	.io = elan_series_emul_io,
};

static void elan_series_emul_reset(const struct emul *target)
{
	struct elan_series_emul_data *data = target->data;

#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
	data->hwid_lo = 0x4F;
	data->hwid_hi = 0x4F;
#else
#error "No elan_series sensor is enabled."
#endif

	data->stop_spi = false;
}

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>

/* Add test reset handlers in when using emulators with tests */
#define ELAN_SERIES_EMUL_RESET_RULE_AFTER(inst) \
	elan_series_emul_reset(EMUL_DT_GET(DT_DRV_INST(inst)))

static void
elan_series_emul_reset_rule_after(const struct ztest_unit_test *test,
				  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(ELAN_SERIES_EMUL_RESET_RULE_AFTER);
}
ZTEST_RULE(elan_series_emul_reset, NULL, elan_series_emul_reset_rule_after);

#endif /* CONFIG_ZTEST */

static int elan_series_emul_init(const struct emul *target,
				 const struct device *parent)
{
	struct elan_series_emul_data *data = target->data;
	ARG_UNUSED(parent);

	data->target = target;

	elan_series_emul_reset(target);

	return 0;
}

#define ELAN_SERIES_EMUL(n)                                                   \
	static const struct elan_series_emul_cfg elan_series_emul_cfg_##n = { \
		.interrupt_pin = GPIO_DT_SPEC_INST_GET(n, irq_gpios),         \
		.reset_pin = GPIO_DT_SPEC_INST_GET(n, reset_gpios),           \
	};                                                                    \
	static struct elan_series_emul_data elan_series_emul_data##n;         \
	EMUL_DT_INST_DEFINE(n, elan_series_emul_init,                         \
			    &elan_series_emul_data##n,                        \
			    &elan_series_emul_cfg_##n, &elan_series_emul_api, \
			    NULL)

DT_INST_FOREACH_STATUS_OKAY(ELAN_SERIES_EMUL);
