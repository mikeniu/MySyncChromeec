/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_MOCK_USB_TRANSPORT_H_
#define UTIL_FOCALTECH_MOCK_USB_TRANSPORT_H_

#include <gmock/gmock.h>

#include "usb_transport.h"

namespace focaltech::testing {

class MockUsbTransport : public UsbTransport {
 public:
  MOCK_METHOD(UsbDeviceId, device_id, (), (const, override));
  MOCK_METHOD((std::expected<size_t, Error>), Send,
              (std::span<const uint8_t>, std::chrono::milliseconds),
              (override));
  MOCK_METHOD((std::expected<size_t, Error>), Recv,
              (std::span<uint8_t>, std::chrono::milliseconds), (override));
  MOCK_METHOD((std::expected<void, Error>), WaitForReset,
              (std::chrono::milliseconds), (override));
};

}  // namespace focaltech::testing

#endif  // UTIL_FOCALTECH_MOCK_USB_TRANSPORT_H_
