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
#include "usb_transport.h"

enum class FtDeviceMode {
  kUnknown,
  kRom,
  kBoot,
  kTest,
  kEc,
  kForceUpdate,
};

struct FtDeviceInfo {
  FtDeviceMode current_mode = FtDeviceMode::kUnknown;
  FtDeviceMode next_mode = FtDeviceMode::kUnknown;
  std::string_view ec_bin;
};

class UsbDevice {
 public:
  // "Inject" the transport dependency here.
  explicit UsbDevice(std::unique_ptr<UsbTransport> transport)
      : transport_(std::move(transport)),
        cdb_tag_(std::make_unique<std::atomic<uint32_t>>(1)) {}

  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) const {
    return transport_->Send(data, timeout);
  }
  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds timeout) const {
    return transport_->Recv(data, timeout);
  }
  std::expected<void, FtError> WaitForReset(
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

#endif  // UTIL_FOCALTECH_USB_DEVICE_H_
