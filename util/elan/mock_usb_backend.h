// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_MOCK_USB_BACKEND_H_
#define UTIL_ELAN_MOCK_USB_BACKEND_H_

#include <gmock/gmock.h>

#include "usb_backend.h"

namespace elan {

class MockUsbBackend : public UsbBackend {
 public:
  MOCK_METHOD(int, Initialize, (), (override));
  MOCK_METHOD(int, OpenDevice, (UsbDeviceId device_id), (override));
  MOCK_METHOD(int, InterruptTransfer,
              (uint8_t endpoint, std::span<uint8_t> data,
               std::chrono::milliseconds timeout),
              (override));
  MOCK_METHOD(void, Release, (), (override));
};

}  // namespace elan

#endif
