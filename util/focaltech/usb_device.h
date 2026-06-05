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

enum class DeviceMode {
  kRom,
  kBoot,
  kTest,
  kEc,
};

using namespace std::chrono_literals;
inline constexpr auto kDeviceRebootTimeout = 3s;

inline std::expected<DeviceMode, Error> GetDeviceMode(const UsbDeviceId& id) {
  if (id == kFocalRomId) return DeviceMode::kRom;
  if (id == kFocalBootId) return DeviceMode::kBoot;
  if (id == kFocalEcTestId) return DeviceMode::kTest;
  if (id == kFocalEcPid9865Id || id == kFocalEcPid9869Id)
    return DeviceMode::kEc;
  return std::unexpected(Error::kNoSuchDevice);
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
