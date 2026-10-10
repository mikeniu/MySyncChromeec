/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupt based USART RX driver for STM32F0 and STM32F3 */

#include "atomic.h"
#include "common.h"
#include "queue.h"
#include "registers.h"
#include "usart.h"

#if defined(SECTION_IS_RO) && defined(CONFIG_STREAM_USART4)
static uint8_t usart4_rx_buf[80];
static size_t usart4_rx_len;
static bool usart4_rx_err;
extern volatile bool usart4_rx_corrupt_irq;
#endif

static void usart_rx_init(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	STM32_USART_CR1(base) |= STM32_USART_CR1_RXNEIE;
	STM32_USART_CR1(base) |= STM32_USART_CR1_RE;
#if defined(SECTION_IS_RO) && defined(CONFIG_STREAM_USART4)
	if (base == STM32_USART4_BASE) {
		usart4_rx_len = 0;
		usart4_rx_err = false;
		STM32_USART_CR3(base) &= ~STM32_USART_CR3_OVRDIS;
		STM32_USART_ICR(base) = STM32_USART_ICR_ORECF |
					STM32_USART_ICR_IDLECF | BIT(1) |
					BIT(2);
		STM32_USART_CR1(base) |= STM32_USART_CR1_IDLEIE;
	} else {
		STM32_USART_CR3(base) |= STM32_USART_CR3_OVRDIS;
	}
#else
	STM32_USART_CR3(base) |= STM32_USART_CR3_OVRDIS;
#endif
}

static void usart_rx_interrupt_handler(struct usart_config const *config)
{
	intptr_t base = config->hw->base;
	int32_t status = STM32_USART_SR(base);

#if defined(SECTION_IS_RO) && defined(CONFIG_STREAM_USART4)
	if (base == STM32_USART4_BASE) {
		if (status & (STM32_USART_SR_ORE | BIT(1) | BIT(2))) {
			STM32_USART_ICR(base) = STM32_USART_ICR_ORECF | BIT(1) |
						BIT(2);
			if (status & (STM32_USART_SR_ORE | BIT(1))) {
				usart4_rx_err = true;
				atomic_add((atomic_t *)&(
						   config->state->rx_overrun),
					   1);
			}
		}
		while (status & STM32_USART_SR_RXNE) {
			uint8_t byte = STM32_USART_RDR(base);

			if (usart4_rx_len < sizeof(usart4_rx_buf))
				usart4_rx_buf[usart4_rx_len++] = byte;
			else
				usart4_rx_err = true;
			status = STM32_USART_SR(base);
			if (status & (STM32_USART_SR_ORE | BIT(1) | BIT(2))) {
				STM32_USART_ICR(base) = STM32_USART_ICR_ORECF |
							BIT(1) | BIT(2);
				if (status & (STM32_USART_SR_ORE | BIT(1))) {
					usart4_rx_err = true;
					atomic_add(
						(atomic_t *)&(
							config->state
								->rx_overrun),
						1);
				}
			}
		}
		if ((status & STM32_USART_SR_IDLE) ||
		    usart4_rx_len >= sizeof(usart4_rx_buf)) {
			if (status & STM32_USART_SR_IDLE)
				STM32_USART_ICR(base) = STM32_USART_ICR_IDLECF;
			if (usart4_rx_err) {
				usart4_rx_len = 0;
				if (status & STM32_USART_SR_IDLE) {
					usart4_rx_err = false;
					usart4_rx_corrupt_irq = true;
					config->producer.queue->policy->add(
						config->producer.queue->policy,
						0);
				}
			} else if (usart4_rx_len > 0) {
				size_t added = queue_add_units(
					config->producer.queue, usart4_rx_buf,
					usart4_rx_len);
				if (added < usart4_rx_len) {
					atomic_add(
						(atomic_t *)&(
							config->state
								->rx_dropped),
						usart4_rx_len - added);
					usart4_rx_corrupt_irq = true;
					config->producer.queue->policy->add(
						config->producer.queue->policy,
						0);
				}
				usart4_rx_len = 0;
			}
			while (STM32_USART_SR(base) & STM32_USART_SR_RXNE) {
				uint8_t byte = STM32_USART_RDR(base);

				if (usart4_rx_len < sizeof(usart4_rx_buf))
					usart4_rx_buf[usart4_rx_len++] = byte;
				else
					usart4_rx_err = true;
			}
		}
		return;
	}
#endif

	if (status & STM32_USART_SR_RXNE) {
		uint8_t byte = STM32_USART_RDR(base);

		if (!queue_add_unit(config->producer.queue, &byte))
			atomic_add((atomic_t *)&(config->state->rx_dropped), 1);
	}
}

struct usart_rx const usart_rx_interrupt = {
	.producer_ops = {
		/*
		 * Nothing to do here, we either had enough space in the queue
		 * when a character came in or we dropped it already.
		 */
		.read = NULL,
	},

	.init      = usart_rx_init,
	.interrupt = usart_rx_interrupt_handler,
	.info      = NULL,
};
