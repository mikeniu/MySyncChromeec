/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <bit>
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

// Test constants for padding/alignment tests.
constexpr size_t kUnalignedRemainder = 13;
constexpr uint8_t kPayloadByte = 0xAA;
constexpr uint8_t kPaddingByte = 0xFF;
constexpr uint8_t kDefaultFwByte = 0xBB;
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
      std::memcpy(&current_tag_, &data[offsetof(BulkCbWrap, tag)],
                  sizeof(current_tag_));
    } else {
      data_payload_sizes_.push_back(data.size());
      data_payloads_.push_back(std::vector<uint8_t>(data.begin(), data.end()));
    }

    if (data.size() == sizeof(BulkCbWrap)) {
      BulkCbWrap cbw;
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
        std::fill(data.begin(), data.end(), 0xAA);
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

// Helper to convert a ConfigPage to a byte vector for testing.
std::vector<uint8_t> ConfigPageToBytes(const ConfigPage& config) {
  std::vector<uint8_t> bytes(sizeof(ConfigPage) + kFocalConfigSize, 0xAA);
  std::memcpy(bytes.data(), &config, sizeof(config));
  return bytes;
}

// ========== Test Fixture (using StubUsbTransport) ==========
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

// ============================================================================
// Fixture-based tests (UpdaterTest) - using StubUsbTransport
// ============================================================================

TEST_F(UpdaterTest, UpdateFw_SuccessBootloaderMode) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto fw_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(fw_data);

  stub_->SetExpectedHash(expected_hash);
  auto res = UpdateFw(*device_, fw_data, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());
}

TEST_F(UpdaterTest, UpdateFw_FailsOnMissingConfigPage) {
  std::vector<uint8_t> fw_data(sizeof(ConfigPage) + kFocalConfigSize,
                               kDefaultFwByte);
  auto res = UpdateFw(*device_, fw_data, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  // The magic number is missing, so we get a format error.
  EXPECT_EQ(res.error(), Error::kFormatError);
}

TEST_F(UpdaterTest, UpdateFw_PadsPartialPayloadsToWordBoundary) {
  std::vector<uint8_t> fw_data(kFocalConfigSize + kUnalignedRemainder,
                               kPayloadByte);

  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  std::memcpy(fw_data.data(), &config, sizeof(config));

  stub_->SetExpectedHash(CalculateSha256(fw_data));
  auto* raw_stub = stub_;

  auto res = UpdateFw(*device_, fw_data, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());

  const auto& payloads = raw_stub->GetDataPayloads();
  ASSERT_EQ(payloads.size(), 2);

  EXPECT_EQ(payloads[0].size(), kFocalConfigSize);
  EXPECT_EQ(payloads[1].size(), kFocalConfigSize);

  EXPECT_EQ(payloads[0][kUnalignedRemainder - 1], kPayloadByte);
  EXPECT_EQ(payloads[0][kUnalignedRemainder], kPaddingByte);
  EXPECT_EQ(payloads[0].back(), kPaddingByte);
}

TEST_F(UpdaterTest, UpdateFw_FailsOnFileTooSmall) {
  std::vector<uint8_t> undersized_data(sizeof(ConfigPage) - 1, kDefaultFwByte);
  auto res = UpdateFw(*device_, undersized_data, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kResultFail);
}

TEST_F(UpdaterTest, UpdateFw_FailsOnModeMismatch) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kRomRunAddr};

  auto fw_data = ConfigPageToBytes(config);
  auto res = UpdateFw(*device_, fw_data, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kParameterError);
}

TEST_F(UpdaterTest, UpdateFw_VerifyFails_AbortsAndErasesConfig) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto fw_data = ConfigPageToBytes(config);
  std::fill(fw_data.begin() + sizeof(ConfigPage), fw_data.end(), kZeroByte);

  stub_->ForceVerifyFailure(true);
  auto* raw_stub = stub_;

  auto res = UpdateFw(*device_, fw_data, DeviceMode::kBoot);
  EXPECT_FALSE(res.has_value());
  // The verification now returns kVerifyFailed instead of kResultFail.
  EXPECT_EQ(res.error(), Error::kVerifyFailed);

  const auto& erased = raw_stub->GetErasedAddresses();
  EXPECT_TRUE(std::ranges::contains(erased, kBootCfgAddr));
}

TEST_F(UpdaterTest, UpdateFw_FailsInEcMode) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  auto fw_data = ConfigPageToBytes(config);

  auto res = UpdateFw(*device_, fw_data, DeviceMode::kEc);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

TEST_F(UpdaterTest, GetFlashProtection_FailsNotInBootMode) {
  auto res = GetFlashProtection(*device_, DeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

TEST_F(UpdaterTest, ReturnToRomBoot_FailsInInvalidMode) {
  auto res = ReturnToRomBoot(*device_, DeviceMode::kEc);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);

  res = ReturnToRomBoot(*device_, DeviceMode::kTest);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);

  res = ReturnToRomBoot(*device_, DeviceMode::kRom);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kInvalidMode);
}

// ============================================================================
// Mock-based tests (UpdaterMockTest) - using MockUsbTransport
// ============================================================================

namespace {

// ============================================================================
// Retry Loop Tests (via ExecuteCommand)
// ============================================================================

// Test 1: Persistent Failure - The loop gives up after exhausting all retries.
TEST(UpdaterMockTest, ExecuteCommand_UpdateFw_FailsAfterMaxRetriesExhausted) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  auto mock = std::make_unique<testing::MockUsbTransport>();

  EXPECT_CALL(*mock, Send(_, _))
      .WillRepeatedly(Return(std::unexpected(Error::kResultFail)));

  UsbDevice device(std::move(mock));
  auto fw_data = ConfigPageToBytes(config);

  // Write fw_data to a temporary file so ExecuteCommand can read it.
  const std::string temp_path = "/tmp/test_firmware.bin";
  std::ofstream file(temp_path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(fw_data.data()), fw_data.size());
  file.close();

  AppConfig app_config{.cmd = Command::kUpdate, .ec_bin = temp_path};
  auto res = ExecuteCommand(app_config, device, DeviceMode::kBoot);

  std::remove(temp_path.c_str());

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kResultFail);
}

// Test 2: Transient Failure - First attempt fails, second attempt succeeds.
TEST(UpdaterMockTest,
     ExecuteCommand_UpdateFw_RetriesAndSucceedsOnTransientSuspend) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};

  auto mock = std::make_unique<testing::MockUsbTransport>();
  StubUsbTransport stub;

  auto fw_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(fw_data);
  stub.SetExpectedHash(expected_hash);

  // Write fw_data to a temporary file.
  const std::string temp_path = "/tmp/test_firmware.bin";
  std::ofstream file(temp_path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(fw_data.data()), fw_data.size());
  file.close();

  // 1. Fail the first attempt (simulating the host suspending the MCU).
  // 2. Delegate everything else to the stub.
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Return(std::unexpected(Error::kResultFail)))
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
  AppConfig app_config{.cmd = Command::kUpdate, .ec_bin = temp_path};
  auto res = ExecuteCommand(app_config, device, DeviceMode::kBoot);

  std::remove(temp_path.c_str());

  EXPECT_TRUE(res.has_value());
}

// ============================================================================
// Firmware Validation (Brick Prevention)
// ============================================================================

// Test that an invalid magic number in the config page is rejected immediately.
TEST(UpdaterMockTest, UpdateFw_FailsOnInvalidMagicNumber) {
  // Create a config page with a bad magic number.
  ConfigPage config{.code_valid_control_word = 0xDEADBEEF};  // Invalid magic
  auto fw_data = ConfigPageToBytes(config);

  auto mock = std::make_unique<testing::MockUsbTransport>();
  // The UpdateFw function should never even attempt a USB transfer because
  // the magic number check happens before any I/O.
  EXPECT_CALL(*mock, Send(_, _)).Times(0);
  EXPECT_CALL(*mock, Recv(_, _)).Times(0);

  UsbDevice device(std::move(mock));
  auto res = UpdateFw(device, fw_data, DeviceMode::kBoot);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kFormatError);
}

// ============================================================================
// Post-Flash Verification Mismatch (Silent Corruption)
// ============================================================================

// Test that a hash mismatch during verification causes the update to fail.
TEST(UpdaterMockTest, UpdateFw_FailsIfVerificationMismatches) {
  ConfigPage config{.code_valid_control_word = kCodeValidMagic,
                    .code_start_address = kBootRunAddr};
  auto fw_data = ConfigPageToBytes(config);
  auto expected_hash = CalculateSha256(fw_data);

  // We'll use a real StubUsbTransport for the successful flash, but corrupt the
  // hash response.
  StubUsbTransport stub;
  stub.SetExpectedHash(expected_hash);

  auto mock = std::make_unique<testing::MockUsbTransport>();

  // Route Send directly to the stub without a lambda.
  EXPECT_CALL(*mock, Send(_, _))
      .WillRepeatedly(Invoke(&stub, &StubUsbTransport::Send));

  // Route Recv to the stub, but corrupt the hash response before returning.
  EXPECT_CALL(*mock, Recv(_, _))
      .WillRepeatedly([&stub](std::span<uint8_t> data, auto timeout) {
        auto res = stub.Recv(data, timeout);
        // If this is a hash response (SHA256_DIGEST_LENGTH bytes), flip a bit.
        if (data.size() == SHA256_DIGEST_LENGTH) {
          // Flip the first byte to simulate silent hardware corruption.
          data[0] ^= 0xFF;
        }
        return res;
      });

  UsbDevice device(std::move(mock));
  auto res = UpdateFw(device, fw_data, DeviceMode::kBoot);

  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), Error::kVerifyFailed);
}

// ============================================================================
// Individual updater function tests.
// ============================================================================

TEST(UpdaterMockTest, GetFlashProtection_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _)).WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FlashWpArea::kRo);
            return kFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = GetFlashProtection(device, DeviceMode::kBoot);
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
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FlashWpArea::kAll);
            return kFlashWpBufferSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 2);
            return testing::kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = SetFlashProtection(device, FlashWpArea::kAll, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterMockTest, ReturnToRomBoot_Success) {
  auto mock = std::make_unique<testing::MockUsbTransport>();
  auto* mock_ptr = mock.get();

  EXPECT_CALL(*mock_ptr, Send(_, _))
      .WillOnce(Return(testing::kUsbCbwSize))
      .WillOnce(Return(testing::kUsbCbwSize));

  EXPECT_CALL(*mock_ptr, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<size_t, Error> {
            testing::BuildValidCsw(data, 1);
            return testing::kUsbCswSize;
          }));

  EXPECT_CALL(*mock_ptr, WaitForReset(_))
      .WillOnce(Return(std::expected<void, Error>()));

  UsbDevice device(std::move(mock));
  auto res = ReturnToRomBoot(device, DeviceMode::kBoot);
  EXPECT_TRUE(res.has_value());
}

}  // namespace

}  // namespace focaltech
