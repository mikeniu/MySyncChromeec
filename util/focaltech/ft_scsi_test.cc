/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_scsi.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

#include "ft_util.h"
#include "mock_usb_transport.h"
#include "test_utils.h"
#include "updater.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;

using focaltech::testing::kArbitraryPayloadByte;
using focaltech::testing::ReturnValidCsw;

constexpr uint32_t kInvalidCswSignature = 0xDEADBEEF;

UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

constexpr size_t kArbitraryDataSize = 10;
constexpr auto kScsiCmdTimeOut = 100ms;

class FtScsiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock = std::make_unique<testing::MockUsbTransport>();
    mock_ = mock.get();
    device_ = std::make_unique<UsbDevice>(std::move(mock));
  }

  testing::MockUsbTransport* mock_;
  std::unique_ptr<UsbDevice> device_;
};

}  // namespace

TEST_F(FtScsiTest, SendBulkDataOut_DataOutSuccess) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(kUsbBulkMaxPacketSize));
  EXPECT_CALL(*mock_, Recv(_, _)).WillOnce(ReturnValidCsw(1));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kWriteBin,
  };
  std::vector<uint8_t> data_buffer(kUsbBulkMaxPacketSize,
                                   kArbitraryPayloadByte);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data_buffer,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, SendBulkDataOut_SendOnlySuccess) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, WaitForReset(_))
      .WillOnce(Return(std::expected<void, Error>{}));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kSoftReset,
  };
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(*device_, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, SendBulkDataOut_SendOnlyWaitResetFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, WaitForReset(_))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kSoftReset,
  };
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(*device_, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kHardwareFailure);
}

TEST_F(FtScsiTest, SendBulkDataOut_CbwSendFails) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kWriteBin,
  };
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kDeviceNotFound);
}

TEST_F(FtScsiTest, SendBulkDataOut_DataSendFails) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kWriteBin,
  };
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kDeviceNotFound);
}

TEST_F(FtScsiTest, SendBulkDataOut_CswSignatureMismatch) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(kArbitraryDataSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke([](std::span<uint8_t> data, auto) {
        BulkCsw csw{
            .signature = ToLittleEndian<uint32_t>(kInvalidCswSignature),
            .tag = ToLittleEndian<uint32_t>(1),
            .data_residue = 0,
            .status = CswStatus::kCommandPassed,
        };
        std::ranges::copy(AsUint8Span(csw), data.begin());
        return sizeof(csw);
      }));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kAction,
      .subcmd = Subcmd::kWriteBin,
  };
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kHardwareFailure);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_Success) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke([](std::span<uint8_t> data, auto) {
        std::ranges::fill(data, kArbitraryPayloadByte);
        return kUsbBulkMaxPacketSize;
      }))
      .WillOnce(ReturnValidCsw(1));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kReadInfo,
      .subcmd = Subcmd::kReadData,
  };
  std::vector<uint8_t> out_buffer(kUsbBulkMaxPacketSize);
  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, kUsbBulkMaxPacketSize);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_ShortReadFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Return(1))  // short data read
      .WillOnce(Return(std::unexpected(Error::kHardwareFailure)));

  const CmdFormat scsi_cmd{};
  std::vector<uint8_t> out_buffer(2);

  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kHardwareFailure);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_EmptyBuffer) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _)).WillOnce(ReturnValidCsw(1));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kReadInfo,
      .subcmd = Subcmd::kReadData,
  };
  std::vector<uint8_t> empty;
  auto result = ReceiveBulkDataIn(*device_, scsi_cmd, empty, kScsiCmdTimeOut);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_DataRecvFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  const CmdFormat scsi_cmd{
      .opcode = Opcode::kReadInfo,
      .subcmd = Subcmd::kReadData,
  };
  std::vector<uint8_t> out_buffer(kUsbBulkMaxPacketSize);
  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kDeviceNotFound);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_FailsOnNonZeroCswStatus) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Invoke([](std::span<uint8_t> data, auto) {
        BulkCsw csw{
            .signature = ToLittleEndian(kUsbMsCswSignature),
            .tag = ToLittleEndian(1),
            .data_residue = 0,
            .status = CswStatus::kCommandFailed,
        };
        std::ranges::copy(AsUint8Span(csw), data.begin());
        return sizeof(csw);
      }));

  UsbDevice device(std::move(mock));
  const CmdFormat scsi_cmd{};
  std::vector<uint8_t> empty_buffer;

  auto res = ReceiveBulkDataIn(device, scsi_cmd, empty_buffer, 1s);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kHardwareFailure);
}

}  // namespace focaltech
