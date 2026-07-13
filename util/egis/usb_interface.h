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

enum class UsbError {
  kTimeout,
  kPipe,
  kOverflow,
  kNoDevice,
  kBusy,
  kInvalidParam,
  kAccessDenied,
  kIoError,
  kNotConnected,
  kTransferFailed,
  kOther,
};

constexpr std::string_view ToString(UsbError err) {
  switch (err) {
    case UsbError::kTimeout:
      return "Operation timed out";
    case UsbError::kPipe:
      return "Pipe error";
    case UsbError::kOverflow:
      return "Overflow";
    case UsbError::kNoDevice:
      return "No such device";
    case UsbError::kBusy:
      return "Device or resource busy";
    case UsbError::kInvalidParam:
      return "Invalid parameter";
    case UsbError::kAccessDenied:
      return "Access denied";
    case UsbError::kIoError:
      return "I/O error";
    case UsbError::kNotConnected:
      return "Not connected";
    case UsbError::kTransferFailed:
      return "Transfer failed";
    case UsbError::kOther:
      return "Other USB error";
  }
  return "Unknown USB error";
}

class UsbInterface {
 public:
  static constexpr auto kDefaultTimeout = std::chrono::milliseconds(1000);

  virtual ~UsbInterface() = default;

  virtual std::expected<void, UsbError> Connect() = 0;

  // Non-virtual public API (Safe default arguments)
  std::expected<void, UsbError> Send(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout = kDefaultTimeout) {
    return DoSend(data, timeout);
  }

  std::expected<int, UsbError> Receive(
      std::span<uint8_t> rx_buffer,
      std::chrono::milliseconds timeout = kDefaultTimeout) {
    return DoReceive(rx_buffer, timeout);
  }

 protected:
  // Pure virtual implementations (No default arguments)
  virtual std::expected<void, UsbError> DoSend(
      std::span<const uint8_t> data, std::chrono::milliseconds timeout) = 0;

  virtual std::expected<int, UsbError> DoReceive(
      std::span<uint8_t> rx_buffer, std::chrono::milliseconds timeout) = 0;
};

}  // namespace egis

#endif  // UTIL_EGIS_USB_INTERFACE_H_
