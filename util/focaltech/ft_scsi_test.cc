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

#include "mock_usb_transport.h"
#include "test_utils.h"
#include "updater.h"
#include "usb_device.h"

namespace {

using namespace std::chrono_literals;
using ::focaltech::testing::BuildValidCsw;
using ::focaltech::testing::MockUsbTransport;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;

UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

constexpr size_t kMaxPacketSize = 64;
constexpr size_t kArbitraryDataSize = 10;
constexpr auto kScsiCmdTimeOut = std::chrono::milliseconds(100);

}  // namespace

TEST(FtScsiTest, BuildFtCmd_VerifyFields) {
  auto cmd = BuildFtCmd(kFtCmdAction, kFtSubcmdErasePage, 0x12345678, 0x1000,
                        0x2000, 0x01);

  EXPECT_THAT(
      cmd,
      ElementsAre(kFtCmdAction,            // [0] opcode
                  0x78, 0x56, 0x34, 0x12,  // [1-4] address (little‑endian)
                  0x00, 0x10, 0x00, 0x00,  // [5-8] length (little‑endian)
                  0x00, 0x20, 0x00, 0x00,  // [9-12] aux_address (little‑endian)
                  kFtSubcmdErasePage,      // [13] subcommand
                  0x01,                    // [14] flags
                  0x00                     // [15] reserved
                  ));
}

TEST(FtScsiTest, BuildFtCmd_ZeroDefaults) {
  auto cmd = BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData);

  EXPECT_THAT(cmd, ElementsAre(kFtCmdReadInfo,          // [0] opcode
                               0x00, 0x00, 0x00, 0x00,  // [1-4] address
                               0x00, 0x00, 0x00, 0x00,  // [5-8] length
                               0x00, 0x00, 0x00, 0x00,  // [9-12] aux_address
                               kFtSubcmdReadData,       // [13] subcommand
                               0x00,                    // [14] flags
                               0x00                     // [15] reserved
                               ));
}

TEST(FtScsiTest, SendBulkDataOut_DataOutSuccess) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kMaxPacketSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return focaltech::testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin);
  std::vector<uint8_t> data_buffer(kMaxPacketSize, 0xAA);
  auto result = SendBulkDataOut(dev, scsi_cmd, data_buffer,
                                TransferDirection::kOut, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, SendBulkDataOut_SendOnlySuccess) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, WaitForReset(_))
      .WillOnce(
          Invoke([](auto) -> std::expected<void, FtError> { return {}; }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdSoftReset);
  std::vector<uint8_t> empty;
  auto result = SendBulkDataOut(dev, scsi_cmd, empty,
                                TransferDirection::kSendOnly, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, SendBulkDataOut_CbwSendFails) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return std::unexpected(FtError::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

TEST(FtScsiTest, SendBulkDataOut_DataSendFails) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return std::unexpected(FtError::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

TEST(FtScsiTest, SendBulkDataOut_CswSignatureMismatch) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kArbitraryDataSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke([](std::span<uint8_t> data,
                          auto) -> std::expected<int, FtError> {
        // Use the official BulkCsWrap from ft_scsi.h
        BulkCsWrap csw{
            .signature = ToLittleEndian<uint32_t>(0xDEADBEEF),  // Bad signature
            .tag = ToLittleEndian<uint32_t>(1),
            .data_residue = 0,
            .status = 0,
        };
        std::memcpy(data.data(), &csw, sizeof(csw));
        return sizeof(csw);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin);
  std::vector<uint8_t> data(kArbitraryDataSize);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

TEST(FtScsiTest, ReceiveBulkDataIn_Success) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            std::fill(data.begin(), data.end(), 0xAB);
            return kMaxPacketSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return focaltech::testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData);
  std::vector<uint8_t> out_buffer(kMaxPacketSize);
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, ReceiveBulkDataIn_EmptyBuffer) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return focaltech::testing::kUsbCswSize;
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData);
  std::vector<uint8_t> empty;
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, empty, kScsiCmdTimeOut);
  EXPECT_TRUE(result.has_value());
}

TEST(FtScsiTest, ReceiveBulkDataIn_DataRecvFails) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return std::unexpected(FtError::kNoSuchDevice);
      }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData);
  std::vector<uint8_t> out_buffer(kMaxPacketSize);
  auto result = ReceiveBulkDataIn(dev, scsi_cmd, out_buffer, kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}
