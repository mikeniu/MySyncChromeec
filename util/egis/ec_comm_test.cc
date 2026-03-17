/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_comm.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

#include "mock_usb_comm.h"

namespace egis {
namespace {
using ::testing::_;
using ::testing::Return;

TEST(EcCommTest, SendCommandSuccess) {
  MockUsbComm mock_usb;
  EcComm ec_comm(mock_usb);

  EcHostCommandResponse mock_resp{
      .protocol_version = 3,
      .checksum = static_cast<uint8_t>(-3),
      .result = ToLittleEndian(static_cast<uint16_t>(0)),
      .data_length = ToLittleEndian(static_cast<uint16_t>(0))};

  EXPECT_CALL(mock_usb, Send(_, _))
      .WillOnce(Return(std::expected<void, UsbError>{}));
  EXPECT_CALL(mock_usb, Receive(_, _))
      .WillOnce([&mock_resp](std::span<uint8_t> out_buffer, auto) {
        auto bytes = AsBytes(mock_resp);
        std::ranges::copy(bytes, out_buffer.begin());
        return static_cast<int>(bytes.size());
      });

  auto result =
      ec_comm.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST(EcCommTest, SendCommandInvalidChecksum) {
  MockUsbComm mock_usb;
  EcComm ec_comm(mock_usb);

  EcHostCommandResponse mock_resp{
      .protocol_version = 3, .checksum = 0, .result = 0, .data_length = 0};

  EXPECT_CALL(mock_usb, Send(_, _))
      .WillOnce(Return(std::expected<void, UsbError>{}));
  EXPECT_CALL(mock_usb, Receive(_, _))
      .WillOnce([&mock_resp](std::span<uint8_t> out_buffer, auto) {
        auto bytes = AsBytes(mock_resp);
        std::ranges::copy(bytes, out_buffer.begin());
        return static_cast<int>(bytes.size());
      });

  auto result =
      ec_comm.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kInvalidChecksum);
}
}  // namespace
}  // namespace egis
