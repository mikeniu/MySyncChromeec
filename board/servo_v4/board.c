/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo V4 configuration */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
/* Just want the .h file for PS8742 definitions, not the large object file. */
#define CONFIG_USB_MUX_PS8742
#include "ps8740.h"
#undef CONFIG_USB_MUX_PS8742
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#ifdef SECTION_IS_RO
#include "usb_bridge.h"
#endif
#include "usb-stream.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_spi.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/******************************************************************************
 * GPIO interrupt handlers.
 */

#ifdef SECTION_IS_RO
static void vbus0_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C0);
}

static void vbus1_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C1);
}

static volatile uint64_t hpd_prev_ts;
static volatile int hpd_prev_level;

/**
 * Hotplug detect deferred task
 *
 * Called after level change on hpd GPIO to evaluate (and debounce) what event
 * has occurred.  There are 3 events that occur on HPD:
 *    1. low  : downstream display sink is deattached
 *    2. high : downstream display sink is attached
 *    3. irq  : downstream display sink signalling an interrupt.
 *
 * The debounce times for these various events are:
 *   HPD_USTREAM_DEBOUNCE_LVL : min pulse width of level value.
 *   HPD_USTREAM_DEBOUNCE_IRQ : min pulse width of IRQ low pulse.
 *
 * lvl(n-2) lvl(n-1)  lvl   prev_delta  now_delta event
 * ----------------------------------------------------
 * 1        0         1     <IRQ        n/a       low glitch (ignore)
 * 1        0         1     >IRQ        <LVL      irq
 * x        0         1     n/a         >LVL      high
 * 0        1         0     <LVL        n/a       high glitch (ignore)
 * x        1         0     n/a         >LVL      low
 */

void hpd_irq_deferred(void)
{
	int dp_mode = pd_alt_mode(1, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	if (dp_mode) {
		pd_send_hpd(DUT, hpd_irq);
		CPRINTS("HPD IRQ");
	}
}
DECLARE_DEFERRED(hpd_irq_deferred);

void hpd_lvl_deferred(void)
{
	int level = gpio_get_level(GPIO_DP_HPD);
	int dp_mode = pd_alt_mode(1, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	if (level != hpd_prev_level) {
		/* It's a glitch while in deferred or canceled action */
		return;
	}

	if (dp_mode) {
		pd_send_hpd(DUT, level ? hpd_high : hpd_low);
		CPRINTS("HPD: %d", level);
	}
}
DECLARE_DEFERRED(hpd_lvl_deferred);

void hpd_evt(enum gpio_signal signal)
{
	int level = gpio_get_level(signal);
	uint64_t now_val = get_time().val;
	uint64_t cur_delta = now_val - hpd_prev_ts;

	/* Store current time */
	hpd_prev_ts = now_val;

	/* All previous hpd level events need to be re-triggered */
	hook_call_deferred(&hpd_lvl_deferred_data, -1);

	/* It's a glitch.  Previous time moves but level is the same. */
	if (cur_delta < HPD_USTREAM_DEBOUNCE_IRQ)
		return;

	if ((!hpd_prev_level && level) &&
	    (cur_delta < HPD_USTREAM_DEBOUNCE_LVL)) {
		/* It's an irq */
		hook_call_deferred(&hpd_irq_deferred_data, 0);
	} else if (cur_delta >= HPD_USTREAM_DEBOUNCE_LVL) {
		hook_call_deferred(&hpd_lvl_deferred_data,
				   HPD_USTREAM_DEBOUNCE_LVL);
	}

	hpd_prev_level = level;
}
#else /* !SECTION_IS_RO */
static void vbus0_evt(enum gpio_signal signal)
{
}
static void vbus1_evt(enum gpio_signal signal)
{
}
void hpd_evt(enum gpio_signal signal)
{
}
#endif /* SECTION_IS_RO */

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************
 * Board pre-init function.
 */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/*
	 * The DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (CHG RX) - Default mapping
	 *  Chan 3 : SPI1_TX   (CHG TX) - Default mapping
	 *  Chan 4 : TIM3_CH1  (DUT RX) - Default mapping
	 *  Chan 5 : SPI2_TX   (DUT TX) - Default mapping
	 *  Chan 6 : USART4 RX (ATmega32U4) - Default mapping
	 *  Chan 7 : USART4 TX (ATmega32U4) - Default mapping
	 *
	 * USART1 does not use DMA on this board (CONFIG_UART_TX/RX_DMA are
	 * disabled and chip/stm32/uart.c clears DMAR/DMAT). Therefore, Port 1
	 * DUT USB-PD TIM3_CH1 and SPI2_TX remain on their hardware-default
	 * DMA1_CH4 and DMA1_CH5 (symmetric with Port 0 CHG TIM1_CH1 and SPI1_TX
	 * on DMA1_CH2 and DMA1_CH3), leaving DMA1_CH6 and DMA1_CH7 free for
	 * USART4_RX and USART4_TX with no SYSCFG_CFGR1 remap bits set.
	 */
}

/******************************************************************************
 * Set up USB PD
 */

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CHG_CC1_PD] = { "CHG_CC1_PD", 3300, 4096, 0, STM32_AIN(2) },
	[ADC_CHG_CC2_PD] = { "CHG_CC2_PD", 3300, 4096, 0, STM32_AIN(4) },
	[ADC_DUT_CC1_PD] = { "DUT_CC1_PD", 3300, 4096, 0, STM32_AIN(0) },
	[ADC_DUT_CC2_PD] = { "DUT_CC2_PD", 3300, 4096, 0, STM32_AIN(5) },
	[ADC_SBU1_DET] = { "SBU1_DET", 3300, 4096, 0, STM32_AIN(3) },
	[ADC_SBU2_DET] = { "SBU2_DET", 3300, 4096, 0, STM32_AIN(7) },
	[ADC_SUB_C_REF] = { "SUB_C_REF", 3300, 4096, 0, STM32_AIN(1) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 16
#define USB_STREAM_TX_SIZE 16

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 =
	QUEUE_DIRECT(64, uint8_t, usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart3_to_usb, usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb, USB_IFACE_USART3_STREAM,
		  USB_STR_USART3_STREAM_NAME, USB_EP_USART3_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart3,
		  usart3_to_usb)

/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

#ifdef SECTION_IS_RO
/*
 * In RO, usart4 connects to the ATmega32U4 over 1.000 Mbps UART (8N1).
 * Shared queues are non-static so usb_bridge.c can bind consumers/producers
 * and dequeue/enqueue data directly.
 * Keyboard queues use 16 bytes (matching USB_STREAM_RX/TX_SIZE = 16) to
 * save 96 bytes of RO SRAM, while UART queues use 128 bytes (holding a
 * 68-byte FRAME_DATA plus SERVO_CTRL_TX_HEADROOM per Target 2).
 */
struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

struct queue const usart4_to_bridge =
	QUEUE_DIRECT(128, uint8_t, usart4.producer, usb_bridge_uart_consumer);
struct queue const bridge_to_usart4 =
	QUEUE_DIRECT(128, uint8_t, usb_bridge_uart_producer, usart4.consumer);

struct queue const usb_to_bridge_kbd =
	QUEUE_DIRECT(16, uint8_t, usart4_usb.producer, usb_bridge_kbd_consumer);
struct queue const bridge_kbd_to_usb =
	QUEUE_DIRECT(16, uint8_t, usb_bridge_kbd_producer, usart4_usb.consumer);

static struct usart_rx_dma const usart4_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH6, 80);
static struct usart_tx_dma const usart4_tx_dma =
	USART_TX_DMA(STM32_DMAC_CH7, 34);

struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart4_rx_dma.usart_rx, usart4_tx_dma.usart_tx,
		     1000000, 0, usart4_to_bridge, bridge_to_usart4);

/*
 * Expand usart4_usb using USB_STREAM_CONFIG instead of
 * USB_STREAM_CONFIG_USART_IFACE. USB_STREAM_CONFIG expands to
 * USB_STREAM_CONFIG_FULL without binding the default usb_usart_interface,
 * allowing usart4_iface_request to absorb baud changes.
 */
USB_STREAM_CONFIG(usart4_usb, USB_IFACE_USART4_STREAM,
		  USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_bridge_kbd,
		  bridge_kbd_to_usb)

/* usart4_iface_request is registered via USB_DECLARE_IFACE in usb_bridge.c */
#else /* !SECTION_IS_RO */
/* Stock 9600-baud direct UART-to-USB wiring for RW fallback image */
static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 =
	QUEUE_DIRECT(64, uint8_t, usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart_rx_interrupt, usart_tx_interrupt, 9600, 0,
		     usart4_to_usb, usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb, USB_IFACE_USART4_STREAM,
		  USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart4,
		  usart4_to_usb)
#endif /* SECTION_IS_RO */

/*
 * Define usb interface descriptor for the `EMPTY` usb interface, to satisfy
 * UEFI and kernel requirements (see b/183857501).
 */
#ifndef SECTION_IS_RO
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_EMPTY) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_EMPTY,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};
#endif

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Servo V4"),
	[USB_STR_SERIALNO] = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo EC Shell"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("DUT UART"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("Atmega UART"),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
#ifdef SECTION_IS_RO
	[USB_STR_BRIDGE_NAME] = USB_STRING_DESC("Android ADB/Fastboot"),
#endif
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "master",
	  .port = I2C_PORT_MASTER,
	  .kbps = 100,
	  .scl = GPIO_MASTER_I2C_SCL,
	  .sda = GPIO_MASTER_I2C_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/******************************************************************************
 * Initialize board.
 */

/*
 * Support tca6416 I2C ioexpander.
 */
#define GPIOX_I2C_ADDR_FLAGS 0x20
#define GPIOX_IN_PORT_A 0x0
#define GPIOX_IN_PORT_B 0x1
#define GPIOX_OUT_PORT_A 0x2
#define GPIOX_OUT_PORT_B 0x3
#define GPIOX_DIR_PORT_A 0x6
#define GPIOX_DIR_PORT_B 0x7

#ifdef SECTION_IS_RO
static timestamp_t atmel_reset_assert_time;
#define ATMEL_RESET_MIN_HOLD_MS 10
#define ATMEL_RESET_MIN_HOLD_US (ATMEL_RESET_MIN_HOLD_MS * 1000)

static inline bool is_ioexp_atmel_reset(int port, uint16_t addr_flags,
					const uint8_t *out, int write_count)
{
	return port == I2C_PORT_MASTER && addr_flags == GPIOX_I2C_ADDR_FLAGS &&
	       write_count >= 2 && out[0] == GPIOX_OUT_PORT_A;
}

static void atmel_reset_pre_write(int val)
{
	if (val && atmel_reset_assert_time.val &&
	    (get_time().val - atmel_reset_assert_time.val) <
		    ATMEL_RESET_MIN_HOLD_US)
		crec_msleep(ATMEL_RESET_MIN_HOLD_MS);
}

static void atmel_reset_post_write(int val)
{
	if (!val)
		atmel_reset_assert_time = get_time();
	usb_bridge_notify_atmel_reset(val);
}

void usb_i2c_board_xfer_pre(int port, uint16_t addr_flags, const uint8_t *out,
			    int write_count)
{
	if (is_ioexp_atmel_reset(port, addr_flags, out, write_count))
		atmel_reset_pre_write(out[1] & BIT(1));
}

void usb_i2c_board_xfer_post(int port, uint16_t addr_flags, const uint8_t *out,
			     int write_count, int ret)
{
	if (ret == EC_SUCCESS &&
	    is_ioexp_atmel_reset(port, addr_flags, out, write_count))
		atmel_reset_post_write(out[1] & BIT(1));
}
#endif

/* Write a Port A GPIO output on the tca6416 I2C ioexpander. */
static void write_ioexpander(int gpio, int val)
{
	int tmp;

	/* Read output port register */
	i2c_read8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_A, &tmp);
	if (val)
		tmp |= BIT(gpio);
	else
		tmp &= ~BIT(gpio);
	/* Write back modified output port register */
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_A, tmp);
}

/* Enable all ioexpander outputs. */
static void init_ioexpander(void)
{
	static const uint8_t init_cmds[][2] = {
		{ GPIOX_OUT_PORT_A, 0xa8 },
		{ GPIOX_OUT_PORT_B, 0x00 },
		{ GPIOX_DIR_PORT_A, 0x00 },
		{ GPIOX_DIR_PORT_B, 0x18 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(init_cmds); i++)
		i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, init_cmds[i][0],
			   init_cmds[i][1]);
#ifdef SECTION_IS_RO
	atmel_reset_post_write(0);
#endif
}

/*
 * Define voltage thresholds for SBU USB detection.
 *
 * Max observed USB low across sampled systems: 666mV
 * Min observed USB high across sampled systems: 3026mV
 */
#define GND_MAX_MV 700
#define USB_HIGH_MV 2500
#define SBU_DIRECT 0
#define SBU_FLIP 1

#define MODE_SBU_DISCONNECT 0
#define MODE_SBU_CONNECT 1
#define MODE_SBU_FLIP 2
#define MODE_SBU_OTHER 3

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);
static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;
	int mux_en;
	static int count /* = 0 */;
	static int last /* = 0 */;
	static int polarity /* = 0 */;

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);
	mux_en = gpio_get_level(GPIO_SBU_MUX_EN);

	int mode = MODE_SBU_OTHER;

	if (!mux_en && sbu1 > USB_HIGH_MV && sbu2 < GND_MAX_MV) {
		mode = MODE_SBU_FLIP;
		polarity = SBU_FLIP;
	} else if (!mux_en && sbu2 > USB_HIGH_MV && sbu1 < GND_MAX_MV) {
		mode = MODE_SBU_CONNECT;
		polarity = SBU_DIRECT;
	} else if (mux_en && sbu1 < GND_MAX_MV && sbu2 < GND_MAX_MV) {
		mode = MODE_SBU_DISCONNECT;
	}

	if (last != mode) {
		last = mode;
		count = 0;
	} else {
		count++;
	}

	/*
	 * We have seen a new state continuously for 500ms.
	 * Let's update the mux to enable/disable SuzyQ appropriately.
	 */
	if (count > 5) {
		if (mux_en) {
			/* Disable mux as it's disconnected now. */
			gpio_set_level(GPIO_SBU_MUX_EN, 0);
			CPRINTS("CCD: disconnected.");
		} else {
			/* SBU flip = polarity */
			write_ioexpander(2, polarity);
			gpio_set_level(GPIO_SBU_MUX_EN, 1);
			CPRINTS("CCD: connected %s",
				polarity ? "flip" : "noflip");
		}
		crec_msleep(10);
	}

	/* Measure every 100ms, forever. */
	hook_call_deferred(&ccd_measure_sbu_data, 100 * MSEC);
}

#ifdef SECTION_IS_RO
void ext_hpd_detection_enable(int enable)
{
	if (enable) {
		hpd_prev_level = gpio_get_level(GPIO_DP_HPD);
		hpd_prev_ts = get_time().val;
		gpio_enable_interrupt(GPIO_DP_HPD);
	} else {
		gpio_disable_interrupt(GPIO_DP_HPD);
	}
}
#else
void ext_hpd_detection_enable(int enable)
{
}
#endif

void ccd_enable(int enable)
{
	if (enable) {
		hook_call_deferred(&ccd_measure_sbu_data, 0);
	} else {
		gpio_set_level(GPIO_SBU_MUX_EN, 0);
		hook_call_deferred(&ccd_measure_sbu_data, -1);
	}
}

int board_get_version(void)
{
	static int ver = -1;

	if (ver < 0) {
		int tmp = 0;

		i2c_read8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_IN_PORT_B, &tmp);
		ver = (tmp >> 3) & 3;
		CPRINTS("Board ID = %d", ver);
	}

	return ver;
}

static void board_init(void)
{
	struct queue const *const qs[] = {
		&usart3_to_usb,	    &usb_to_usart3,
#ifdef SECTION_IS_RO
		&usart4_to_bridge,  &bridge_to_usart4,
		&usb_to_bridge_kbd, &bridge_kbd_to_usb,
#else
		&usart4_to_usb,
		&usb_to_usart4,
#endif
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(qs); i++)
		queue_init(qs[i]);

	/* UART init */
	usart_init(&usart3);
	usart_init(&usart4);
#ifdef SECTION_IS_RO
	usb_bridge_init();
#endif

	/* Delay DUT hub to avoid brownout. */
	crec_usleep(1000);
	gpio_set_flags(GPIO_DUT_HUB_USB_RESET_L, GPIO_OUT_HIGH);

	/*
	 * Disable USB3 mode in PS8742 USB/DP Mux.
	 */
	i2c_write8(I2C_PORT_MASTER, PS8740_I2C_ADDR0_FLAG, PS8740_REG_MODE, 0);

	/* Enable uservo USB by default. */
	init_ioexpander();
#ifdef SECTION_IS_RO
	atmel_reset_pre_write(1);
	write_ioexpander(1, 1);
	atmel_reset_post_write(1);
#else
	write_ioexpander(1, 1);
#endif

#ifdef SECTION_IS_RO
	/* Clear BBRAM, we don't want any PD state carried over on reset. */
	for (i = SYSTEM_BBRAM_IDX_PD0; i <= SYSTEM_BBRAM_IDX_PD1; i++)
		system_set_bbram(i, 0);

	/*
	 * Disable SBU mux. The polarity is set each time a presense is detected
	 * on SBU, and wired thorugh. On missing voltage on SBU. SBU wires are
	 * disconnected.
	 */
	gpio_set_level(GPIO_SBU_MUX_EN, 0);

	/*
	 * Voltage transition needs to occur in lockstep between the CHG and
	 * DUT ports, so initially limit voltage to 5V.
	 */
	pd_set_max_voltage(PD_MIN_MV);

	/* Enable VBUS detection to wake PD tasks fast enough */
	gpio_enable_interrupt(GPIO_USB_DET_PP_CHG);
	gpio_enable_interrupt(GPIO_USB_DET_PP_DUT);

	hook_call_deferred(&ccd_measure_sbu_data, 1000 * MSEC);
#endif /* SECTION_IS_RO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
