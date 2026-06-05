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

UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

constexpr size_t kArbitraryDataSize = 10;
constexpr auto kScsiCmdTimeOut = 100ms;

// ========== Test Fixture ==========
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

TEST_F(FtScsiTest, BuildFtCmd_VerifyFields) {
  auto cmd = BuildFtCmd(Opcode::kAction, Subcmd::kErasePage, 0x12345678, 0x1000,
                        0x2000, 0x01);

  auto cmd_span = AsUint8Span(cmd);
  EXPECT_THAT(cmd_span,
              ElementsAre(0xDC, 0x78, 0x56, 0x34, 0x12, 0x00, 0x10, 0x00, 0x00,
                          0x00, 0x20, 0x00, 0x00, 0x08, 0x01, 0x00));
}

TEST_F(FtScsiTest, BuildFtCmd_ZeroDefaults) {
  auto cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);

  auto cmd_span = AsUint8Span(cmd);
  EXPECT_THAT(cmd_span,
              ElementsAre(0xDB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00));
}

TEST_F(FtScsiTest, SendBulkDataOut_DataOutSuccess) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(kUsbBulkMaxPacketSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data_buffer(kUsbBulkMaxPacketSize, 0xAA);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data_buffer,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, SendBulkDataOut_SendOnlySuccess) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, WaitForReset(_))
      .WillOnce(Return(std::expected<void, Error>{}));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kSoftReset);
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(*device_, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, SendBulkDataOut_SendOnlyWaitResetFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, WaitForReset(_))
      .WillOnce(Return(std::unexpected(Error::kNoSuchDevice)));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kSoftReset);
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(*device_, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(FtScsiTest, SendBulkDataOut_CbwSendFails) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(std::unexpected(Error::kNoSuchDevice)));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST_F(FtScsiTest, SendBulkDataOut_DataSendFails) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(std::unexpected(Error::kNoSuchDevice)));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST_F(FtScsiTest, SendBulkDataOut_CswSignatureMismatch) {
  EXPECT_CALL(*mock_, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(kArbitraryDataSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            BulkCsWrap csw{
                .signature = ToLittleEndian<uint32_t>(0xDEADBEEF),
                .tag = ToLittleEndian<uint32_t>(1),
                .data_residue = 0,
                .status = CswStatus::kCommandPassed,
            };
            std::memcpy(data.data(), &csw, sizeof(csw));
            return sizeof(csw);
          }));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(*device_, scsi_cmd, data,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_Success) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            std::fill(data.begin(), data.end(), 0xAB);
            return kUsbBulkMaxPacketSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> out_buffer(kUsbBulkMaxPacketSize);
  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_ShortReadFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Return(1))  // short data read
      .WillOnce(Return(std::unexpected(Error::kResultFail)));  // CSW failure

  CmdFormat scsi_cmd{};
  std::vector<uint8_t> out_buffer(2);

  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_EmptyBuffer) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> empty;
  auto result = ReceiveBulkDataIn(*device_, scsi_cmd, empty, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST_F(FtScsiTest, ReceiveBulkDataIn_DataRecvFails) {
  EXPECT_CALL(*mock_, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));
  EXPECT_CALL(*mock_, Recv(_, _))
      .WillOnce(Return(std::unexpected(Error::kNoSuchDevice)));

  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> out_buffer(kUsbBulkMaxPacketSize);
  auto result =
      ReceiveBulkDataIn(*device_, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

// ============================================================================
// CSW Status Test
// ============================================================================

// Test that a non-zero CSW status (Command Failed) causes the function to fail.
TEST_F(FtScsiTest, ReceiveBulkDataIn_FailsOnNonZeroCswStatus) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  // Mock Send to return success (the CBW transfer should succeed).
  EXPECT_CALL(*mock_ptr, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  // The device will send a CSW with status = 1 (Command Failed).
  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            BulkCsWrap csw{
                .signature = ToLittleEndian(kUsbMsCswSignature),
                .tag = ToLittleEndian(1),
                .data_residue = 0,
                .status = CswStatus::kCommandFailed,  // Non-zero status!
            };
            std::memcpy(data.data(), &csw, sizeof(csw));
            return sizeof(csw);
          }));

  UsbDevice device(std::move(mock));
  CmdFormat scsi_cmd{};
  std::vector<uint8_t> empty_buffer;

  auto res = ReceiveBulkDataIn(device, scsi_cmd, empty_buffer, 1s);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kResultFail);
}

}  // namespace focaltech
