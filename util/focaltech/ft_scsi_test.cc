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

constexpr size_t kMaxPacketSize = 64;
constexpr size_t kArbitraryDataSize = 10;
constexpr auto kScsiCmdTimeOut = std::chrono::milliseconds(100);

}  // namespace

TEST(FtScsiTest, BuildFtCmd_VerifyFields) {
  auto cmd = BuildFtCmd(Opcode::kAction, Subcmd::kErasePage, 0x12345678, 0x1000,
                        0x2000, 0x01);

  auto cmd_span = AsUint8Span(cmd);
  EXPECT_THAT(cmd_span,
              ElementsAre(0xDC, 0x78, 0x56, 0x34, 0x12, 0x00, 0x10, 0x00, 0x00,
                          0x00, 0x20, 0x00, 0x00, 0x08, 0x01, 0x00));
}

TEST(FtScsiTest, BuildFtCmd_ZeroDefaults) {
  auto cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);

  auto cmd_span = AsUint8Span(cmd);
  EXPECT_THAT(cmd_span,
              ElementsAre(0xDB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00));
}

TEST(FtScsiTest, SendBulkDataOut_DataOutSuccess) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return kMaxPacketSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data_buffer(kMaxPacketSize, 0xAA);
  auto result = SendBulkDataOut(dev, scsi_cmd, data_buffer,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, SendBulkDataOut_SendOnlySuccess) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, WaitForReset(_))
      .WillOnce(Invoke([](auto) -> std::expected<void, Error> { return {}; }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kSoftReset);
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(dev, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, SendBulkDataOut_SendOnlyWaitResetFails) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, WaitForReset(_))
      .WillOnce(Invoke([](auto) -> std::expected<void, Error> {
        return std::unexpected(Error::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kSoftReset);
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(dev, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST(FtScsiTest, SendBulkDataOut_CbwSendFails) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return std::unexpected(Error::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST(FtScsiTest, SendBulkDataOut_DataSendFails) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return std::unexpected(Error::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST(FtScsiTest, SendBulkDataOut_CswSignatureMismatch) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return kArbitraryDataSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            BulkCsWrap csw{
                .signature = ToLittleEndian<uint32_t>(0xDEADBEEF),
                .tag = ToLittleEndian<uint32_t>(1),
                .data_residue = 0,
                .status = CswStatus::kCommandPassed,
            };
            std::memcpy(data.data(), &csw, sizeof(csw));
            return sizeof(csw);
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kAction, Subcmd::kWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST(FtScsiTest, ReceiveBulkDataIn_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            std::fill(data.begin(), data.end(), 0xAB);
            return kMaxPacketSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> out_buffer(kMaxPacketSize);
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, ReceiveBulkDataIn_ShortReadFails) {
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Return(1))  // short data read
      .WillOnce(Return(std::unexpected(Error::kResultFail)));  // CSW failure

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd{};
  std::vector<uint8_t> out_buffer(2);

  auto result = ReceiveBulkDataIn(dev, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST(FtScsiTest, ReceiveBulkDataIn_EmptyBuffer) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> empty;
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, empty, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, ReceiveBulkDataIn_DataRecvFails) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return std::unexpected(Error::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  CmdFormat scsi_cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData);
  std::vector<uint8_t> out_buffer(kMaxPacketSize);
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

}  // namespace focaltech
