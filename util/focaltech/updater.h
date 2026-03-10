/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_UPDATER_H_
#define UTIL_FOCALTECH_UPDATER_H_

#include <cstdint>
#include <expected>
#include <string_view>

#include "ft_util.h"
#include "usb_device.h"

// Forward declarations
class UsbDevice;

enum class FtFlashWpArea {
  kUnknown,
  kAll,
  kBoot,
  kRo,
  kRw,
  kOutRange,
};

struct FtConfigPage {
  uint32_t code_valid_control_word;
  uint8_t reserved[28];
  uint32_t code_start_address;
} __attribute__((packed));

// ----------------------------------------------------------------------------
// Firmware update
// ----------------------------------------------------------------------------
std::expected<void, FtError> UpdateFw(const UsbDevice& device,
                                      FtDeviceInfo& info);

// ----------------------------------------------------------------------------
// Flash write-protect operations (valid only in bootloader mode)
// ----------------------------------------------------------------------------
std::expected<FtFlashWpArea, FtError> FtGetFlashProtect(
    const UsbDevice& device, FtDeviceMode current_mode);

std::expected<void, FtError> FtSetFlashProtect(const UsbDevice& device,
                                               FtFlashWpArea wp_area,
                                               FtDeviceMode current_mode);

// ----------------------------------------------------------------------------
// Switch device back to ROM boot mode (erase config page and reset)
// ----------------------------------------------------------------------------
std::expected<void, FtError> FtBackToRomBoot(const UsbDevice& device,
                                             FtDeviceMode current_mode);

// ----------------------------------------------------------------------------
// Verification utility (exposed for testing if needed)
// ----------------------------------------------------------------------------
std::expected<void, FtError> FtVerifyData(const UsbDevice& device,
                                          std::string_view file_path,
                                          uint32_t target_addr);

#endif  // UTIL_FOCALTECH_UPDATER_H_
