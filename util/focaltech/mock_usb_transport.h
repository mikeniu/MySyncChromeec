/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <gmock/gmock.h>

#include "usb_transport.h"

class MockUsbTransport : public UsbTransport {
 public:
  MOCK_METHOD(UsbDeviceId, device_id, (), (const, override));
  MOCK_METHOD((std::expected<int, FtError>), Send,
              (std::span<const uint8_t>, std::chrono::milliseconds),
              (override));
  MOCK_METHOD((std::expected<int, FtError>), Recv,
              (std::span<uint8_t>, std::chrono::milliseconds), (override));
  MOCK_METHOD((std::expected<void, FtError>), WaitForReset,
              (std::chrono::milliseconds), (override));
};
