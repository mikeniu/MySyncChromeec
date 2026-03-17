/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_USB_INTERFACE_H_
#define UTIL_EGIS_USB_INTERFACE_H_

#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace egis {

static constexpr std::chrono::milliseconds kTransferTimeout =
    std::chrono::milliseconds(500);

enum class UsbError {
  kSuccess = 0,
  kTimeout,
  kPipe,
  kOverflow,
  kNoDevice,
  kBusy,
  kInvalidParam,
  kAccessDenied,
  kIoError,
  kNotSupported,
  kNotConnected,
  kOther,
};

constexpr std::string_view ToString(UsbError err) {
  switch (err) {
    case UsbError::kTimeout:
      return "Operation timed out";
    case UsbError::kPipe:
      return "Control request failed (pipe error)";
    case UsbError::kOverflow:
      return "Device offered more data than expected";
    case UsbError::kNoDevice:
      return "Device not found or has been disconnected";
    case UsbError::kBusy:
      return "Device or resource is busy";
    case UsbError::kNotConnected:
      return "Not connected to a device";
    case UsbError::kAccessDenied:
      return "Access denied (permission issue)";
    default:
      return "An unknown USB error occurred";
  }
}
class UsbInterface {
 public:
  virtual ~UsbInterface() = default;

  virtual std::expected<void, UsbError> Connect() = 0;

  virtual std::expected<void, UsbError> Send(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout) = 0;

  virtual std::expected<int, UsbError> Receive(
      std::span<uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout) = 0;
};

}  // namespace egis

#endif  // UTIL_EGIS_USB_INTERFACE_H_
