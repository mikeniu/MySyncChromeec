/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_UPDATER_H_
#define UTIL_FOCALTECH_UPDATER_H_

#include <assert.h>
#include <openssl/sha.h>

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

enum class FlashWpArea : uint32_t {
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

constexpr size_t kBootVersionMax = 22;
constexpr size_t kRomVersionMax = 36;
constexpr uint32_t kWriteSramAddr = 0x20002000;
constexpr size_t kEcBinSizeMax = 2 * 1024 * 1024;

constexpr size_t kFocalConfigSize = 0x1000;
constexpr uint32_t kRomCfgAddr = 0x10000000;
constexpr uint32_t kBootCfgAddr = 0x10080000;
constexpr uint32_t kRomRunAddr = 0x10002000;
constexpr uint32_t kBootRunAddr = 0x10081000;

constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

std::expected<void, Error> UpdateFw(const UsbDevice& device,
                                    std::span<const uint8_t> fw_data,
                                    DeviceMode current_mode);

std::expected<FlashWpArea, Error> GetFlashProtection(const UsbDevice& device,
                                                     DeviceMode current_mode);

std::expected<void, Error> SetFlashProtection(const UsbDevice& device,
                                              FlashWpArea wp_area,
                                              DeviceMode current_mode);

std::expected<void, Error> ReturnToRomBoot(const UsbDevice& device,
                                           DeviceMode current_mode);

std::expected<void, Error> GetBootVersion(const UsbDevice& device,
                                          DeviceMode current_mode);

inline std::array<uint8_t, SHA256_DIGEST_LENGTH> CalculateSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_UPDATER_H_
