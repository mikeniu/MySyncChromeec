/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_rts5817maa.h"
#include "fingerprint_rts5817maa_pal.h"
#include "fingerprint_rts5817maa_private.h"

#include <stdio.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for RTS5817MAA binary */

LOG_MODULE_REGISTER(rts5817maa_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_RTS5817MAA_HEAP_SIZE);

#define RTS5817MAA_DBG_LOG_EN	1

void __unused rts5817_debug_log(const char *format, ...)
{
	if (format == NULL)
		return;

#if RTS5817MAA_DBG_LOG_EN == 0
	return;
#endif

	va_list args;

	va_start(args, format);
	vprintk(format, args);
	va_end(args);
}

int __unused rts5817_spi_xfer(uint8_t *data, uint32_t len)
{
	int ret = 0;
	const struct rts5817maa_cfg *cfg = fp_sensor_dev->config;
	struct spi_buf spi_buf = {0};

	if (data == NULL) {
		return -EINVAL;
	}

	spi_buf.buf = data;
	spi_buf.len = len;

	const struct spi_buf_set buf_set = {.buffers = &spi_buf, .count = 1};

	ret = spi_transceive_dt(&cfg->spi, &buf_set, &buf_set);
	if (ret < 0) {
		return ret;
	}

	return ret;
}

void __unused rts5817_set_reset_pin(bool active)
{
	const struct rts5817maa_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_set_dt(&cfg->reset_pin, active ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
	}
}

uint64_t __unused rts5817_get_time(void)
{
	return k_uptime_get();
}

void __unused rts5817_busy_wait(uint32_t us)
{
	k_busy_wait(us);
}

void __unused *rts5817_malloc(uint32_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, 32, size, K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Failed to allocate %d bytes", size);
		k_oops();
		CODE_UNREACHABLE;
	}

	return p;
}

void __unused rts5817_free(void *data)
{
	if (data) {
		k_heap_free(&fp_driver_heap, data);
	}
}
