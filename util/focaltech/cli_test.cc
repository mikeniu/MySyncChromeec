/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
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

// Mock USB transport for CLI tests that require hardware interaction.
class MockUsbTransport : public UsbTransport {
 public:
  MOCK_METHOD(UsbDeviceId, device_id, (), (const, override));
  MOCK_METHOD((std::expected<int, FtError>), Send,
              (std::span<const uint8_t>, std::chrono::milliseconds),
              (override));
  MOCK_METHOD((std::expected<int, FtError>), Recv,
              (std::span<uint8_t>, std::chrono::milliseconds), (override));
  MOCK_METHOD((std::expected<void, FtError>), WaitForReset,
              (std::chrono::milliseconds), (override));
};

// Helper to create a UsbDevice with a given transport.
static UsbDevice MakeDevice(std::unique_ptr<UsbTransport> transport) {
  return UsbDevice(std::move(transport));
}

// -----------------------------------------------------------------------------
// ParseArguments Tests
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

// -----------------------------------------------------------------------------
// ParseInt Utility Tests
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// DeviceInit Tests
// -----------------------------------------------------------------------------
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
// ExecuteCommand Tests using proper fake transports with CSW handling
// -----------------------------------------------------------------------------

// Helper: build a valid CSW.
static void BuildCsw(std::span<uint8_t> data, uint32_t tag, uint8_t status) {
  struct __attribute__((packed)) BulkCsWrap {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
  };
  BulkCsWrap csw{.signature = htole32(0x53425355),  // kUsbMsCswSignature
                 .tag = htole32(tag),
                 .data_residue = 0,
                 .status = status};
  std::memcpy(data.data(), &csw, sizeof(csw));
}

TEST(CliTest, ExecuteCommand_GetEcVersion_Success) {
  class FakeForVersion : public UsbTransport {
   public:
    UsbDeviceId device_id() const override {
      return {kFocalEcVid, kFocalEcPidTest};
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
      if (data.size() == 64) {
        const char* version = "chojnik_v1.2.3";
        size_t len = std::strlen(version);
        std::copy(version, version + len, data.begin() + 8);
        return 8 + len;
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

  auto transport = std::make_unique<FakeForVersion>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kGetEcVersion};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kTest};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
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
        // Return the newly set area (e.g., kRo)
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

TEST(CliTest, ExecuteCommand_EnterBoot_Success) {
  class FakeEnterBoot : public UsbTransport {
   public:
    UsbDeviceId device_id() const override {
      return {kFocalEcVid, kFocalEcPid9865};
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
      if (data.size() == 64) {
        const char* ver = "chojnik_v1.0";
        std::copy(ver, ver + strlen(ver), data.begin() + 8);
        return 8 + strlen(ver);
      }
      if (data.size() == 4) {
        // Dummy response for mode switch
        return 0;
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

  auto transport = std::make_unique<FakeEnterBoot>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kEnterBoot};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kEc};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, ExecuteCommand_UnknownCommand) {
  auto transport = std::make_unique<MockUsbTransport>();
  UsbDevice device = MakeDevice(std::move(transport));
  AppConfig config{.cmd = Command::kUnknown};
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot};

  auto result = ExecuteCommand(config, device, info);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kParameterError);
}

// -----------------------------------------------------------------------------
// CheckEcBuildVersion Tests (using proper fake with CSW)
// -----------------------------------------------------------------------------
TEST(CliTest, CheckEcBuildVersion_Success) {
  class FakeVersion : public UsbTransport {
   public:
    UsbDeviceId device_id() const override { return {0, 0}; }

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
      if (data.size() == 64) {
        const char* version = "ft9001_v2.0";
        std::copy(version, version + strlen(version), data.begin() + 8);
        return 8 + strlen(version);
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

  auto transport = std::make_unique<FakeVersion>();
  UsbDevice device = MakeDevice(std::move(transport));
  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_TRUE(result.has_value());
}

TEST(CliTest, CheckEcBuildVersion_UnsupportedProject) {
  class FakeVersionUnsupported : public UsbTransport {
   public:
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
      if (data.size() == 64) {
        const char* version = "unknown_project_v1.0";
        std::copy(version, version + strlen(version), data.begin() + 8);
        return 8 + strlen(version);
      }
      return 0;
    }

    UsbDeviceId device_id() const override { return {0, 0}; }
    std::expected<void, FtError> WaitForReset(
        std::chrono::milliseconds) override {
      return {};
    }

   private:
    uint32_t current_cbw_tag_ = 0;
  };
  auto transport = std::make_unique<FakeVersionUnsupported>();
  UsbDevice device = MakeDevice(std::move(transport));
  auto result = CheckEcBuildVersion(device, FtDeviceMode::kEc);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}
