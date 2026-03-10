/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

#include "ft_scsi.h"
#include "mock_usb_transport.h"
#include "test_utils.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
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

  std::expected<int, Error> Send(std::span<const uint8_t> data,
                                 std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCbwSize) {
      std::memcpy(&current_tag_, &data[4], sizeof(current_tag_));
    } else {
      data_payload_sizes_.push_back(data.size());
    }

    if (data.size() == sizeof(BulkCbWrap)) {
      BulkCbWrap cbw;
      std::memcpy(&cbw, data.data(), sizeof(cbw));

      if (cbw.cmd[0] == kCmdAction) {
        auto ft_cmd = std::bit_cast<CmdFormat>(cbw.cmd);

        if (ft_cmd.subcmd == kSubcmdErasePage) {
          erased_addresses_.push_back(ToLittleEndian(ft_cmd.address));
        }
      }
    }

    return static_cast<int>(data.size());
  }

  std::expected<int, Error> Recv(std::span<uint8_t> data,
                                 std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCswSize) {
      testing::BuildValidCsw(data, current_tag_);
      return testing::kUsbCswSize;
    }

    if (data.size() == kBootVersionMax) {
      std::ranges::copy(std::string("boot_v1.0"), data.begin());
      return kBootVersionMax;
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

  std::expected<void, Error> WaitForReset(std::chrono::milliseconds) override {
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
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  std::vector<uint8_t> fw_data(sizeof(ConfigPage) + kFocalConfigSize, 0xBB);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  auto expected_hash = testing::ComputeSha256(fw_data);
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->SetExpectedHash(expected_hash);
  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterTest, UpdateFw_FailsOnMissingConfigPage) {
  std::vector<uint8_t> fw_data(sizeof(ConfigPage) + kFocalConfigSize, 0xFF);
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kParameterError);
}

TEST(UpdaterTest, UpdateFw_PadsPartialPayloadsToWordBoundary) {
  std::vector<uint8_t> fw_data(kFocalConfigSize + 13, 0xAA);

  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  std::memcpy(fw_data.data(), &config, sizeof(config));

  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->SetExpectedHash(testing::ComputeSha256(fw_data));
  auto* raw_transport = transport.get();

  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());

  const auto& payload_sizes = raw_transport->GetDataPayloadSizes();
  EXPECT_TRUE(std::ranges::contains(payload_sizes, kFocalConfigSize))
      << "Expected a 4096-byte padded transmission for the 13-byte remainder.";
}

TEST(UpdaterTest, UpdateFw_FailsOnFileTooSmall) {
  std::vector<uint8_t> undersized_data(sizeof(ConfigPage) - 1, 0xFF);
  ScopedTempFile fw_file(undersized_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kResultFail);
}

TEST(UpdaterTest, UpdateFw_FailsOnModeMismatch) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kRomRunAddr};

  std::vector<uint8_t> fw_data(sizeof(ConfigPage) + kFocalConfigSize, 0xFF);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kParameterError);
}

TEST(UpdaterTest, UpdateFw_VerifyFails_AbortsAndErasesConfig) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  std::vector<uint8_t> fw_data(sizeof(ConfigPage) + kFocalConfigSize, 0x00);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  ScopedTempFile fw_file(fw_data);
  std::string fw_path = fw_file.path();

  auto transport = std::make_unique<StubUsbTransport>();
  transport->ForceVerifyFailure(true);

  auto* raw_transport = transport.get();
  UsbDevice device(std::move(transport));

  auto res = UpdateFw(device, fw_path, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kResultFail);

  const auto& erased = raw_transport->GetErasedAddresses();
  EXPECT_TRUE(std::ranges::contains(erased, kBootCfgAddr))
      << "Abort sequence failed to erase the config page.";
}

TEST(UpdaterTest, GetFlashProtection_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FlashWpArea::kRo);
            return kFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = GetFlashProtection(device, DeviceMode::kBoot);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, FlashWpArea::kRo);
}

TEST(UpdaterTest, GetFlashProtection_FailsNotInBootMode) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  UsbDevice device(std::move(mock));

  auto res = GetFlashProtection(device, DeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

TEST(UpdaterTest, SetFlashProtection_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            return kFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FlashWpArea::kRw);
            return kFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 2);
            return testing::kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = SetFlashProtection(device, FlashWpArea::kRw, DeviceMode::kBoot);

  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterTest, ReturnToRomBoot_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, Error> {
        return testing::kUsbCbwSize;
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  EXPECT_CALL(*mock, WaitForReset(_))
      .WillOnce(Return(std::expected<void, Error>()));

  UsbDevice device(std::move(mock));
  auto res = ReturnToRomBoot(device, DeviceMode::kBoot);

  EXPECT_TRUE(res.has_value());
}

}  // namespace focaltech
