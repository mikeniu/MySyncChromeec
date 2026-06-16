/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <ranges>
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
using testing::BuildValidCsw;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;

UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

class FakeEcTransport : public UsbTransport {
 public:
  explicit FakeEcTransport(UsbDeviceId id = kFocalEcTestId) : device_id_(id) {}

  void AddResponse(std::vector<uint8_t> response) {
    responses_.push_back(std::move(response));
  }

  void FailSendOnCommand(size_t cmd_index) { fail_on_command_ = cmd_index; }

  UsbDeviceId device_id() const override { return device_id_; }

  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds) override {
    last_sent_.assign(data.begin(), data.end());

    if (send_count_ == fail_on_command_) {
      ++send_count_;
      return std::unexpected(Error::kNoSuchDevice);
    }
    ++send_count_;
    return data.size();
  }

  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (response_idx_ >= responses_.size()) {
      return 0;
    }

    const auto& resp = responses_[response_idx_];
    if (recv_offset_ >= resp.size()) {
      ++response_idx_;
      recv_offset_ = 0;
      if (response_idx_ >= responses_.size()) {
        return 0;
      }
      return Recv(data, std::chrono::milliseconds(0));
    }

    auto unread_data = std::span(resp).subspan(recv_offset_);
    size_t to_copy = std::min(data.size(), unread_data.size());
    std::ranges::copy(unread_data.first(to_copy), data.begin());
    recv_offset_ += to_copy;
    return to_copy;
  }

  std::expected<void, Error> WaitForReset(std::chrono::milliseconds) override {
    return {};
  }

  const std::vector<uint8_t>& last_sent() const { return last_sent_; }

 private:
  UsbDeviceId device_id_;
  std::vector<std::vector<uint8_t>> responses_;
  size_t response_idx_ = 0;
  size_t recv_offset_ = 0;
  size_t send_count_ = 0;
  size_t fail_on_command_ = std::numeric_limits<size_t>::max();
  std::vector<uint8_t> last_sent_;
};

std::vector<uint8_t> BuildEcResponse(uint16_t result,
                                     std::vector<uint8_t> data) {
  ec_host_response resp{
      .struct_version = EC_HOST_RESPONSE_VERSION,
      .result = result,
      .data_len = static_cast<uint16_t>(data.size()),
      .reserved = 0,
  };

  std::vector<uint8_t> packet(sizeof(resp) + data.size());
  memcpy(packet.data(), &resp, sizeof(resp));
  if (!data.empty()) {
    memcpy(packet.data() + sizeof(resp), data.data(), data.size());
  }

  uint8_t checksum = std::accumulate(packet.begin(), packet.end(), uint8_t{0});
  packet[offsetof(ec_host_response, checksum)] = -checksum;
  return packet;
}

std::vector<uint8_t> BuildStringResponse(std::string_view str) {
  std::vector<uint8_t> data(str.begin(), str.end());
  if (data.empty() || data.back() != '\0') data.push_back('\0');
  return BuildEcResponse(EC_RES_SUCCESS, data);
}

std::vector<uint8_t> BuildEmptySuccessResponse() {
  return BuildEcResponse(EC_RES_SUCCESS, {});
}

// ========== Test Fixture ==========
class CliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto transport = std::make_unique<FakeEcTransport>();
    fake_ = transport.get();
    device_ = std::make_unique<UsbDevice>(std::move(transport));
  }

  FakeEcTransport* fake_;
  std::unique_ptr<UsbDevice> device_;
};

}  // namespace

// -------- ParseArguments tests using command name constants --------
TEST_F(CliTest, ParseArguments_UpdateCommand) {
  std::vector<std::string_view> args = {"firmware.bin"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kUpdate);
  EXPECT_EQ(config->ec_bin, "firmware.bin");
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST_F(CliTest, ParseArguments_EnterBootloader) {
  std::vector<std::string_view> args = {kCmdEnterBoot};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kEnterBoot);
  EXPECT_TRUE(config->ec_bin.empty());
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST_F(CliTest, ParseArguments_EnterRom) {
  std::vector<std::string_view> args = {kCmdEnterRom};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kEnterRom);
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST_F(CliTest, ParseArguments_GetFlashWp) {
  std::vector<std::string_view> args = {kCmdGetFlashWp};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kGetFlashWp);
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST_F(CliTest, ParseArguments_SetFlashWp_Valid) {
  std::vector<std::string_view> args = {kCmdSetFlashWp, "ro"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kSetFlashWp);
  EXPECT_EQ(config->wp_area, FlashWpArea::kRo);
}

TEST_F(CliTest, ParseArguments_SetFlashWp_ValidAll) {
  std::vector<std::string_view> args = {kCmdSetFlashWp, "all"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kSetFlashWp);
  EXPECT_EQ(config->wp_area, FlashWpArea::kAll);
}

TEST_F(CliTest, ParseArguments_SetFlashWp_InvalidParam) {
  std::vector<std::string_view> args = {kCmdSetFlashWp, "invalid_area"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), Error::kParameterError);
}

TEST_F(CliTest, ParseArguments_GetEcVersion) {
  std::vector<std::string_view> args = {kCmdGetEcVersion};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kGetEcVersion);
  EXPECT_EQ(config->wp_area, FlashWpArea::kUnknown);
}

TEST_F(CliTest, ParseArguments_UnknownCommand) {
  std::vector<std::string_view> args = {"unknown"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), Error::kParameterError);
}

TEST_F(CliTest, ParseInt_Valid) {
  auto res = ParseInt("42");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 42);
}

TEST_F(CliTest, ParseInt_Negative) {
  auto res = ParseInt("-5");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, -5);
}

TEST_F(CliTest, ParseInt_Invalid) {
  auto res = ParseInt("12a");
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kParameterError);
}

TEST_F(CliTest, GetDeviceMode_RomMode) {
  auto res = GetDeviceMode(kFocalRomId);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kRom);
}

TEST_F(CliTest, GetDeviceMode_BootMode) {
  auto res = GetDeviceMode(kFocalBootId);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kBoot);
}

TEST_F(CliTest, GetDeviceMode_EcTestMode) {
  auto res = GetDeviceMode(kFocalEcTestId);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kTest);
}

TEST_F(CliTest, GetDeviceMode_EcNormalMode) {
  auto res = GetDeviceMode(kFocalEcPid9865Id);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, DeviceMode::kEc);
}

TEST_F(CliTest, GetDeviceMode_Unknown) {
  UsbDeviceId unknown{.vid = 0x1234, .pid = 0x5678};
  auto res = GetDeviceMode(unknown);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kNoSuchDevice);
}

TEST_F(CliTest, SwitchMcuMode_SuccessWithoutDisconnect) {
  fake_->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake_->AddResponse(BuildEmptySuccessResponse());
  auto result = SwitchMcuMode(*device_, DeviceMode::kEc, Command::kEnterBoot);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, SwitchMcuMode_FailsAfterMaxRetries) {
  fake_->AddResponse(BuildStringResponse("ft9001_v1.0"));
  auto result = SwitchMcuMode(*device_, DeviceMode::kEc, Command::kEnterBoot);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(CliTest, SendEcCommandAndReadResponse_TruncatesPaddedUsbData) {
  std::vector<uint8_t> packet =
      BuildEcResponse(EC_RES_SUCCESS, {0x01, 0x02, 0x03});
  packet.resize(kUsbBulkMaxPacketSize, 0xAA);
  fake_->AddResponse(packet);
  auto result = internal::SendEcCommandAndReadResponse(*device_, 0x1234, 0);
  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, ElementsAre(0x01, 0x02, 0x03));
}

TEST_F(CliTest, GetBuildInfo_Success) {
  fake_->AddResponse(BuildStringResponse("chojnik_v1.2.3"));
  auto result = GetBuildInfo(*device_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "chojnik_v1.2.3");
}

TEST_F(CliTest, GetBuildInfo_NoDevice) {
  fake_->FailSendOnCommand(0);
  auto result = GetBuildInfo(*device_);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST_F(CliTest, SwitchToBootloader_Success) {
  fake_->AddResponse(BuildEmptySuccessResponse());
  auto result = SwitchToBootloader(*device_, 0);
  EXPECT_TRUE(result.has_value());
  const auto& sent = fake_->last_sent();
  EXPECT_THAT(sent, ElementsAre(3, 26, 0xE2, 0x00, 0, 0, 1, 0, 0));
}

TEST_F(CliTest, SwitchToRom_Success) {
  fake_->AddResponse(BuildEmptySuccessResponse());
  auto result = SwitchToRom(*device_);
  EXPECT_TRUE(result.has_value());
  const auto& sent = fake_->last_sent();
  EXPECT_THAT(sent,
              ElementsAre(3, 0x82, 0xCE, 0xFA, 0, 0, 3, 0, 0xAA, 0x06, 0x00));
}

TEST_F(CliTest, CheckEcBuildVersion_Success) {
  fake_->AddResponse(BuildStringResponse("ft9001_v2.0"));
  auto result = CheckEcBuildVersion(*device_, DeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, CheckEcBuildVersion_UnsupportedProject) {
  fake_->AddResponse(BuildStringResponse("unsupported_project_v1.0"));
  auto result = CheckEcBuildVersion(*device_, DeviceMode::kEc);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kNoSuchDevice);
}

TEST_F(CliTest, CheckEcBuildVersion_RetrySuccess) {
  fake_->FailSendOnCommand(0);
  fake_->AddResponse(BuildStringResponse("chojnik_v3.0"));
  auto result = CheckEcBuildVersion(*device_, DeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, CheckEcBuildVersion_AllRetriesFail) {
  for (int i = 0; i < kMaxRetry; ++i) {
    fake_->FailSendOnCommand(i);
  }
  auto result = CheckEcBuildVersion(*device_, DeviceMode::kEc);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(CliTest, SwitchMcuMode_EnterBoot) {
  fake_->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake_->FailSendOnCommand(1);
  auto result = SwitchMcuMode(*device_, DeviceMode::kEc, Command::kEnterBoot);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, SwitchMcuMode_EnterRom) {
  fake_->AddResponse(BuildStringResponse("chojnik_v2.0"));
  fake_->FailSendOnCommand(1);
  auto result = SwitchMcuMode(*device_, DeviceMode::kTest, Command::kEnterRom);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, SwitchMcuMode_FailsIfBuildVersionFails) {
  fake_->FailSendOnCommand(0);
  auto result = SwitchMcuMode(*device_, DeviceMode::kEc, Command::kEnterBoot);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kResultFail);
}

TEST_F(CliTest, ExecuteCommand_GetEcVersion_Success) {
  fake_->AddResponse(BuildStringResponse("ft9001_v2.0"));
  AppConfig config{.cmd = Command::kGetEcVersion};
  auto result = ExecuteCommand(config, *device_, DeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, ExecuteCommand_EnterBoot_Success) {
  fake_->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake_->FailSendOnCommand(1);
  AppConfig config{.cmd = Command::kEnterBoot};
  auto result = ExecuteCommand(config, *device_, DeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

// Test that enter_bootloader is rejected when device is not in EC mode.
TEST_F(CliTest, ExecuteCommand_EnterBoot_FailsInInvalidMode) {
  AppConfig config{.cmd = Command::kEnterBoot};
  // Try from Bootloader mode (should fail).
  auto result = ExecuteCommand(config, *device_, DeviceMode::kBoot);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kInvalidMode);

  // Try from ROM mode (should also fail).
  result = ExecuteCommand(config, *device_, DeviceMode::kRom);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kInvalidMode);

  // Try from Test mode (should also fail).
  result = ExecuteCommand(config, *device_, DeviceMode::kTest);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kInvalidMode);
}

TEST_F(CliTest, ExecuteCommand_EnterRom_FromTestMode) {
  fake_->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake_->FailSendOnCommand(1);
  AppConfig config{.cmd = Command::kEnterRom};
  auto result = ExecuteCommand(config, *device_, DeviceMode::kTest);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CliTest, ExecuteCommand_UnknownCommand) {
  AppConfig config{.cmd = Command::kUnknown};
  auto result = ExecuteCommand(config, *device_, DeviceMode::kBoot);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kParameterError);
}

namespace {

class FakeForFlashWp : public UsbTransport {
 public:
  UsbDeviceId device_id() const override { return kFocalBootId; }
  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCbwSize) {
      std::memcpy(&current_cbw_tag_, &data[offsetof(BulkCbWrap, tag)],
                  sizeof(current_cbw_tag_));
    }
    return data.size();
  }
  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCswSize) {
      BuildValidCsw(data, current_cbw_tag_, CswStatus::kCommandPassed);
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

class FakeForSetWp : public UsbTransport {
 public:
  UsbDeviceId device_id() const override { return kFocalBootId; }
  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCbwSize) {
      std::memcpy(&current_cbw_tag_, &data[offsetof(BulkCbWrap, tag)],
                  sizeof(current_cbw_tag_));
    }
    return data.size();
  }
  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCswSize) {
      BuildValidCsw(data, current_cbw_tag_, CswStatus::kCommandPassed);
      return testing::kUsbCswSize;
    }
    if (data.size() == kFlashWpBufferSize) {
      data[0] = static_cast<uint8_t>(FlashWpArea::kAll);
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

TEST(CliSpecialTest, ExecuteCommand_GetFlashWp_Success) {
  auto transport = std::make_unique<FakeForFlashWp>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kGetFlashWp};
  auto result = ExecuteCommand(config, device, DeviceMode::kBoot);
  EXPECT_TRUE(result.has_value());
}

TEST(CliSpecialTest, ExecuteCommand_SetFlashWp_Success) {
  auto transport = std::make_unique<FakeForSetWp>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kSetFlashWp, .wp_area = FlashWpArea::kAll};
  auto result = ExecuteCommand(config, device, DeviceMode::kBoot);
  EXPECT_TRUE(result.has_value());
}

}  // namespace focaltech
