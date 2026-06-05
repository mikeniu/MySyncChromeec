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

namespace focaltech {

class UsbDevice;

// This configuration item is not used during the upgrade; it is only used as an
// address offset.
constexpr size_t kConfigReservedSize = 28;

enum class FlashWpArea {
  kUnknown,
  kAll,
  kBoot,
  kRo,
  kRw,
  kOutRange,
};

struct ConfigPage {
  uint32_t code_valid_control_word;
  uint8_t reserved[kConfigReservedSize];
  uint32_t code_start_address;
} __attribute__((packed));

static_assert(sizeof(ConfigPage) == 36, "ConfigPage must be exactly 36 bytes");

constexpr size_t kFlashWpBufferSize = 512;
constexpr uint8_t kCmdReadInfo = 0xDB;
constexpr uint8_t kCmdAction = 0xDC;

constexpr uint8_t kSubcmdReadData = 0x01;
constexpr uint8_t kSubcmdWriteBin = 0x03;
constexpr uint8_t kSubcmdCodeBin = 0x04;
constexpr uint8_t kSubcmdErasePage = 0x08;
constexpr uint8_t kSubcmdVerify = 0xBB;
constexpr uint8_t kSubcmdSoftReset = 0xDD;
constexpr uint8_t kSubcmdFlashWpSet = 0xF7;
constexpr uint8_t kSubcmdFlashWpGet = 0xF8;

constexpr size_t kBootVersionMax = 36;
constexpr uint32_t kWriteSramAddr = 0x20002000;
constexpr size_t kEcBinSizeMax = 2 * 1024 * 1024;

constexpr size_t kFocalConfigSize = 0x1000;
constexpr uint32_t kRomCfgAddr = 0x10000000;
constexpr uint32_t kBootCfgAddr = 0x10080000;
constexpr uint32_t kRomRunAddr = 0x10002000;
constexpr uint32_t kBootRunAddr = 0x10081000;

constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

std::expected<void, Error> UpdateFw(const UsbDevice& device,
                                    std::string_view ec_bin,
                                    DeviceMode current_mode);

std::expected<FlashWpArea, Error> GetFlashProtection(const UsbDevice& device,
                                                     DeviceMode current_mode);

std::expected<void, Error> SetFlashProtection(const UsbDevice& device,
                                              FlashWpArea wp_area,
                                              DeviceMode current_mode);

std::expected<void, Error> ReturnToRomBoot(const UsbDevice& device,
                                           DeviceMode current_mode);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_UPDATER_H_
