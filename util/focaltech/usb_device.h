/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_USB_DEVICE_H_
#define UTIL_FOCALTECH_USB_DEVICE_H_

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>

#include "ft_util.h"
#include "libusb_transport.h"
#include "usb_transport.h"

namespace focaltech {

// Represents the current execution state of the Focaltech MCU.
enum class DeviceMode {
  // Hardwired mask ROM. Cannot be overwritten. Used for safe recovery.
  kRom,
  // Updatable flash bootloader. Used for normal firmware updates.
  kBootloader,
};

using namespace std::chrono_literals;
inline constexpr auto kDeviceRebootTimeout = 3s;

inline std::expected<DeviceMode, Error> GetDeviceMode(const UsbDeviceId& id) {
  if (id == kFocalRomId) return DeviceMode::kRom;
  if (id == kFocalBootId) return DeviceMode::kBootloader;
  return std::unexpected(Error::kDeviceNotFound);
}

class UsbDevice {
 public:
  explicit UsbDevice(std::unique_ptr<UsbTransport> transport)
      : transport_(std::move(transport)),
        cdb_tag_(std::make_unique<std::atomic<uint32_t>>(1)) {}

  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds timeout) const {
    return transport_->Send(data, timeout);
  }
  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds timeout) const {
    return transport_->Recv(data, timeout);
  }
  std::expected<void, Error> WaitForReset(
      std::chrono::milliseconds timeout) const {
    return transport_->WaitForReset(timeout);
  }

  UsbDeviceId device_id() const { return transport_->device_id(); }
  uint32_t GetNextCdbTag() const {
    return cdb_tag_->fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::unique_ptr<UsbTransport> transport_;
  mutable std::unique_ptr<std::atomic<uint32_t>> cdb_tag_;
};

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_USB_DEVICE_H_
