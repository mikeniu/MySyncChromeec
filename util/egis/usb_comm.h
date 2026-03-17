/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_USB_COMM_H_
#define UTIL_EGIS_USB_COMM_H_

// C++ system files
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Other libraries' .h files
#include <libusb-1.0/libusb.h>

namespace egis {

constexpr int kInterfaceNumber = 0;

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

class UsbComm {
 public:  // Require the caller to provide the context
  UsbComm(libusb_context *ctx, uint16_t vid, uint16_t pid, uint8_t ep_out,
          uint8_t ep_in);
  ~UsbComm();

  UsbComm(const UsbComm &) = delete;
  UsbComm &operator=(const UsbComm &) = delete;

  std::expected<void, UsbError> Connect();
  std::expected<void, UsbError> Send(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout);
  std::expected<int, UsbError> Receive(
      std::span<uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout);

 private:
  libusb_context *ctx_ = nullptr;
  std::unique_ptr<libusb_device_handle, decltype(&libusb_close)> handle_{
      nullptr, &libusb_close};
  uint16_t vid_;
  uint16_t pid_;
  bool kernel_driver_detached_ = false;
  uint8_t ep_out_;
  uint8_t ep_in_;
};

}  // namespace egis

#endif  // UTIL_EGIS_USB_COMM_H_
