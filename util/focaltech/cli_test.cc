/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "updater.h"
#include "usb_device.h"

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;

constexpr size_t kUsbCbwSize = 31;
constexpr size_t kUsbCswSize = 13;

static UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

// -----------------------------------------------------------------------------
// Enhanced fake transport that supports multiple responses and selective send
// failures.
// -----------------------------------------------------------------------------
class FakeEcTransport : public UsbTransport {
 public:
  explicit FakeEcTransport(UsbDeviceId id = {kFocalEcVid, kFocalEcPidTest})
      : device_id_(id) {}

  // Add a response that will be returned for the next command (FIFO).
  void AddResponse(std::vector<uint8_t> response) {
    responses_.push_back(std::move(response));
  }

  // Make the Nth send (0‑based) fail with FtError::kNoSuchDevice.
  void FailSendOnCommand(size_t cmd_index) { fail_on_command_ = cmd_index; }

  // UsbTransport interface
  UsbDeviceId device_id() const override { return device_id_; }

  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds) override {
    last_sent_.assign(data.begin(), data.end());

    if (send_count_ == fail_on_command_) {
      ++send_count_;
      return std::unexpected(FtError::kNoSuchDevice);
    }
    ++send_count_;
    return static_cast<int>(data.size());
  }

  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds) override {
    if (response_idx_ >= responses_.size()) {
      // No response prepared – return 0 bytes (simulates empty read).
      return 0;
    }

    const auto& resp = responses_[response_idx_];
    if (recv_offset_ >= resp.size()) {
      // Fully consumed this response; move to next one for the next command.
      ++response_idx_;
      recv_offset_ = 0;
      if (response_idx_ >= responses_.size()) {
        return 0;
      }
      // Recursively call to serve the next response (should not happen in
      // normal flow because Recv is called once per command).
      return Recv(data, std::chrono::milliseconds(0));
    }

    size_t to_copy = std::min(data.size(), resp.size() - recv_offset_);
    memcpy(data.data(), resp.data() + recv_offset_, to_copy);
    recv_offset_ += to_copy;
    return static_cast<int>(to_copy);
  }

  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds) override {
    return {};
  }

  const std::vector<uint8_t>& last_sent() const { return last_sent_; }

 private:
  UsbDeviceId device_id_;
  std::vector<std::vector<uint8_t>> responses_;
  size_t response_idx_ = 0;
  size_t recv_offset_ = 0;
  size_t send_count_ = 0;
  size_t fail_on_command_ = static_cast<size_t>(-1);
  std::vector<uint8_t> last_sent_;
};

// -----------------------------------------------------------------------------
// Helper functions to build EC responses
// -----------------------------------------------------------------------------
std::vector<uint8_t> BuildEcResponse(uint16_t result,
                                     std::vector<uint8_t> data) {
  ec_host_response resp{};
  resp.struct_version = EC_HOST_RESPONSE_VERSION;  // = 3
  resp.result = result;
  resp.data_len = data.size();
  resp.reserved = 0;

  std::vector<uint8_t> packet(sizeof(resp) + data.size());
  memcpy(packet.data(), &resp, sizeof(resp));
  if (!data.empty()) {
    memcpy(packet.data() + sizeof(resp), data.data(), data.size());
  }

  uint8_t checksum = 0;
  for (uint8_t b : packet) checksum += b;
  packet[1] = -checksum;
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

// -----------------------------------------------------------------------------
// ParseArguments, ParseInt, DeviceInit tests (unchanged)
// -----------------------------------------------------------------------------
TEST(CliTest, ParseArguments_UpdateCommand) {
  std::vector<std::string_view> args = {"firmware.bin"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kUpdate);
  EXPECT_EQ(config->ec_bin, "firmware.bin");
  EXPECT_EQ(config->action_param, 0);
}

TEST(CliTest, ParseArguments_EnterBootloader) {
  std::vector<std::string_view> args = {"enter_bootloader"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kEnterBoot);
  EXPECT_TRUE(config->ec_bin.empty());
  EXPECT_EQ(config->action_param, 0);
}

TEST(CliTest, ParseArguments_EnterRom) {
  std::vector<std::string_view> args = {"enter_rom"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kEnterRom);
}

TEST(CliTest, ParseArguments_GetFlashWp) {
  std::vector<std::string_view> args = {"flash_get_protect"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kGetFlashWp);
}

TEST(CliTest, ParseArguments_SetFlashWp_Valid) {
  std::vector<std::string_view> args = {"flash_set_protect", "3"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kSetFlashWp);
  EXPECT_EQ(config->action_param, 3);
}

TEST(CliTest, ParseArguments_SetFlashWp_InvalidParam) {
  std::vector<std::string_view> args = {"flash_set_protect", "abc"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), FtError::kParameterError);
}

TEST(CliTest, ParseArguments_GetEcVersion) {
  std::vector<std::string_view> args = {"get_ec_version"};
  auto config = ParseArguments(args);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->cmd, Command::kGetEcVersion);
}

TEST(CliTest, ParseArguments_UnknownCommand) {
  std::vector<std::string_view> args = {"unknown"};
  auto config = ParseArguments(args);
  EXPECT_FALSE(config.has_value());
  EXPECT_EQ(config.error(), FtError::kParameterError);
}

TEST(CliTest, ParseInt_Valid) {
  auto res = ParseInt("42");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 42);
}

TEST(CliTest, ParseInt_Negative) {
  auto res = ParseInt("-5");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, -5);
}

TEST(CliTest, ParseInt_Invalid) {
  auto res = ParseInt("12a");
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kParameterError);
}

TEST(CliTest, DeviceInit_RomMode) {
  UsbDeviceId id{.vid = kFocalRomVid, .pid = kFocalRomPid};
  FtDeviceInfo info;
  auto res = DeviceInit(id, info);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(info.current_mode, FtDeviceMode::kRom);
}

TEST(CliTest, DeviceInit_BootMode) {
  UsbDeviceId id{.vid = kFocalBootVid, .pid = kFocalBootPid};
  FtDeviceInfo info;
  auto res = DeviceInit(id, info);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(info.current_mode, FtDeviceMode::kBoot);
}

TEST(CliTest, DeviceInit_EcTestMode) {
  UsbDeviceId id{.vid = kFocalEcVid, .pid = kFocalEcPidTest};
  FtDeviceInfo info;
  auto res = DeviceInit(id, info);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(info.current_mode, FtDeviceMode::kTest);
}

TEST(CliTest, DeviceInit_EcNormalMode) {
  UsbDeviceId id{.vid = kFocalEcVid, .pid = kFocalEcPid9865};
  FtDeviceInfo info;
  auto res = DeviceInit(id, info);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(info.current_mode, FtDeviceMode::kEc);
}

TEST(CliTest, DeviceInit_Unknown) {
  UsbDeviceId id{.vid = 0x1234, .pid = 0x5678};
  FtDeviceInfo info;
  auto res = DeviceInit(id, info);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kNoSuchDevice);
}

// -----------------------------------------------------------------------------
// EC protocol command tests
// -----------------------------------------------------------------------------
TEST(CliTest, GetBuildInfo_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildStringResponse("chojnik_v1.2.3"));

  auto result = GetBuildInfo(device);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "chojnik_v1.2.3");
}

TEST(CliTest, GetBuildInfo_NoDevice) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->FailSendOnCommand(0);  // First send fails with kNoSuchDevice

  auto result = GetBuildInfo(device);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}

TEST(CliTest, SwitchToBootloader_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildEmptySuccessResponse());

  auto result = SwitchToBootloader(device, 0);
  EXPECT_TRUE(result.has_value());

  const auto& sent = fake->last_sent();
  ASSERT_GE(sent.size(), sizeof(ec_host_request));
  const ec_host_request* req =
      reinterpret_cast<const ec_host_request*>(sent.data());
  EXPECT_EQ(req->command, EC_CMD_ENTER_BOOTLOADER);
}

TEST(CliTest, SwitchToRom_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildEmptySuccessResponse());

  auto result = SwitchToRom(device);
  EXPECT_TRUE(result.has_value());

  const auto& sent = fake->last_sent();
  ASSERT_GE(sent.size(), sizeof(ec_host_request));
  const ec_host_request* req =
      reinterpret_cast<const ec_host_request*>(sent.data());
  EXPECT_EQ(req->command, 0xFACE);
}

// -----------------------------------------------------------------------------
// CheckEcBuildVersion tests
// -----------------------------------------------------------------------------
TEST(CliTest, CheckEcBuildVersion_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildStringResponse("ft9001_v2.0"));

  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, CheckEcBuildVersion_UnsupportedProject) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildStringResponse("unsupported_project_v1.0"));

  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}

TEST(CliTest, CheckEcBuildVersion_RetrySuccess) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  // First attempt fails with device loss, second succeeds.
  fake->FailSendOnCommand(0);
  fake->AddResponse(BuildStringResponse("chojnik_v3.0"));

  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, CheckEcBuildVersion_AllRetriesFail) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  // Make all kMaxRetry attempts fail with device loss.
  for (int i = 0; i < kMaxRetry; ++i) {
    fake->FailSendOnCommand(i);
  }
  // No response added – will cause recv to return 0 after send succeeds,
  // which triggers "Insufficient data" error.
  // We actually want the send to always fail, so we already set failures.
  // No response needed.

  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

// -----------------------------------------------------------------------------
// SwitchMcuMode tests
// -----------------------------------------------------------------------------
TEST(CliTest, SwitchMcuMode_EnterBoot) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));

  // Command 0: GetBuildInfo (succeeds)
  fake->AddResponse(BuildStringResponse("ft9001_v1.0"));
  // Command 1: SwitchToBootloader – we simulate device loss.
  fake->FailSendOnCommand(1);
  // We don't need a response for the switch command because send fails.

  auto result = SwitchMcuMode(device, FtDeviceMode::kEc, Command::kEnterBoot);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, SwitchMcuMode_EnterRom) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));

  // Command 0: GetBuildInfo (succeeds)
  fake->AddResponse(BuildStringResponse("chojnik_v2.0"));
  // Command 1: SwitchToRom – device loss.
  fake->FailSendOnCommand(1);

  auto result = SwitchMcuMode(device, FtDeviceMode::kTest, Command::kEnterRom);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, SwitchMcuMode_FailsIfBuildVersionFails) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));

  // First command (GetBuildInfo) fails with device loss.
  fake->FailSendOnCommand(0);

  auto result = SwitchMcuMode(device, FtDeviceMode::kEc, Command::kEnterBoot);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

// -----------------------------------------------------------------------------
// ExecuteCommand tests for EC commands
// -----------------------------------------------------------------------------
TEST(CliTest, ExecuteCommand_GetEcVersion_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));
  fake->AddResponse(BuildStringResponse("ft9001_v2.0"));

  AppConfig config{.cmd = Command::kGetEcVersion};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kEc};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_EnterBoot_Success) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));

  fake->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake->FailSendOnCommand(1);  // Switch command fails with device loss

  AppConfig config{.cmd = Command::kEnterBoot};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kEc};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_EnterRom_FromTestMode) {
  auto transport = std::make_unique<FakeEcTransport>();
  auto* fake = transport.get();
  UsbDevice device = MakeDevice(std::move(transport));

  fake->AddResponse(BuildStringResponse("ft9001_v1.0"));
  fake->FailSendOnCommand(1);  // Switch command fails with device loss

  AppConfig config{.cmd = Command::kEnterRom};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kTest};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_UnknownCommand) {
  auto transport = std::make_unique<FakeEcTransport>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kUnknown};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kParameterError);
}

// -----------------------------------------------------------------------------
// SCSI-based command tests (GetFlashWp, SetFlashWp) – unchanged
// -----------------------------------------------------------------------------
static void BuildCsw(std::span<uint8_t> data, uint32_t tag, uint8_t status) {
  struct __attribute__((packed)) BulkCsWrap {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
  };
  BulkCsWrap csw{.signature = htole32(0x53425355),
                 .tag = htole32(tag),
                 .data_residue = 0,
                 .status = status};
  std::memcpy(data.data(), &csw, sizeof(csw));
}

TEST(CliTest, ExecuteCommand_GetFlashWp_Success) {
  class FakeForFlashWp : public UsbTransport {
   public:
    UsbDeviceId device_id() const override {
      return {kFocalBootVid, kFocalBootPid};
    }
    std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                     std::chrono::milliseconds) override {
      if (data.size() == kUsbCbwSize) {
        current_cbw_tag_ = *reinterpret_cast<const uint32_t*>(&data[4]);
      }
      return data.size();
    }
    std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                     std::chrono::milliseconds) override {
      if (data.size() == kUsbCswSize) {
        BuildCsw(data, current_cbw_tag_, 0);
        return kUsbCswSize;
      }
      if (data.size() == 512) {
        data[0] = static_cast<uint8_t>(FtFlashWpArea::kBoot);
        return 512;
      }
      return 0;
    }
    std::expected<void, FtError> WaitForReset(
        std::chrono::milliseconds) override {
      return {};
    }

   private:
    uint32_t current_cbw_tag_ = 0;
  };

  auto transport = std::make_unique<FakeForFlashWp>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kGetFlashWp};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_SetFlashWp_Success) {
  class FakeForSetWp : public UsbTransport {
   public:
    UsbDeviceId device_id() const override {
      return {kFocalBootVid, kFocalBootPid};
    }
    std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                     std::chrono::milliseconds) override {
      if (data.size() == kUsbCbwSize) {
        current_cbw_tag_ = *reinterpret_cast<const uint32_t*>(&data[4]);
      }
      return data.size();
    }
    std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                     std::chrono::milliseconds) override {
      if (data.size() == kUsbCswSize) {
        BuildCsw(data, current_cbw_tag_, 0);
        return kUsbCswSize;
      }
      if (data.size() == 512) {
        data[0] = static_cast<uint8_t>(FtFlashWpArea::kRo);
        return 512;
      }
      return 0;
    }
    std::expected<void, FtError> WaitForReset(
        std::chrono::milliseconds) override {
      return {};
    }

   private:
    uint32_t current_cbw_tag_ = 0;
  };

  auto transport = std::make_unique<FakeForSetWp>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kSetFlashWp, .action_param = 3};  // kRo
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}
