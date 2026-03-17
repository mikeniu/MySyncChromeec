/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "flasher_logic.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "bootrom.h"
#include "mock_usb_comm.h"

namespace egis {
namespace {
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class FlasherLogicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fw_path_ = std::filesystem::temp_directory_path() / "test_fw.bin";
    std::ofstream(fw_path_, std::ios::binary).write("TESTDATA", 8);
  }
  void TearDown() override { std::filesystem::remove(fw_path_); }
  std::filesystem::path fw_path_;
};

TEST_F(FlasherLogicTest, RunFlashFwInvalidArgs) {
  NiceMock<MockUsbComm> mock_usb;
  BootromComm brom(mock_usb);
  AppConfig config;

  EXPECT_EQ(RunFlashFw(config, brom).error(), AppError::kInvalidArgs);

  config.fw_path = fw_path_.string();
  config.algo_type = EgisCryptoType::kAesGcm;
  EXPECT_EQ(RunFlashFw(config, brom).error(), AppError::kInvalidArgs);
}

TEST_F(FlasherLogicTest, RunFlashFwSuccessFlow) {
  NiceMock<MockUsbComm> mock_usb;
  BootromComm brom(mock_usb);

  AppConfig config{
      .fw_path = fw_path_.string(),
      .algo_type = EgisCryptoType::kSha256,
      .flash_offset = 0x0000,
  };

  ON_CALL(mock_usb, Send(_, _))
      .WillByDefault(Return(std::expected<void, UsbError>{}));
  ON_CALL(mock_usb, Receive(_, _))
      .WillByDefault([](std::span<uint8_t> out_buffer, auto) {
        // Return a valid secure response with a 4-byte payload. This will
        // satisfy calls like ReadReg that expect data, while being
        // ignored by calls that don't.
        BootromComm::SecureResponseHeader mock_hdr{
            .status = 0,
            .payload_len = ToLittleEndian(static_cast<uint32_t>(4))};
        const std::array<uint8_t, 4> mock_payload{};  // Four zero-bytes
        auto resp_packet = PackMessage(mock_hdr, mock_payload);
        if (out_buffer.size() >= resp_packet.size()) {
          std::ranges::copy(resp_packet, out_buffer.begin());
          return static_cast<int>(resp_packet.size());
        }
        return 0;
      });

  EXPECT_TRUE(RunFlashFw(config, brom).has_value());
}

TEST_F(FlasherLogicTest, RunFlashCmdTruncatedPacket) {
  NiceMock<MockUsbComm> mock_usb;

  const uint8_t bad_cmd[] = {EgisBootrom::kCmdSecureWrapped,
                             std::to_underlying(EgisCryptoType::kSha256)};
  std::ofstream(fw_path_, std::ios::binary)
      .write(reinterpret_cast<const char*>(bad_cmd), 2);

  AppConfig config{.mode = AppMode::kFlashCmd, .fw_path = fw_path_.string()};

  EXPECT_EQ(RunFlashCmd(config, mock_usb).error(), AppError::kParseError);
}
}  // namespace
}  // namespace egis
