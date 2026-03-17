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

// Project headers
#include "usb_interface.h"

namespace egis {

class UsbComm : public UsbInterface {
 public:  // Require the caller to provide the context
  UsbComm(libusb_context *ctx, uint16_t vid, uint16_t pid, uint8_t ep_out,
          uint8_t ep_in);
  ~UsbComm() override;

  UsbComm(const UsbComm &) = delete;
  UsbComm &operator=(const UsbComm &) = delete;

  std::expected<void, UsbError> Connect() override;
  std::expected<void, UsbError> Send(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout) override;
  std::expected<int, UsbError> Receive(
      std::span<uint8_t> data,
      std::chrono::milliseconds timeout = kTransferTimeout) override;

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
