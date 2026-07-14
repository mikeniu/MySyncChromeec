// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_LIBUSB_BACKEND_H_
#define UTIL_ELAN_LIBUSB_BACKEND_H_

#include <libusb-1.0/libusb.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

#include "usb_backend.h"

namespace elan {

class LibusbBackend : public UsbBackend {
 public:
  LibusbBackend();
  ~LibusbBackend() override;

  // Prevent copying/moving to safely manage raw handles
  LibusbBackend(const LibusbBackend&) = delete;
  LibusbBackend& operator=(const LibusbBackend&) = delete;

  int Initialize() override;
  int OpenDevice(UsbDeviceId device_id) override;
  int InterruptTransfer(uint8_t endpoint, std::span<uint8_t> data,
                        std::chrono::milliseconds timeout) override;
  void Release() override;

 private:
  libusb_device_handle* dev_handle_ = nullptr;
  bool libusb_initialized_ = false;
  std::vector<int> claimed_interfaces_;
  std::vector<int> detached_kernel_drivers_;
};

}  // namespace elan

#endif
