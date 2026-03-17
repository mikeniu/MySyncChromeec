/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <gtest/gtest.h>

#include <vector>

namespace egis {
namespace {
TEST(UsbCommTest, FailsWhenNotConnected) {
  UsbComm comm(nullptr, 0x1234, 0x5678, 0x01, 0x81);
  std::vector<uint8_t> test_data(4);
  EXPECT_EQ(comm.Send(test_data).error(), UsbError::kNotConnected);
  EXPECT_EQ(comm.Receive(test_data).error(), UsbError::kNotConnected);
}

TEST(UsbCommTest, ConnectFailsWithNullContext) {
  UsbComm comm(nullptr, 0x1234, 0x5678, 0x01, 0x81);
  EXPECT_EQ(comm.Connect().error(), UsbError::kInvalidParam);
}
}  // namespace
}  // namespace egis
