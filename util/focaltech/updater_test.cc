/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

// Third-party libraries come AFTER standard C++ libraries
#include <openssl/sha.h>

#include "ft_scsi.h"
#include "mock_usb_transport.h"
#include "test_utils.h"
#include "usb_device.h"

namespace {

using namespace std::chrono_literals;
using ::focaltech::testing::BuildValidCsw;
using ::focaltech::testing::ComputeSha256;
using ::focaltech::testing::kUsbCbwSize;
using ::focaltech::testing::kUsbCswSize;
using ::focaltech::testing::MockUsbTransport;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class ScopedTempFile {
 public:
  explicit ScopedTempFile(std::span<const uint8_t> data) {
    path_ = std::filesystem::temp_directory_path() / "test_fw.bin";
    std::ofstream ofs(path_, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
  }
  ~ScopedTempFile() { std::filesystem::remove(path_); }
  std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

class StubUsbTransport : public UsbTransport {
 public:
  UsbDeviceId device_id() const override {
    return UsbDeviceId{.vid = 0x2808, .pid = 0x0001};
  }

  void SetExpectedHash(const std::array<uint8_t, SHA256_DIGEST_LENGTH>& hash) {
    expected_hash_ = hash;
  }

  void ForceVerifyFailure(bool force) { force_verify_fail_ = force; }

  const std::vector<uint32_t>& GetErasedAddresses() const {
    return erased_addresses_;
  }

  const std::vector<size_t>& GetDataPayloadSizes() const {
    return data_payload_sizes_;
  }

  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds) override {
    if (data.size() == kUsbCbwSize) {
      std::memcpy(&current_tag_, &data[4], sizeof(current_tag_));
    } else {
      data_payload_sizes_.push_back(data.size());
    }

    // Use structured packet inspection instead of magic indices.
    if (data.size() == sizeof(BulkCbWrap)) {
      const auto* cbw = reinterpret_cast<const BulkCbWrap*>(data.data());
      // Ensure the CBW contains an FocalTech command (opcode 0xDC).
      if (cbw->cmd[0] == kFtCmdAction) {
        const auto* ft_cmd = reinterpret_cast<const FtCmdFormat*>(cbw->cmd);
        if (ft_cmd->subcmd == kFtSubcmdErasePage) {
          erased_addresses_.push_back(ToLittleEndian(ft_cmd->address));
        }
      }
    }

    return static_cast<int>(data.size());
  }

  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds) override {
    if (data.size() == kUsbCswSize) {
      BuildValidCsw(data, current_tag_);
      return kUsbCswSize;
    }

    if (data.size() == kFtBootVersionMax) {
      std::ranges::copy(std::string("boot_v1.0"), data.begin());
      return kFtBootVersionMax;
    }

    if (data.size() == SHA256_DIGEST_LENGTH) {
      if (force_verify_fail_) {
        std::fill(data.begin(), data.end(), 0xAA);
      } else {
        std::ranges::copy(expected_hash_, data.begin());
      }
      return SHA256_DIGEST_LENGTH;
    }

    return static_cast<int>(data.size());
  }

  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds) override {
    return {};
  }

 private:
  bool force_verify_fail_ = false;
  std::vector<uint32_t> erased_addresses_;
  std::vector<size_t> data_payload_sizes_;
  std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_hash_{};
  uint32_t current_tag_ = 1;
};

}  // namespace

TEST(UpdaterTest, UpdateFw_SuccessBootloaderMode) {
  FtConfigPage config{.code_valid_control_word = kCodeValidMagic,
                      .code_start_address = kFocalBootRunAddr};

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + kFocalConfigSize, 0xBB);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  auto expected_hash = ComputeSha256(fw_data);
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->SetExpectedHash(expected_hash);
  UsbDevice device(std::move(transport));

  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);
  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterTest, UpdateFw_FailsOnMissingConfigPage) {
  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + kFocalConfigSize, 0xFF);
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kParameterError);
}

TEST(UpdaterTest, UpdateFw_PadsPartialPayloadsToWordBoundary) {
  std::vector<uint8_t> fw_data(kFocalConfigSize + 13, 0xAA);

  FtConfigPage config{.code_valid_control_word = kCodeValidMagic,
                      .code_start_address = kFocalBootRunAddr};
  std::memcpy(fw_data.data(), &config, sizeof(config));

  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->SetExpectedHash(ComputeSha256(fw_data));
  auto* raw_transport = transport.get();

  UsbDevice device(std::move(transport));
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);
  EXPECT_TRUE(res.has_value());

  const auto& payload_sizes = raw_transport->GetDataPayloadSizes();
  EXPECT_TRUE(std::ranges::contains(payload_sizes, 16))
      << "Expected a 16-byte padded transmission for the 13-byte remainder.";
}

TEST(UpdaterTest, UpdateFw_FailsOnFileTooSmall) {
  std::vector<uint8_t> undersized_data(sizeof(FtConfigPage) - 1, 0xFF);
  ScopedTempFile fw_file(undersized_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));

  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kParameterError);
}

TEST(UpdaterTest, UpdateFw_FailsOnModeMismatch) {
  FtConfigPage config{.code_valid_control_word = kCodeValidMagic,
                      .code_start_address = kFocalRomRunAddr};

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + kFocalConfigSize, 0xFF);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));

  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kParameterError);
}

TEST(UpdaterTest, UpdateFw_VerifyFails_AbortsAndErasesConfig) {
  FtConfigPage config{.code_valid_control_word = kCodeValidMagic,
                      .code_start_address = kFocalBootRunAddr};

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + kFocalConfigSize, 0x00);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->ForceVerifyFailure(true);

  auto* raw_transport = transport.get();
  UsbDevice device(std::move(transport));
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  auto res = UpdateFw(device, info);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kResultFail);

  const auto& erased = raw_transport->GetErasedAddresses();
  EXPECT_TRUE(std::ranges::contains(erased, kFocalBootCfgAddr))
      << "Abort sequence failed to erase the config page.";
}

TEST(UpdaterTest, FtGetFlashProtect_Success) {
  auto mock = std::make_unique<MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FtFlashWpArea::kRo);
            return kFtFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = FtGetFlashProtect(device, FtDeviceMode::kBoot);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, FtFlashWpArea::kRo);
}

TEST(UpdaterTest, FtGetFlashProtect_FailsNotInBootMode) {
  auto mock = std::make_unique<MockUsbTransport>();
  UsbDevice device(std::move(mock));

  auto res = FtGetFlashProtect(device, FtDeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), FtError::kInvalidMode);
}

TEST(UpdaterTest, FtSetFlashProtect_Success) {
  auto mock = std::make_unique<MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            return kFtFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return kUsbCswSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FtFlashWpArea::kRw);
            return kFtFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 2);
            return kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = FtSetFlashProtect(device, FtFlashWpArea::kRw, FtDeviceMode::kBoot);

  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterTest, FtBackToRomBoot_Success) {
  auto mock = std::make_unique<MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            BuildValidCsw(data, 1);
            return kUsbCswSize;
          }));

  EXPECT_CALL(*mock, WaitForReset(_))
      .WillOnce(Return(std::expected<void, FtError>()));

  UsbDevice device(std::move(mock));
  auto res = FtBackToRomBoot(device, FtDeviceMode::kBoot);

  EXPECT_TRUE(res.has_value());
}
