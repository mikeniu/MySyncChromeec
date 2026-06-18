/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "test_utils.h"
#include "updater.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class FakeUsbTransport : public UsbTransport {
 public:
  UsbDeviceId device_id() const override { return kFocalBootId; }

  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCbwSize) {
      std::memcpy(&current_cbw_tag_, &data[offsetof(BulkCbw, tag)],
                  sizeof(current_cbw_tag_));
    }
    return data.size();
  }

  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCswSize) {
      testing::BuildValidCsw(data, current_cbw_tag_);
      return testing::kUsbCswSize;
    }
    if (data.size() == kFlashWpBufferSize) {
      data[0] = static_cast<uint8_t>(FlashWpArea::kBoot);
      return kFlashWpBufferSize;
    }
    return 0;
  }

  std::expected<void, Error> WaitForReset(std::chrono::milliseconds) override {
    return {};
  }

 private:
  uint32_t current_cbw_tag_ = 0;
};

}  // namespace

TEST(CliTest, ParseArguments_UpdateCommand) {
  std::vector<std::string_view> args = {"firmware.bin"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kUpdate);
  EXPECT_EQ(config->firmware_path, "firmware.bin");
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST(CliTest, ParseArguments_EnterRom) {
  std::vector<std::string_view> args = {kCmdEnterRom};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kEnterRom);
  EXPECT_TRUE(config->firmware_path.empty());
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST(CliTest, ParseArguments_GetFlashWp) {
  std::vector<std::string_view> args = {kCmdGetFlashWp};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kGetFlashWp);
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST(CliTest, ParseArguments_SetFlashWp_Valid) {
  std::vector<std::string_view> args = {kCmdSetFlashWp, "ro"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kSetFlashWp);
  EXPECT_EQ(config->wp_area, FlashWpArea::kRo);
}

TEST(CliTest, ParseArguments_SetFlashWp_InvalidParam) {
  std::vector<std::string_view> args = {kCmdSetFlashWp, "invalid_area"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), Error::kInvalidParameter);
}

TEST(CliTest, ParseArguments_UnknownCommand) {
  std::vector<std::string_view> args = {"unknown"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), Error::kInvalidParameter);
}

TEST(CliTest, GetDeviceMode_RomMode) {
  auto res = GetDeviceMode(kFocalRomId);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kRom);
}

TEST(CliTest, GetDeviceMode_BootMode) {
  auto res = GetDeviceMode(kFocalBootId);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kBootloader);
}

TEST(CliTest, GetDeviceMode_Unknown) {
  UsbDeviceId unknown{.vid = 0x1234, .pid = 0x5678};
  auto res = GetDeviceMode(unknown);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kDeviceNotFound);
}

TEST(CliTest, ExecuteCommand_GetFlashWp_Success) {
  auto transport = std::make_unique<FakeUsbTransport>();
  UsbDevice device(std::move(transport));
  AppConfig config{.cmd = Command::kGetFlashWp};
  auto result = ExecuteCommand(config, device, DeviceMode::kBootloader);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_EnterRom_Success) {
  auto transport = std::make_unique<FakeUsbTransport>();
  UsbDevice device(std::move(transport));
  AppConfig config{.cmd = Command::kEnterRom};
  auto result = ExecuteCommand(config, device, DeviceMode::kBootloader);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_EnterRom_FailsInRomMode) {
  auto transport = std::make_unique<FakeUsbTransport>();
  UsbDevice device(std::move(transport));
  AppConfig config{.cmd = Command::kEnterRom};
  auto result = ExecuteCommand(config, device, DeviceMode::kRom);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kInvalidMode);
}

TEST(CliTest, ExecuteCommand_UnknownCommand) {
  AppConfig config{.cmd = Command::kUnknown};
  auto transport = std::make_unique<FakeUsbTransport>();
  UsbDevice device(std::move(transport));
  auto result = ExecuteCommand(config, device, DeviceMode::kBootloader);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kInvalidParameter);
}

}  // namespace focaltech
