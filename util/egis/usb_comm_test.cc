/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_comm.h"

#include <vector>

#include <gtest/gtest.h>

namespace egis {
namespace {

constexpr UsbDeviceProfile kTestProfile = {
    .id =
        {
            .vid = 0x1234,
            .pid = 0x5678,
        },
    .ep_out = 0x01,
    .ep_in = 0x81,
};

TEST(UsbCommTest, FailsWhenNotConnected) {
  UsbComm comm(nullptr, kTestProfile);
  std::vector<uint8_t> test_data(4);
  EXPECT_EQ(comm.Send(test_data).error(), UsbError::kNotConnected);
  EXPECT_EQ(comm.Receive(test_data).error(), UsbError::kNotConnected);
}

TEST(UsbCommTest, ConnectFailsWithNullContext) {
  UsbComm comm(nullptr, kTestProfile);
  EXPECT_EQ(comm.Connect().error(), UsbError::kInvalidParam);
}
}  // namespace
}  // namespace egis
