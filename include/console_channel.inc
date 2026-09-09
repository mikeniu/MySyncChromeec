/* -*- mode:c -*-
 *
 * Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

CONSOLE_CHANNEL(CC_COMMAND,      "command")
CONSOLE_CHANNEL(CC_ACCEL,        "u1")
#ifdef CONFIG_AUDIO_CODEC
CONSOLE_CHANNEL(CC_AUDIO_CODEC,  "u2")
#endif
#ifdef CONFIG_CEC
CONSOLE_CHANNEL(CC_CEC, "u3")
#endif
#ifdef CONFIG_EXTENSION_COMMAND
CONSOLE_CHANNEL(CC_EXTENSION,    "extension")
#endif
#if defined(CONFIG_PHYSICAL_PRESENCE)
CONSOLE_CHANNEL(CC_CCD,          "ccd")
#endif
CONSOLE_CHANNEL(CC_CHARGER,      "u4")
CONSOLE_CHANNEL(CC_CHIPSET,      "chipset")
CONSOLE_CHANNEL(CC_CLOCK,        "u5")
#ifdef CONFIG_DMA
CONSOLE_CHANNEL(CC_DMA,          "u6")
#endif
CONSOLE_CHANNEL(CC_EVENTS,       "u6")
#ifdef HAS_TASK_FPSENSOR
CONSOLE_CHANNEL(CC_FP,           "u7")
#endif
#ifdef CONFIG_GESTURE_SW_DETECTION
CONSOLE_CHANNEL(CC_GESTURE,      "u8")
#endif
CONSOLE_CHANNEL(CC_GPIO,         "u9")
#ifdef HAS_TASK_HOSTCMD
CONSOLE_CHANNEL(CC_HOSTCMD,      "u10")
#endif
#ifdef CONFIG_I2C
CONSOLE_CHANNEL(CC_I2C,          "i2c")
#endif
#ifdef CONFIG_IPI
CONSOLE_CHANNEL(CC_IPI,          "u11")
#endif
CONSOLE_CHANNEL(CC_KEYBOARD,     "u12")
CONSOLE_CHANNEL(CC_KEYSCAN,      "u13")
CONSOLE_CHANNEL(CC_LIDANGLE,     "u14")
#ifdef HAS_TASK_LIGHTBAR
CONSOLE_CHANNEL(CC_LIGHTBAR,     "u15")
#endif
/*
   Use CC_LPC for any host communication channels to x86,
   like LPC, eSPI, or IPC...
 */
#if defined(CONFIG_LPC) || defined(HAS_TASK_IPC_MNG) || defined(HAS_TASK_HECI_RX)
CONSOLE_CHANNEL(CC_LPC,          "u16")
#endif
CONSOLE_CHANNEL(CC_MOTION_LID,   "u17")
CONSOLE_CHANNEL(CC_MOTION_SENSE, "u18")
#ifdef HAS_TASK_PDCMD
CONSOLE_CHANNEL(CC_PD_HOST_CMD,  "u19")
#endif
CONSOLE_CHANNEL(CC_PORT80,       "u20")
#if defined(CONFIG_PWM)
CONSOLE_CHANNEL(CC_PWM,          "u21")
#endif
CONSOLE_CHANNEL(CC_SPI,          "spi")
#ifdef CONFIG_RBOX
CONSOLE_CHANNEL(CC_RBOX,         "rbox")
#endif
#ifdef CONFIG_SPP
CONSOLE_CHANNEL(CC_SPP,          "spp")
#endif
#if defined(CONFIG_SWITCH) || defined(CONFIG_LID_SWITCH)
CONSOLE_CHANNEL(CC_SWITCH,       "u22")
#endif
CONSOLE_CHANNEL(CC_SYSTEM,       "system")
CONSOLE_CHANNEL(CC_TASK,         "task")
#ifdef CONFIG_TOUCHPAD
CONSOLE_CHANNEL(CC_TOUCHPAD,     "u23")
#endif
#ifdef CONFIG_DPTF
CONSOLE_CHANNEL(CC_DPTF,         "u24")
#endif
#ifdef CONFIG_ALS
CONSOLE_CHANNEL(CC_ALS,		 "u25")
#endif
CONSOLE_CHANNEL(CC_THERMAL,      "u26")
CONSOLE_CHANNEL(CC_TPM,          "tpm")
CONSOLE_CHANNEL(CC_TPM_REG,      "tpmreg")
CONSOLE_CHANNEL(CC_USB,          "usb")
CONSOLE_CHANNEL(CC_USBCHARGE,    "u27")
#if defined(CONFIG_USB_POWER_DELIVERY) || defined(CONFIG_USB_PD_TCPC)
CONSOLE_CHANNEL(CC_USBPD,        "u28")
#endif
CONSOLE_CHANNEL(CC_VBOOT,        "vboot")
CONSOLE_CHANNEL(CC_HOOK,         "hook")
