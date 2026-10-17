/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state_driver.h"
#include "gpio.h"
#include "spi.h"
#include "task.h"

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

/* Interrupt line from the fingerprint sensor */
extern "C" void fps_event(gpio_signal signal)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ);
}

namespace fp_platform
{

void lock_pm()
{
}

void unlock_pm()
{
}

#ifdef HAVE_FP_PRIVATE_DRIVER
void enable_sensor_irq()
{
	gpio_clear_pending_interrupt(GPIO_FPS_INT);
	gpio_enable_interrupt(GPIO_FPS_INT);
}

void enter_sensor_low_power(uint32_t evt)
{
	if (evt & TASK_EVENT_SENSOR_IRQ) {
		gpio_clear_pending_interrupt(GPIO_FPS_INT);
	}
	fp_sensor_low_power();
}

void prepare_for_mode_change()
{
	gpio_disable_interrupt(GPIO_FPS_INT);
}

void prepare_for_sensor_or_timer_event(uint32_t)
{
	gpio_disable_interrupt(GPIO_FPS_INT);
}

void flush_spi_after_acquire()
{
	/*
	 * Clean up SPI before clocking up to avoid hang on the dsb
	 * in dma_go. Ignore the return value to let the WDT reboot
	 * the MCU (and avoid getting trapped in the loop).
	 * b/112781659
	 */
	const int spi_res = spi_transaction_flush(SPI_FP_DEVICE);
	if (spi_res) {
		CPRINTS("Failed to flush SPI: 0x%x", spi_res);
	}
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

} /* namespace fp_platform */
