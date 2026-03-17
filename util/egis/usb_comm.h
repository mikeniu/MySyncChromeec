/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_USB_COMM_H_
#define UTIL_EGIS_USB_COMM_H_

// C++ system files
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Other libraries' .h files
#include <libusb-1.0/libusb.h>

constexpr int kInterfaceNumber = 0;
constexpr uint8_t kEpOutBulk = 0x01;  // Bulk OUT
constexpr uint8_t kEpInBulk = 0x81;   // Bulk IN
constexpr uint8_t kEpInInt = 0x82;    // Interrupt IN

static constexpr int kTransferTimeoutMs = 500;
static constexpr uint8_t kResponseReadyIrq = 1;

class UsbComm {
 public:  // Require the caller to provide the context
  UsbComm(libusb_context *ctx, uint16_t vid, uint16_t pid);
  ~UsbComm();

  UsbComm(const UsbComm &) = delete;
  UsbComm &operator=(const UsbComm &) = delete;

  int BulkWrite(uint8_t endpoint, std::span<const uint8_t> data,
                int timeout_ms);
  int BulkRead(uint8_t endpoint, std::span<uint8_t> data, int timeout_ms);

  int Connect();
  bool Send(std::span<const uint8_t> data);
  bool Wait();
  int Receive(std::span<uint8_t> data);
  void SetIds(uint16_t vid, uint16_t pid);

 private:
  libusb_context *ctx_ = nullptr;
  libusb_device_handle *handle_ = nullptr;
  uint16_t vid_;
  uint16_t pid_;
  bool kernel_driver_detached_ = false;
};

#endif  // UTIL_EGIS_USB_COMM_H_
