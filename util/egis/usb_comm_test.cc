/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <gtest/gtest.h>

#include <vector>

namespace egis {
namespace {

constexpr uint16_t kTestVid = 0x1234;
constexpr uint16_t kTestPid = 0x5678;
constexpr uint8_t kTestEpOut = 0x01;
constexpr uint8_t kTestEpIn = 0x81;

TEST(UsbCommTest, FailsWhenNotConnected) {
  UsbComm comm(nullptr, kTestVid, kTestPid, kTestEpOut, kTestEpIn);
  std::vector<uint8_t> test_data(4);
  EXPECT_EQ(comm.Send(test_data).error(), UsbError::kNotConnected);
  EXPECT_EQ(comm.Receive(test_data).error(), UsbError::kNotConnected);
}

TEST(UsbCommTest, ConnectFailsWithNullContext) {
  UsbComm comm(nullptr, kTestVid, kTestPid, kTestEpOut, kTestEpIn);
  EXPECT_EQ(comm.Connect().error(), UsbError::kInvalidParam);
}
}  // namespace
}  // namespace egis
