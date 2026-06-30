/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flasher_logic.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bootrom.h"
#include "fake_usb_comm.h"

namespace egis {
namespace {

class FlasherLogicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fw_path_ = std::filesystem::temp_directory_path() / "test_fw.bin";

    // Use the sticky response to simulate a completely compliant, happy device.
    // It will constantly return a generic success packet for all operations,
    // allowing the high-level logic to run uninterrupted.
    bootrom_protocol::SecureResponseHeader mock_hdr{
        .status = 0, .payload_len = ToLittleEndian(static_cast<uint32_t>(4))};
    const auto mock_payload = std::to_array<uint8_t>({0, 0, 0, 0});

    fake_usb_.SetStickyDeviceResponse(PackMessage(mock_hdr, mock_payload));
  }

  void TearDown() override {
    if (std::filesystem::exists(fw_path_)) {
      std::filesystem::remove(fw_path_);
    }
  }

  void WriteTestFile(std::span<const uint8_t> data) {
    std::ofstream(fw_path_, std::ios::binary)
        .write(reinterpret_cast<const char*>(data.data()), data.size());
  }

  std::filesystem::path fw_path_;
  FakeUsbComm fake_usb_;
  BootromComm bootrom_comm_{fake_usb_};
};

TEST_F(FlasherLogicTest, RunFlashFwInvalidArgs) {
  FlasherConfig config;

  // Missing FW Path
  EXPECT_EQ(ExecuteFirmwareFlash(config, bootrom_comm_).error(),
            AppError::kInvalidArgs);

  // Missing Key Path when AES is requested
  WriteTestFile(std::to_array<uint8_t>({0x01, 0x02}));
  config.firmware_path = fw_path_.string();
  config.crypto_algorithm = EgisCryptoType::kAesGcm;
  EXPECT_EQ(ExecuteFirmwareFlash(config, bootrom_comm_).error(),
            AppError::kInvalidArgs);
}

TEST_F(FlasherLogicTest, RunFlashFwOversizedChunking) {
  // Create an 80KB firmware file to force chunking
  // (Max chunk is BootromComm::kMaxFlashWriteSize = 32KB)
  std::vector<uint8_t> large_fw(80 * 1024, 0xFF);
  WriteTestFile(large_fw);

  FlasherConfig config{
      .firmware_path = fw_path_.string(),
      .crypto_algorithm = EgisCryptoType::kSha256,
      .image_offset = 0x0000,
  };

  EXPECT_TRUE(ExecuteFirmwareFlash(config, bootrom_comm_).has_value());

  // Verify that the firmware was written in the correct number of chunks by
  // inspecting the USB transmissions.
  const auto& transmissions = fake_usb_.GetHostTransmissions();
  int write_flash_count = 0;
  for (const auto& tx : transmissions) {
    // A WriteFlash command is a kCmdMemWrite with a large payload, whereas a
    // WriteReg is also a kCmdMemWrite but with a tiny payload. We can
    // distinguish them by size. A register write payload is exactly sizeof
    // (bootrom_protocol::MemWriteRegCommand).
    if (tx.size() > sizeof(bootrom_protocol::SecureCommandShaHeader) +
                        sizeof(bootrom_protocol::MemWriteRegCommand) &&
        tx[0] ==
            std::to_underlying(bootrom_protocol::CommandId::kSecureWrapped)) {
      // The wrapped command ID is inside the payload of the secure command.
      if (tx[sizeof(bootrom_protocol::SecureCommandShaHeader)] ==
          std::to_underlying(bootrom_protocol::CommandId::kMemWrite)) {
        write_flash_count++;
      }
    }
  }
  // 80KB firmware with 32KB max chunk size should result in 3 chunks.
  EXPECT_EQ(write_flash_count, 3);
}

TEST_F(FlasherLogicTest, RunFlashCmdParserDataTooSmall) {
  // A file with only 1 byte cannot possibly contain a valid header
  WriteTestFile(std::to_array<uint8_t>(
      {std::to_underlying(bootrom_protocol::CommandId::kSecureWrapped)}));

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};
  EXPECT_EQ(ExecuteRawCommandFlash(config, bootrom_comm_).error(),
            AppError::kParseError);
}

TEST_F(FlasherLogicTest, RunFlashCmdParserInvalidHeader) {
  // Not a System Reset (0x12) or Secure Wrapped (0xF4) command
  WriteTestFile(std::to_array<uint8_t>({0xFF, 0x00, 0x00, 0x00}));

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};
  EXPECT_EQ(ExecuteRawCommandFlash(config, bootrom_comm_).error(),
            AppError::kParseError);
}

TEST_F(FlasherLogicTest, RunFlashCmdParserTruncatedPayload) {
  // Valid AES header, claims payload length is 100 bytes, but file ends early
  bootrom_protocol::SecureCommandAesHeader hdr;
  hdr.algorithm = EgisCryptoType::kAesGcm;
  hdr.payload_len = ToLittleEndian(static_cast<uint32_t>(100));

  WriteTestFile(AsBytes(hdr));

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};
  EXPECT_EQ(ExecuteRawCommandFlash(config, bootrom_comm_).error(),
            AppError::kParseError);
}

TEST_F(FlasherLogicTest, RunFlashCmdParserPayloadTooLarge) {
  // Construct a header that claims a payload larger than the hardware limit.
  // This prevents a potential integer overflow when calculating packet size.
  bootrom_protocol::SecureCommandShaHeader hdr;
  hdr.algorithm = EgisCryptoType::kSha256;
  hdr.payload_len =
      ToLittleEndian(static_cast<uint32_t>(BootromComm::kMaxPayloadSize + 1));

  // We only need to write the header, as the parser should fail before reading
  // the payload.
  WriteTestFile(AsBytes(hdr));

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};
  EXPECT_EQ(ExecuteRawCommandFlash(config, bootrom_comm_).error(),
            AppError::kParseError);
}

TEST_F(FlasherLogicTest, RunFlashCmdSuccessReset) {
  // System Reset is a special case: 2 bytes, no response expected
  WriteTestFile(std::to_array<uint8_t>(
      {std::to_underlying(bootrom_protocol::CommandId::kSystemReset),
       bootrom_protocol::kSystemResetTypeNormal}));

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};

  EXPECT_TRUE(ExecuteRawCommandFlash(config, bootrom_comm_).has_value());
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
}

TEST_F(FlasherLogicTest, RunFlashCmdSuccessSha) {
  // Construct a valid SHA header claiming a 4-byte payload
  bootrom_protocol::SecureCommandShaHeader hdr;
  hdr.algorithm = EgisCryptoType::kSha256;
  hdr.payload_len = ToLittleEndian(static_cast<uint32_t>(4));
  hdr.hash = crypto::Sha256(std::to_array<uint8_t>({1, 2, 3, 4}));

  auto valid_packet = PackMessage(hdr, std::to_array<uint8_t>({1, 2, 3, 4}));
  WriteTestFile(valid_packet);

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};

  // With a sticky success response from the fake, this should succeed.
  EXPECT_TRUE(ExecuteRawCommandFlash(config, bootrom_comm_).has_value());
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
}

TEST_F(FlasherLogicTest, RunFlashCmdDeviceErrorHalt) {
  // Construct a valid SHA header claiming a 4-byte payload
  bootrom_protocol::SecureCommandShaHeader hdr;
  hdr.algorithm = EgisCryptoType::kSha256;
  hdr.payload_len = ToLittleEndian(static_cast<uint32_t>(4));

  auto valid_packet = PackMessage(hdr, std::to_array<uint8_t>({1, 2, 3, 4}));
  WriteTestFile(valid_packet);

  FlasherConfig config{.mode = AppMode::kFlashCmd,
                       .firmware_path = fw_path_.string()};

  // Clear the sticky success response, and push an explicit error response
  // (0x0001)
  fake_usb_.Reset();
  uint16_t error_status = ToLittleEndian(static_cast<uint16_t>(1));
  auto error_bytes = AsBytes(error_status);
  fake_usb_.PushDeviceResponse({error_bytes.begin(), error_bytes.end()});

  // The logic must detect the non-zero status and abort with kBootromError
  EXPECT_EQ(ExecuteRawCommandFlash(config, bootrom_comm_).error(),
            AppError::kBootromError);
}

}  // namespace
}  // namespace egis
