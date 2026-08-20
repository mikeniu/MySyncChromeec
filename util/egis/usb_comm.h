/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_USB_COMM_H_
#define UTIL_EGIS_USB_COMM_H_
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <libusb-1.0/libusb.h>

#include "usb_interface.h"

namespace egis {

struct UsbDeviceId {
  uint16_t vid;
  uint16_t pid;

  bool operator==(const UsbDeviceId&) const = default;
};

struct UsbDeviceProfile {
  UsbDeviceId id;
  uint8_t ep_out;
  uint8_t ep_in;
};

class UsbComm : public UsbInterface {
 public:
  UsbComm(libusb_context* ctx, const UsbDeviceProfile& profile);
  ~UsbComm() override;

  UsbComm(const UsbComm&) = delete;
  UsbComm& operator=(const UsbComm&) = delete;

  std::expected<void, UsbError> Connect() override;

 protected:
  std::expected<void, UsbError> DoSend(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout) override;
  std::expected<int, UsbError> DoReceive(
      std::span<uint8_t> rx_buffer, std::chrono::milliseconds timeout) override;

 private:
  std::expected<int, UsbError> ExecuteBulkTransfer(
      uint8_t ep, uint8_t* data, int length, std::chrono::milliseconds timeout);

  libusb_context* ctx_ = nullptr;
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
