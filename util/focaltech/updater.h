/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_UPDATER_H_
#define UTIL_FOCALTECH_UPDATER_H_

#include <assert.h>

#include <cstdint>
#include <expected>
#include <string_view>

#include "ft_util.h"
#include "usb_device.h"

// Forward declarations
class UsbDevice;

// This configuration item is not used during the upgrade; it is only used as an
// address offset.
constexpr size_t kFtConfigReservedSize = 28;

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
  uint8_t reserved[kFtConfigReservedSize];
  uint32_t code_start_address;
} __attribute__((packed));

static_assert(sizeof(FtConfigPage) == 36,
              "FtConfigPage must be exactly 36 bytes");

constexpr size_t kFtFlashWpBufferSize = 512;
// FocalTech vendor-specific SCSI command opcodes
constexpr uint8_t kFtCmdReadInfo = 0xDB;
constexpr uint8_t kFtCmdAction = 0xDC;

// Sub-commands for kFtCmdAction
constexpr uint8_t kFtSubcmdReadData = 0x01;
constexpr uint8_t kFtSubcmdWriteBin = 0x03;
constexpr uint8_t kFtSubcmdCodeBin = 0x04;
constexpr uint8_t kFtSubcmdErasePage = 0x08;
constexpr uint8_t kFtSubcmdVerify = 0xBB;
constexpr uint8_t kFtSubcmdSoftReset = 0xDD;
constexpr uint8_t kFtSubcmdFlashWpSet = 0xF7;
constexpr uint8_t kFtSubcmdFlashWpGet = 0xF8;

constexpr size_t kFtBootVersionMax = 36;
constexpr uint32_t kFtWriteSramAddr = 0x20002000;
constexpr size_t kFtEcBinSizeMax = 2 * 1024 * 1024;

constexpr size_t kFocalConfigSize = 0x1000;
constexpr uint32_t kFocalRomCfgAddr = 0x10000000;
constexpr uint32_t kFocalBootCfgAddr = 0x10080000;
constexpr uint32_t kFocalRomRunAddr = 0x10002000;
constexpr uint32_t kFocalBootRunAddr = 0x10081000;

constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

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

#endif  // UTIL_FOCALTECH_UPDATER_H_
