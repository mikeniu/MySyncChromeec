/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

enum sspi_clk_sel {
	sspi_clk_24mhz = 0,
	sspi_clk_12mhz,
	sspi_clk_8mhz,
	sspi_clk_6mhz,
	sspi_clk_4p8mhz,
	sspi_clk_4mhz,
	sspi_clk_3p428mhz,
	sspi_clk_3mhz,
};

<<<<<<< HEAD
enum sspi_ch_sel {
	SSPI_CH_CS0 = 0,
	SSPI_CH_CS1,
=======
/* A host command is in processing by the host command task? */
static bool shi_cmd_processing;

/* Store read and write data buffer */
static uint8_t in_msg[SPI_RX_MAX_FIFO_SIZE] __aligned(4);
static uint8_t out_msg[SPI_TX_MAX_FIFO_SIZE] __aligned(4);

/* Parameters used by host protocols */
static struct host_packet spi_packet;

enum spi_peripheral_state_machine {
	/* Ready to receive next request */
	SPI_STATE_READY_TO_RECV,
	/* Receiving request */
	SPI_STATE_RECEIVING,
	/* Processing request */
	SPI_STATE_PROCESSING,
	/* Received bad data */
	SPI_STATE_RX_BAD,

	SPI_STATE_COUNT,
} spi_peripheral_state;

static const int spi_response_state[] = {
	[SPI_STATE_READY_TO_RECV] = EC_SPI_RX_READY,
	[SPI_STATE_RECEIVING] = EC_SPI_RECEIVING,
	[SPI_STATE_PROCESSING] = EC_SPI_PROCESSING,
	[SPI_STATE_RX_BAD] = EC_SPI_RX_BAD_DATA,
>>>>>>> 04a0e33a97 (it83xx/spi: added shi_cmd_processing flag)
};

static void sspi_frequency(enum sspi_clk_sel freq)
{
	/*
	 * bit[6:5]
	 * Bit 6:Clock Polarity (CLPOL)
	 * 0: SSCK is low in the idle mode.
	 * 1: SSCK is high in the idle mode.
	 * Bit 5:Clock Phase (CLPHS)
	 * 0: Latch data on the first SSCK edge.
	 * 1: Latch data on the second SSCK edge.
	 *
	 * bit[4:2]
	 * 000b: 1/2 clk_sspi
	 * 001b: 1/4 clk_sspi
	 * 010b: 1/6 clk_sspi
	 * 011b: 1/8 clk_sspi
	 * 100b: 1/10 clk_sspi
	 * 101b: 1/12 clk_sspi
	 * 110b: 1/14 clk_sspi
	 * 111b: 1/16 clk_sspi
	 *
	 * SSCK frequency is [freq] MHz and mode 3.
	 * note, clk_sspi need equal to 48MHz above.
	 */
	IT83XX_SSPI_SPICTRL1 |= (0x60 | (freq << 2));
}

static void sspi_transmission_end(void)
{
	/* Write 1 to end the SPI transmission. */
	IT83XX_SSPI_SPISTS = 0x20;

<<<<<<< HEAD
	/* Short delay for "Transfer End Flag" */
	IT83XX_GCTRL_WNCKR = 0;
=======
	shi_cmd_processing = false;

	if (spi_peripheral_state != SPI_STATE_PROCESSING) {
		CPRINTS("The request data is not processing.");
		return;
	}
>>>>>>> 04a0e33a97 (it83xx/spi: added shi_cmd_processing flag)

	/* Write 1 to clear this bit and terminate data transmission. */
	IT83XX_SSPI_SPISTS = 0x02;
}

/* We assume only one SPI port in the chip, one SPI device */
int spi_enable(int port, int enable)
{
	if (enable) {
		/*
		 * bit[5:4]
		 * 00b: SPI channel 0 and channel 1 are disabled.
		 * 10b: SSCK/SMOSI/SMISO/SSCE1# are enabled.
		 * 01b: SSCK/SMOSI/SMISO/SSCE0# are enabled.
		 * 11b: SSCK/SMOSI/SMISO/SSCE1#/SSCE0# are enabled.
		 */
		if (port == SSPI_CH_CS1)
			IT83XX_GPIO_GRC1 |= 0x20;
		else
			IT83XX_GPIO_GRC1 |= 0x10;

<<<<<<< HEAD
		gpio_config_module(MODULE_SPI, 1);
=======
	/* CPU Rx FIFO1 access */
	IT83XX_SPI_TXRXFAR = IT83XX_SPI_CPURXF1A;
	/*
	 * In spi_parse_header, the request data will separate to
	 * write in_msg buffer so we cannot set CPU to end accessing
	 * Rx FIFO in this function. We will set IT83XX_SPI_TXRXFAR = 0
	 * in reset_rx_fifo.
	 */

	for (i = 0; i < count; i += 4)
		/* Get data from controller to buffer */
		*(uint32_t *)(in_msg_addr + i) = IT83XX_SPI_RXFRDRB0;
}

/* Parse header for version of spi-protocol */
static void spi_parse_header(void)
{
	struct ec_host_request r;

	/* Store request header from Rx FIFO to local buffer */
	spi_host_request_data((uint8_t *)&r, sizeof(r));

	/* Protocol version 3 */
	if (r.struct_version == EC_HOST_REQUEST_VERSION) {
		int pkt_size;

		/* Check how big the packet should be */
		pkt_size = host_request_expected_size(&r);

		if (pkt_size == 0 || pkt_size > sizeof(in_msg))
			return spi_bad_received_data(pkt_size);

		/* Copy verified header to in_msg buffer */
		memcpy(in_msg, &r, sizeof(r));

		/* Store request data from Rx FIFO to in_msg buffer */
		spi_host_request_data(in_msg + sizeof(r), pkt_size - sizeof(r));

		/* Set up parameters for host request */
		spi_packet.send_response = spi_send_response_packet;
		spi_packet.request = in_msg;
		spi_packet.request_temp = NULL;
		spi_packet.request_max = sizeof(in_msg);
		spi_packet.request_size = pkt_size;

		/* Response must start with the preamble */
		memcpy(out_msg, out_preamble, EC_SPI_PREAMBLE_LENGTH);

		spi_packet.response = out_msg + EC_SPI_PREAMBLE_LENGTH;
		/* Reserve space for frame start and trailing past-end byte */
		spi_packet.response_max = SPI_MAX_RESPONSE_SIZE;
		spi_packet.response_size = 0;
		spi_packet.driver_result = EC_RES_SUCCESS;

		/* Move to processing state */
		spi_set_state(SPI_STATE_PROCESSING);

		/* Go to common-layer to handle request */
		shi_cmd_processing = true;
		host_packet_receive(&spi_packet);
>>>>>>> 04a0e33a97 (it83xx/spi: added shi_cmd_processing flag)
	} else {
		if (port == SSPI_CH_CS1)
			IT83XX_GPIO_GRC1 &= ~0x20;
		else
			IT83XX_GPIO_GRC1 &= ~0x10;

		gpio_config_module(MODULE_SPI, 0);
	}

<<<<<<< HEAD
	return EC_SUCCESS;
}

int spi_transaction(const struct spi_device_t *spi_device,
		const uint8_t *txdata, int txlen,
		uint8_t *rxdata, int rxlen)
{
	int idx;
	uint8_t port = spi_device->port;
	static struct mutex spi_mutex;

	mutex_lock(&spi_mutex);
	/* bit[0]: Write cycle */
	IT83XX_SSPI_SPICTRL2 &= ~0x04;
	for (idx = 0x00; idx < txlen; idx++) {
		IT83XX_SSPI_SPIDATA = txdata[idx];
		if (port == SSPI_CH_CS1)
			/* Write 1 to start the data transmission of CS1 */
			IT83XX_SSPI_SPISTS |= 0x08;
		else
			/* Write 1 to start the data transmission of CS0 */
			IT83XX_SSPI_SPISTS |= 0x10;
=======
	/*
	 * The status of SPI end detection interrupt bit is set, it
	 * means that host command parse has been completed and AP
	 * has received the last byte which is EC_SPI_PAST_END from
	 * EC responded data, then AP ended the transaction.
	 */
	if (IT83XX_SPI_ISR & IT83XX_SPI_ENDDETECTINT) {
		/* Ready to receive */
		spi_set_state(SPI_STATE_READY_TO_RECV);
		/*
		 * Once there is no SPI active, enable idle task deep
		 * sleep bit of SPI in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_SPI);
		/* CS# is deasserted, so write clear all peripheral status */
		IT83XX_SPI_ISR = 0xff;
	}
	/*
	 * The status of Rx valid length interrupt bit is set that
	 * indicates reached target count(IT83XX_SPI_FTCB1R,
	 * IT83XX_SPI_FTCB0R) and the length field of the host
	 * requested data.
	 */
	if (IT83XX_SPI_RX_VLISR & IT83XX_SPI_RVLI) {
		/* write clear peripheral status */
		IT83XX_SPI_RX_VLISR = IT83XX_SPI_RVLI;
		/* Parse header for version of spi-protocol */
		if (!shi_cmd_processing) {
			spi_parse_header();
		} else {
			CPRINTS("The host cmd task is processing the request.");
		}
>>>>>>> 04a0e33a97 (it83xx/spi: added shi_cmd_processing flag)
	}

	/* bit[1]: Read cycle */
	IT83XX_SSPI_SPICTRL2 |= 0x04;
	for (idx = 0x00; idx < rxlen; idx++) {
		if (port == SSPI_CH_CS1)
			/* Write 1 to start the data transmission of CS1 */
			IT83XX_SSPI_SPISTS |= 0x08;
		else
			/* Write 1 to start the data transmission of CS0 */
			IT83XX_SSPI_SPISTS |= 0x10;
		rxdata[idx] = IT83XX_SSPI_SPIDATA;
	}

	sspi_transmission_end();
	mutex_unlock(&spi_mutex);

	return EC_SUCCESS;
}

static void sspi_init(void)
{
	int i;

	clock_enable_peripheral(CGC_OFFSET_SSPI, 0, 0);
	sspi_frequency(sspi_clk_8mhz);

	/*
	 * bit[5:3] Byte Width (BYTEWIDTH)
	 * 000b: 8-bit transmission
	 * 001b: 1-bit transmission
	 * 010b: 2-bit transmission
	 * 011b: 3-bit transmission
	 * 100b: 4-bit transmission
	 * 101b: 5-bit transmission
	 * 110b: 6-bit transmission
	 * 111b: 7-bit transmission
	 *
	 * bit[1] Blocking selection
	 */
	IT83XX_SSPI_SPICTRL2 |= 0x02;

	for (i = 0; i < spi_devices_used; i++)
		/* Disabling spi module */
		spi_enable(spi_devices[i].port, 0);
}
DECLARE_HOOK(HOOK_INIT, sspi_init, HOOK_PRIO_INIT_SPI);
