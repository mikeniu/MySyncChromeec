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

using namespace std::chrono_literals;
using ::focaltech::testing::BuildValidCsw;
using ::focaltech::testing::MockUsbTransport;
using ::testing::_;
using ::testing::Invoke;

static UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

constexpr size_t kMaxPacketSize = 64;
constexpr auto kScsiCmdTimeOut = std::chrono::milliseconds(100);

// -----------------------------------------------------------------------------
// BuildFtCmd Tests
// -----------------------------------------------------------------------------
TEST(FtScsiTest, BuildFtCmd_VerifyFields) {
  auto cmd = BuildFtCmd(0xDC, 0x08, 0x12345678, 0x1000, 0x2000, 0x01);
  EXPECT_EQ(cmd[0], 0xDC);
  EXPECT_EQ(cmd[1], 0x78);
  EXPECT_EQ(cmd[2], 0x56);
  EXPECT_EQ(cmd[3], 0x34);
  EXPECT_EQ(cmd[4], 0x12);
  EXPECT_EQ(cmd[5], 0x00);
  EXPECT_EQ(cmd[6], 0x10);
  EXPECT_EQ(cmd[7], 0x00);
  EXPECT_EQ(cmd[8], 0x00);
  EXPECT_EQ(cmd[9], 0x00);
  EXPECT_EQ(cmd[10], 0x20);
  EXPECT_EQ(cmd[11], 0x00);
  EXPECT_EQ(cmd[12], 0x00);
  EXPECT_EQ(cmd[13], 0x08);
  EXPECT_EQ(cmd[14], 0x01);
  EXPECT_EQ(cmd[15], 0x00);
}

TEST(FtScsiTest, BuildFtCmd_ZeroDefaults) {
  auto cmd = BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData);
  EXPECT_EQ(cmd[0], kFtCmdReadInfo);
  EXPECT_EQ(cmd[13], kFtSubcmdReadData);
  for (int i = 1; i <= 12; ++i) EXPECT_EQ(cmd[i], 0);
  EXPECT_EQ(cmd[14], 0);
  EXPECT_EQ(cmd[15], 0);
}

// -----------------------------------------------------------------------------
// SendBulkDataOut Tests
// -----------------------------------------------------------------------------

TEST(FtScsiTest, SendBulkDataOut_DataOutSuccess) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;  // CBW size
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kMaxPacketSize;  // data OUT size
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
  std::vector<uint8_t> data(10);
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
  std::vector<uint8_t> data(10);
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
      .WillOnce(
          Invoke([](auto, auto) -> std::expected<int, FtError> { return 10; }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // Bad signature
            struct __attribute__((packed)) Csw {
              uint32_t sig;
              uint32_t tag;
              uint32_t residue;
              uint8_t status;
            };
            Csw csw{
                .sig = ToLittleEndian<uint32_t>(0xDEADBEEF),
                .tag = ToLittleEndian<uint32_t>(1),
                .residue = 0,
                .status = 0,
            };
            std::memcpy(data.data(), &csw, sizeof(csw));
            return sizeof(csw);
          }));

  UsbDevice dev = MakeDevice(std::move(mock));
  std::array<uint8_t, kFtScsiCmdSize> scsi_cmd =
      BuildFtCmd(kFtCmdAction, kFtSubcmdWriteBin);
  std::vector<uint8_t> data(10);
  auto result = SendBulkDataOut(dev, scsi_cmd, data, TransferDirection::kOut,
                                kScsiCmdTimeOut);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

// -----------------------------------------------------------------------------
// ReceiveBulkDataIn Tests
// -----------------------------------------------------------------------------

TEST(FtScsiTest, ReceiveBulkDataIn_Success) {
  auto mock = std::make_unique<MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return focaltech::testing::kUsbCbwSize;
      }));
  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // Simulate data IN (kMaxPacketSize bytes)
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
