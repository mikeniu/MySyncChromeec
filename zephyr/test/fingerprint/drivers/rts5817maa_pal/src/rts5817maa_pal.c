/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#include <fingerprint_rts5817maa.h>
#include <fingerprint_rts5817maa_pal.h>

#define RTS5817MAA_NODE DT_NODELABEL(rts5817maa)

#define PAL_TEST_HEAP_SIZE CONFIG_FINGERPRINT_SENSOR_RTS5817MAA_HEAP_SIZE

static const struct rts5817maa_cfg pal_test_cfg = {
	.spi = SPI_DT_SPEC_GET(RTS5817MAA_NODE,
			       SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),
	.interrupt = GPIO_DT_SPEC_GET(RTS5817MAA_NODE, irq_gpios),
	.reset_pin = GPIO_DT_SPEC_GET(RTS5817MAA_NODE, reset_gpios),
};

DEVICE_DT_DEFINE(RTS5817MAA_NODE, NULL, NULL, NULL, &pal_test_cfg, POST_KERNEL,
		 CONFIG_FINGERPRINT_SENSOR_INIT_PRIORITY, NULL);

/* State of the minimal SPI peripheral emulator. */
static uint8_t emul_last_tx[16];
static size_t emul_last_len;
static int emul_io_ret;

static int pal_emul_io(const struct emul *target, const struct spi_config *config,
		       const struct spi_buf_set *tx_bufs,
		       const struct spi_buf_set *rx_bufs)
{
	ARG_UNUSED(target);
	ARG_UNUSED(config);

	if (emul_io_ret != 0) {
		return emul_io_ret;
	}

	zassert_true(tx_bufs->buffers[0].len <= sizeof(emul_last_tx),
		     "TX length exceeds emulator buffer");
	zassert_equal(tx_bufs->buffers[0].len, rx_bufs->buffers[0].len,
		      "TX and RX lengths must match");
	emul_last_len = tx_bufs->buffers[0].len;
	memcpy(emul_last_tx, tx_bufs->buffers[0].buf, emul_last_len);

	/* The peripheral answers with the bitwise inverse of every byte. */
	for (size_t i = 0; i < rx_bufs->buffers[0].len; i++) {
		((uint8_t *)rx_bufs->buffers[0].buf)[i] =
			~emul_last_tx[i];
	}

	return 0;
}

static const struct spi_emul_api pal_emul_api = {
	.io = pal_emul_io,
};

static int pal_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);

	return 0;
}

EMUL_DT_DEFINE(RTS5817MAA_NODE, pal_emul_init, NULL, NULL, &pal_emul_api, NULL);

static void rts5817maa_pal_before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(emul_last_tx, 0, sizeof(emul_last_tx));
	emul_last_len = 0;
	emul_io_ret = 0;
}

ZTEST_SUITE(rts5817maa_pal, NULL, NULL, rts5817maa_pal_before, NULL, NULL);

ZTEST(rts5817maa_pal, test_spi_xfer_full_duplex)
{
	const uint8_t expected_tx[] = { 0x01, 0x02, 0xA5 };
	const uint8_t expected_rx[] = { (uint8_t)~0x01, (uint8_t)~0x02,
					(uint8_t)~0xA5 };
	uint8_t data[] = { 0x01, 0x02, 0xA5 };

	zassert_ok(rts5817_spi_xfer(data, sizeof(data)));

	zassert_equal(emul_last_len, sizeof(data));
	zassert_mem_equal(emul_last_tx, expected_tx, sizeof(expected_tx));
	/* The same buffer is used for TX and RX. */
	zassert_mem_equal(data, expected_rx, sizeof(expected_rx));
}

ZTEST(rts5817maa_pal, test_spi_xfer_null_data)
{
	zassert_equal(rts5817_spi_xfer(NULL, 4), -EINVAL);
	zassert_equal(emul_last_len, 0);
}

ZTEST(rts5817maa_pal, test_spi_xfer_bus_error)
{
	uint8_t data[] = { 0x01 };

	emul_io_ret = -EIO;
	zassert_equal(rts5817_spi_xfer(data, sizeof(data)), -EIO);
}

ZTEST(rts5817maa_pal, test_set_reset_pin)
{
	const struct gpio_dt_spec reset_pin =
		GPIO_DT_SPEC_GET(RTS5817MAA_NODE, reset_gpios);

	zassert_true(gpio_is_ready_dt(&reset_pin));
	zassert_ok(gpio_pin_configure_dt(&reset_pin, GPIO_OUTPUT_INACTIVE));

	/* The reset pin is active low, so the raw output goes low. */
	rts5817_set_reset_pin(true);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 0);

	rts5817_set_reset_pin(false);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 1);
}

ZTEST(rts5817maa_pal, test_get_time)
{
	uint64_t before = k_uptime_get();
	uint64_t pal_time = rts5817_get_time();
	uint64_t after = k_uptime_get();

	zassert_true(pal_time >= before);
	zassert_true(pal_time <= after);
}

ZTEST(rts5817maa_pal, test_busy_wait)
{
	const uint32_t wait_us = 1000;
	uint32_t start = k_cycle_get_32();
	uint32_t elapsed_us;

	rts5817_busy_wait(wait_us);
	elapsed_us = k_cyc_to_us_near32(k_cycle_get_32() - start);

	zassert_true(elapsed_us >= wait_us, "busy wait took only %u us",
		     elapsed_us);
}

ZTEST(rts5817maa_pal, test_malloc_free)
{
	const size_t alloc_size = 64;
	uint8_t *p = rts5817_malloc(alloc_size);

	zassert_not_null(p);
	zassert_equal((uintptr_t)p % RTS5817MAA_MEM_ALIGNMENT, 0);

	memset(p, 0xA5, alloc_size);
	rts5817_free(p);
}

ZTEST(rts5817maa_pal, test_free_null)
{
	rts5817_free(NULL);
}

ZTEST(rts5817maa_pal, test_malloc_reuses_freed_memory)
{
	/*
	 * Allocating nearly the whole heap only works if the previous
	 * allocation was returned to the heap.
	 */
	size_t big = PAL_TEST_HEAP_SIZE / 2;
	void *first = rts5817_malloc(big);

	zassert_not_null(first);
	rts5817_free(first);

	void *second = rts5817_malloc(big);

	zassert_not_null(second);
	rts5817_free(second);
}

ZTEST(rts5817maa_pal, test_malloc_exhausted_panics)
{
	ztest_set_fault_valid(true);

	rts5817_malloc(PAL_TEST_HEAP_SIZE + 1);

	zassert_unreachable("rts5817_malloc did not trigger k_oops()");
}

ZTEST(rts5817maa_pal, test_debug_log)
{
	rts5817_debug_log(NULL);
	rts5817_debug_log("pal test log %d", 42);
}
