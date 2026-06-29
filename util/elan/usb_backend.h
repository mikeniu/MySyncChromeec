// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_USB_BACKEND_H_
#define UTIL_ELAN_USB_BACKEND_H_

#include <chrono>
#include <cstdint>
#include <span>

namespace elan {

struct UsbDeviceId {
  uint16_t vid;
  uint16_t pid;

  bool operator==(const UsbDeviceId&) const = default;
};

class UsbBackend {
 public:
  virtual ~UsbBackend() = default;

  virtual int Initialize() = 0;
  virtual int OpenDevice(UsbDeviceId device_id) = 0;
  virtual int InterruptTransfer(uint8_t endpoint, std::span<uint8_t> data,
                                std::chrono::milliseconds timeout) = 0;
  virtual void Release() = 0;
};

}  // namespace elan

#endif
