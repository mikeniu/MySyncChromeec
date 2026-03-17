/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_MOCK_USB_COMM_H_
#define UTIL_EGIS_MOCK_USB_COMM_H_

#include <gmock/gmock.h>

#include "usb_interface.h"

namespace egis {
class MockUsbComm : public UsbInterface {
 public:
  MOCK_METHOD((std::expected<void, UsbError>), Connect, (), (override));
  MOCK_METHOD((std::expected<void, UsbError>), Send,
              (std::span<const uint8_t> data,
               std::chrono::milliseconds timeout),
              (override));
  MOCK_METHOD((std::expected<int, UsbError>), Receive,
              (std::span<uint8_t> data, std::chrono::milliseconds timeout),
              (override));
};
}  // namespace egis
#endif  // UTIL_EGIS_MOCK_USB_COMM_H_
