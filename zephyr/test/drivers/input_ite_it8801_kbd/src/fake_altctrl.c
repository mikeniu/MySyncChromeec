/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file fake_altctrl.c
 * @brief Stub devices for ite,it8801-altctrl DT nodes
 *
 * The real altctrl driver is not compiled (CONFIG_MFD_ITE_IT8801=n),
 * but DEVICE_DT_GET references in the KBD driver's mfdctrl macros
 * require device structs to exist.
 *
 * This file creates minimal stub devices for any it8801-altctrl nodes
 * in the device tree, allowing the test to use mfdctrl configurations.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>

#define DT_DRV_COMPAT ite_it8801_altctrl

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
