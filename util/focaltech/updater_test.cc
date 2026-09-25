/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

#include "cli.h"
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

using focaltech::testing::kArbitraryPayloadByte;
using focaltech::testing::ReturnValidCsw;
constexpr size_t kUnalignedRemainder = 13;
constexpr uint8_t kPaddingByte = 0xFF;
constexpr uint32_t kInvalidMagicNumber = 0xDEADBEEF;
constexpr uint8_t kZeroByte = 0x00;

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

  const std::vector<std::vector<uint8_t>>& GetDataPayloads() const {
    return data_payloads_;
  }

  std::expected<size_t, Error> Send(std::span<const uint8_t> data,
                                    std::chrono::milliseconds) override {
    if (data.size() == testing::kUsbCbwSize) {
      std::memcpy(&current_tag_, &data[offsetof(BulkCbw, tag)],
                  sizeof(current_tag_));
    } else {
      data_payload_sizes_.push_back(data.size());
      data_payloads_.push_back(std::vector<uint8_t>(data.begin(), data.end()));
    }

    if (data.size() == sizeof(BulkCbw)) {
      BulkCbw cbw;
      std::memcpy(&cbw, data.data(), sizeof(cbw));

      if (cbw.cmd.opcode == Opcode::kAction) {
        if (cbw.cmd.subcmd == Subcmd::kErasePage) {
          erased_addresses_.push_back(FromLittleEndian(cbw.cmd.address));
        }
      }
    }

    return data.size();
  }

  std::expected<size_t, Error> Recv(std::span<uint8_t> data,
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
        std::ranges::fill(data, 0xAA);
      } else {
        std::ranges::copy(expected_hash_, data.begin());
      }
      return SHA256_DIGEST_LENGTH;
    }

    return data.size();
  }

  std::expected<void, Error> WaitForReset(std::chrono::milliseconds) override {
    return {};
  }

 private:
  bool force_verify_fail_ = false;
  std::vector<uint32_t> erased_addresses_;
  std::vector<size_t> data_payload_sizes_;
  std::vector<std::vector<uint8_t>> data_payloads_;
  std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_hash_{};
  uint32_t current_tag_ = 1;
};

std::vector<uint8_t> ConfigPageToBytes(const ConfigPage& config) {
  std::vector<uint8_t> bytes(sizeof(ConfigPage) + kFocalConfigSize,
                             kArbitraryPayloadByte);
  std::ranges::copy(AsUint8Span(config), bytes.begin());
  return bytes;
}

class UpdaterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto transport = std::make_unique<StubUsbTransport>();
    stub_ = transport.get();
    device_ = std::make_unique<UsbDevice>(std::move(transport));
  }

  StubUsbTransport* stub_;
  std::unique_ptr<UsbDevice> device_;
};

}  // namespace

TEST_F(UpdaterTest, UpdateFirmware_SuccessBootloaderMode) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto firmware_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(firmware_data);

  stub_->SetExpectedHash(expected_hash);
  auto res = UpdateFirmware(*device_, firmware_data, DeviceMode::kBootloader);
  EXPECT_TRUE(res.has_value());
}

TEST_F(UpdaterTest, UpdateFirmware_FailsOnMissingConfigPage) {
  std::vector<uint8_t> firmware_data(sizeof(ConfigPage) + kFocalConfigSize,
                                     kArbitraryPayloadByte);
  auto res = UpdateFirmware(*device_, firmware_data, DeviceMode::kBootloader);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidFormat);
}

TEST_F(UpdaterTest, UpdateFirmware_PadsPartialPayloadsToWordBoundary) {
  std::vector<uint8_t> firmware_data(kFocalConfigSize + kUnalignedRemainder,
                                     kArbitraryPayloadByte);

  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  std::memcpy(firmware_data.data(), &config, sizeof(config));

  stub_->SetExpectedHash(CalculateSha256(firmware_data));
  auto* raw_stub = stub_;

  auto res = UpdateFirmware(*device_, firmware_data, DeviceMode::kBootloader);
  EXPECT_TRUE(res.has_value());

  const auto& payloads = raw_stub->GetDataPayloads();
  ASSERT_EQ(payloads.size(), 2);

  EXPECT_EQ(payloads[0].size(), kFocalConfigSize);
  EXPECT_EQ(payloads[1].size(), kFocalConfigSize);

  EXPECT_EQ(payloads[0][kUnalignedRemainder - 1], kArbitraryPayloadByte);
  EXPECT_EQ(payloads[0][kUnalignedRemainder], kPaddingByte);
  EXPECT_EQ(payloads[0].back(), kPaddingByte);
}

TEST_F(UpdaterTest, UpdateFirmware_FailsOnFileTooSmall) {
  std::vector<uint8_t> undersized_data(sizeof(ConfigPage) - 1,
                                       kArbitraryPayloadByte);
  auto res = UpdateFirmware(*device_, undersized_data, DeviceMode::kBootloader);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kHardwareFailure);
}

TEST_F(UpdaterTest, UpdateFirmware_FailsOnModeMismatch) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kRomRunAddr};

  auto firmware_data = ConfigPageToBytes(config);
  auto res = UpdateFirmware(*device_, firmware_data, DeviceMode::kBootloader);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidParameter);
}

TEST_F(UpdaterTest, UpdateFirmware_VerifyFails_AbortsAndErasesConfig) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto firmware_data = ConfigPageToBytes(config);
  std::fill(firmware_data.begin() + sizeof(ConfigPage), firmware_data.end(),
            kZeroByte);

  stub_->ForceVerifyFailure(true);
  auto* raw_stub = stub_;

  auto res = UpdateFirmware(*device_, firmware_data, DeviceMode::kBootloader);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kVerificationFailed);

  const auto& erased = raw_stub->GetErasedAddresses();
  EXPECT_TRUE(std::ranges::contains(erased, kBootConfigAddr));
}

TEST_F(UpdaterTest, GetFlashProtection_FailsNotInBootMode) {
  auto res = GetFlashProtection(*device_, DeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

TEST_F(UpdaterTest, ReturnToRomBoot_FailsInInvalidMode) {
  auto res = ReturnToRomBoot(*device_, DeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

namespace {

TEST(UpdaterMockTest,
     ExecuteCommand_UpdateFirmware_FailsAfterMaxRetriesExhausted) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillRepeatedly(Return(std::unexpected(Error::kHardwareFailure)));

  UsbDevice device(std::move(mock));
  auto firmware_data = ConfigPageToBytes(config);

  const std::string temp_path = "/tmp/test_firmware.bin";
  std::ofstream file(temp_path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(firmware_data.data()),
             firmware_data.size());
  file.close();

  AppConfig app_config{.cmd = Command::kUpdate, .firmware_path = temp_path};
  auto res = ExecuteCommand(app_config, device, DeviceMode::kBootloader);

  std::remove(temp_path.c_str());

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kHardwareFailure);
}

TEST(UpdaterMockTest,
     ExecuteCommand_UpdateFirmware_RetriesAndSucceedsOnTransientSuspend) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto mock = std::make_unique<testing::MockUsbTransport>();
  StubUsbTransport stub;

  auto firmware_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(firmware_data);
  stub.SetExpectedHash(expected_hash);

  const std::string temp_path = "/tmp/test_firmware.bin";
  std::ofstream file(temp_path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(firmware_data.data()),
             firmware_data.size());
  file.close();

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Return(std::unexpected(Error::kHardwareFailure)))
      .WillRepeatedly([&stub](auto data, auto timeout) {
        return stub.Send(data, timeout);
      });

  EXPECT_CALL(*mock, Recv(_, _))
      .WillRepeatedly([&stub](auto data, auto timeout) {
        return stub.Recv(data, timeout);
      });

  EXPECT_CALL(*mock, WaitForReset(_)).WillRepeatedly([&stub](auto timeout) {
    return stub.WaitForReset(timeout);
  });

  UsbDevice device(std::move(mock));
  AppConfig app_config{.cmd = Command::kUpdate, .firmware_path = temp_path};
  auto res = ExecuteCommand(app_config, device, DeviceMode::kBootloader);

  std::remove(temp_path.c_str());

  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterMockTest, UpdateFirmware_FailsOnInvalidMagicNumber) {
  ConfigPage config{.code_valid_control_word = kInvalidMagicNumber};
  auto firmware_data = ConfigPageToBytes(config);

  auto mock = std::make_unique<testing::MockUsbTransport>();
  EXPECT_CALL(*mock, Send(_, _)).Times(0);
  EXPECT_CALL(*mock, Recv(_, _)).Times(0);

  UsbDevice device(std::move(mock));
  auto res = UpdateFirmware(device, firmware_data, DeviceMode::kBootloader);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidFormat);
}

TEST(UpdaterMockTest, UpdateFirmware_FailsIfVerificationMismatches) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  auto firmware_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(firmware_data);

  StubUsbTransport stub;
  stub.SetExpectedHash(expected_hash);

  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillRepeatedly(Invoke(&stub, &StubUsbTransport::Send));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillRepeatedly([&stub](std::span<uint8_t> data, auto timeout) {
        auto res = stub.Recv(data, timeout);
        if (data.size() == SHA256_DIGEST_LENGTH) {
          data[0] ^= 0xFF;
        }
        return res;
      });

  UsbDevice device(std::move(mock));
  auto res = UpdateFirmware(device, firmware_data, DeviceMode::kBootloader);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kVerificationFailed);
}

TEST(UpdaterMockTest, GetFlashProtection_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Invoke([](std::span<uint8_t> data, auto) {
        std::ranges::fill(data, 0);
        data[0] = static_cast<uint8_t>(FlashWpArea::kRo);
        return kFlashWpBufferSize;
      }))
      .WillOnce(ReturnValidCsw(1));

  UsbDevice device(std::move(mock));
  auto res = GetFlashProtection(device, DeviceMode::kBootloader);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, FlashWpArea::kRo);
}

TEST(UpdaterMockTest, SetFlashProtection_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Return(kFlashWpBufferSize))
      .WillOnce(ReturnValidCsw(1))
      .WillOnce(Invoke([](std::span<uint8_t> data, auto) {
        std::ranges::fill(data, 0);
        data[0] = static_cast<uint8_t>(FlashWpArea::kAll);
        return kFlashWpBufferSize;
      }))
      .WillOnce(ReturnValidCsw(2));

  UsbDevice device(std::move(mock));
  auto res =
      SetFlashProtection(device, FlashWpArea::kAll, DeviceMode::kBootloader);
  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterMockTest, ReturnToRomBoot_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _)).WillOnce(ReturnValidCsw(1));

  EXPECT_CALL(*mock_ptr, WaitForReset(_))
      .WillOnce(Return(std::expected<void, Error>()));

  UsbDevice device(std::move(mock));
  auto res = ReturnToRomBoot(device, DeviceMode::kBootloader);
  EXPECT_TRUE(res.has_value());
}

}  // namespace

}  // namespace focaltech
