/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Divided Vconn voltage sense */
GPIO(VCONN_VSENSE,        PIN(A, 0),  GPIO_ANALOG)
/* CC ADC, PD in comparator, or tx enable out (low) */
GPIO(CC_VPDMCU,           PIN(A, 1),  GPIO_ANALOG)
/* CC 0.2V comparator during charge-through, active Rd (low) or Rp3A0 (high) */
GPIO(CC_RP3A0_RD_L,       PIN(A, 2),  GPIO_ANALOG)
/* 0.2V resistor divider for various CC comapators */
GPIO(RDCONNECT_REF,       PIN(A, 3),  GPIO_ANALOG)
/*
 * Charger CC1 0.2V comparator and ADC, drive a Rp1A5 (high) or
 * partial Rd (low)
 */
GPIO(CC1_RP1A5_ODH,       PIN(A, 4),  GPIO_ANALOG)
/*
 * Charger CC2 0.2V comparator and ADC, drive a Rp1A5 (high) or
 * partial Rd (low)
 */
GPIO(CC2_RP1A5_ODH,       PIN(A, 5),  GPIO_ANALOG)
/* Divided host VBUS voltage sense */
GPIO(HOST_VBUS_VSENSE,    PIN(A, 6),  GPIO_ANALOG)
/* Divided charger VBUS voltage sense */
GPIO(CHARGER_VBUS_VSENSE, PIN(A, 7),  GPIO_ANALOG)
/* Charger CC1 ADC, or drive a RpUSB (high) */
GPIO(CC1_RPUSB_ODH,       PIN(B, 0), GPIO_ANALOG)
/* Charger CC2 ADC, or drive a RpUSB (high) */
GPIO(CC2_RPUSB_ODH,       PIN(B, 1), GPIO_ANALOG)

/* PD TX data output */
GPIO(CC_TX_DATA,          PIN(B, 4), GPIO_INPUT)

/* Enables the VBUS pass-through (high) */
GPIO(VBUS_PASS_EN,        PIN(B, 2), GPIO_OUT_LOW)

/*
 * Desired billboard state. One of "no billboard/nothing connected" (low),
 * "source connected but not in charge-through" (pull-up), or "sink connected"
 * (high)
 */
GPIO(PRESENT_BILLBOARD,   PIN(A, 8),  GPIO_OUT_LOW)

/* Enables cReceiver and the path to the PD RX/TX, RpUSB, and Rp1A5 */
GPIO(VPDMCU_CC_EN,        PIN(A, 11),  GPIO_OUT_LOW)

/* Disables dead battery Rd on host side (low) */
GPIO(CC_DB_EN_OD,         PIN(A, 12),  GPIO_ODR_HIGH)

/* RpUSB on host side (high) */
GPIO(CC_RPUSB_ODH,        PIN(A, 13),  GPIO_INPUT)

/* Rp1A5 on host side (high) */
GPIO(CC_RP1A5_ODH,        PIN(A, 14),  GPIO_INPUT)

/* Controls the pull-downs on charger side; either Rd (low) or Hi-Z (high) */
GPIO(CC1_CC2_RD_L,        PIN(A, 15),  GPIO_OUT_LOW)

/* Chooses between Vconn (low) and VBUS (high) */
GPIO(VCONN_PWR_SEL_ODL,   PIN(B, 6), GPIO_INPUT)

/* Passes CC1 to the host CC (high) */
GPIO(CC1_SEL,             PIN(F, 0), GPIO_OUT_LOW)
/* Passes CC2 to the host CC (high) */
GPIO(CC2_SEL,             PIN(F, 1), GPIO_OUT_LOW)
/* Debug red LED driver (low). Keep off for power measurements */
GPIO(DEBUG_LED_R_L,       PIN(B, 5), GPIO_ODR_HIGH)
/* Debug green LED driver (low). Keep off for power measurements */
/* TODO(shurst): Change to B7 on next rev of board */
GPIO(DEBUG_LED_G_L,       PIN(B, 8), GPIO_ODR_HIGH)

UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(ENTERING_RW)

/* SCK(PB3): PD_TX_CLK_IN - Clock input for PD TX */
ALTERNATE(PIN_MASK(B, 0x0008), 0, MODULE_USB_PD, 0)
/* TIM17_CH1N(PB7): PD_TX_CLK_OUT - Clock generator for PD TX */
ALTERNATE(PIN_MASK(B, 0x0080), 2, MODULE_USB_PD, 0)
/* USART1 (PA9/PA10): TX/RX for debug and programming */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   0)
