/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_UPDATER_H_
#define UTIL_FOCALTECH_UPDATER_H_

#include <openssl/sha.h>

#include <cassert>
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

// Flash write protection areas.
// The numeric values correspond to the underlying hardware values.
enum class FlashWpArea : uint32_t {
  kUnknown,
  kNone,        // Disable all write protection
  kBoot,        // Protect bootloader only
  kRo,          // Protect bootloader + RO region
  kAll,         // Protect the entire flash (Bootloader, RO, and RW)
  kOutOfRange,  // Boundary marker for validation
};

// Convert a FlashWpArea to its user-facing string representation.
constexpr std::string_view ToString(FlashWpArea area) {
  switch (area) {
    case FlashWpArea::kNone:
      return "none";
    case FlashWpArea::kBoot:
      return "boot";
    case FlashWpArea::kRo:
      return "ro";
    case FlashWpArea::kAll:
      return "all";
    default:
      return "unknown";
  }
}

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
constexpr size_t kFirmwareBinSizeMax = 2 * 1024 * 1024;

constexpr size_t kFocalConfigSize = 0x1000;

// Hardware memory addresses for ROM and Bootloader modes.

constexpr uint32_t kRomConfigAddr = 0x10000000;
constexpr uint32_t kBootConfigAddr = 0x10080000;
constexpr uint32_t kRomRunAddr = 0x10002000;
constexpr uint32_t kBootRunAddr = 0x10081000;

struct MemoryMap {
  uint32_t config_address;
  uint32_t run_address;
};

constexpr std::expected<MemoryMap, Error> GetMemoryMap(DeviceMode mode) {
  switch (mode) {
    case DeviceMode::kRom:
      return MemoryMap{.config_address = kRomConfigAddr,
                       .run_address = kRomRunAddr};
    case DeviceMode::kBootloader:
      return MemoryMap{.config_address = kBootConfigAddr,
                       .run_address = kBootRunAddr};
    default:
      return std::unexpected(Error::kInvalidMode);
  }
}

constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

std::expected<void, Error> UpdateFirmware(
    const UsbDevice& device, std::span<const uint8_t> firmware_data,
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
