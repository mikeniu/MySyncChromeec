/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <endian.h>
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

#include "mock_usb_transport.h"
#include "usb_device.h"

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

constexpr size_t kUsbCbwSize = 31;
constexpr size_t kUsbCswSize = 13;
constexpr uint32_t kUsbMsCswSignature = 0x53425355;

// FocalTech constants for tests
constexpr uint32_t kFocalBootCfgAddr = 0x10080000;
constexpr uint32_t kFocalBootRunAddr = 0x10081000;
constexpr uint32_t kCodeValidMagic = 0x3AEC3721;

struct BulkCsWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

// RAII: Automatically deletes the test firmware file when the test finishes.
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

// Helper: compute SHA256 of a byte span.
static std::array<uint8_t, SHA256_DIGEST_LENGTH> ComputeSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

// Helper: build a valid CSW with the given tag
static void BuildValidCsw(std::span<uint8_t> data, uint32_t tag) {
  BulkCsWrap csw{.signature = htole32(kUsbMsCswSignature),
                 .tag = htole32(tag),
                 .data_residue = 0,
                 .status = 0};
  std::memcpy(data.data(), &csw, sizeof(csw));
}

// -----------------------------------------------------------------------------
// STUB TRANSPORT FOR ORCHESTRATION TESTS
// -----------------------------------------------------------------------------
class StubUsbTransport : public UsbTransport {
 public:
  UsbDeviceId device_id() const override { return {0x2808, 0x0001}; }

  void SetExpectedHash(const std::array<uint8_t, SHA256_DIGEST_LENGTH>& hash) {
    expected_hash_ = hash;
  }

  void ForceVerifyFailure(bool force) { force_verify_fail_ = force; }

  // Expose erased addresses to verify abort logic
  const std::vector<uint32_t>& GetErasedAddresses() const {
    return erased_addresses_;
  }

  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds) override {
    // Capture tag from CBW for later CSW generation
    if (data.size() == kUsbCbwSize) {
      current_tag_ = *reinterpret_cast<const uint32_t*>(&data[4]);
    }

    // Intercept Erase Page commands:
    // - CBW length == 31
    // - SCSI opcode (byte 15) == 0xDC (kFtCmdAction)
    // - Subcommand (byte 28) == 0x08 (kFtSubcmdErasePage)
    // - Address is at bytes 16..19 (little-endian)
    if (data.size() == kUsbCbwSize && data[15] == 0xDC && data[28] == 0x08) {
      uint32_t address = *reinterpret_cast<const uint32_t*>(&data[16]);
      erased_addresses_.push_back(le32toh(address));
    }
    return static_cast<int>(data.size());
  }

  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds) override {
    // CSW is always 13 bytes
    if (data.size() == kUsbCswSize) {
      BuildValidCsw(data, current_tag_);
      return kUsbCswSize;
    }

    // Boot version request (36 bytes expected)
    if (data.size() == 36) {
      std::ranges::copy(std::string("boot_v1.0"), data.begin());
      return 36;
    }

    // Verify hash request (32 bytes expected)
    if (data.size() == SHA256_DIGEST_LENGTH) {
      if (force_verify_fail_) {
        std::fill(data.begin(), data.end(), 0xAA);
      } else {
        std::ranges::copy(expected_hash_, data.begin());
      }
      return SHA256_DIGEST_LENGTH;
    }

    // For any other read, just return the requested number of bytes
    return static_cast<int>(data.size());
  }

  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds) override {
    return {};
  }

 private:
  bool force_verify_fail_ = false;
  std::vector<uint32_t> erased_addresses_;
  std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_hash_{};
  uint32_t current_tag_ = 1;
};

// -----------------------------------------------------------------------------
// UPDATE FW TESTS
// -----------------------------------------------------------------------------

TEST(UpdaterTest, UpdateFw_SuccessBootloaderMode) {
  FtConfigPage config{.code_valid_control_word = kCodeValidMagic,
                      .code_start_address = kFocalBootRunAddr};

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + 4096, 0xBB);
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
  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + 4096, 0xFF);
  ScopedTempFile fw_file(fw_data);
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
                      .code_start_address = 0x10002000};  // ROM run address

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + 4096, 0xFF);
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

  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + 4096, 0x00);
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

  // Verify that the config page was erased during abort
  const auto& erased = raw_transport->GetErasedAddresses();
  EXPECT_TRUE(std::ranges::contains(erased, kFocalBootCfgAddr))
      << "Abort sequence failed to erase the config page.";
}

// -----------------------------------------------------------------------------
// FLASH PROTECTION & MODE SWITCH TESTS
// -----------------------------------------------------------------------------

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
            return 512;
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

  // Sequence:
  // 1. Send CBW for SET command
  // 2. Recv data (dummy 512 bytes)
  // 3. Recv CSW for SET
  // 4. Send CBW for GET command (verification)
  // 5. Recv data (512 bytes containing the WP area)
  // 6. Recv CSW for GET

  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;  // SET CBW
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;  // GET CBW
      }));

  EXPECT_CALL(*mock, Recv(_, _))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // Data read for SET command (dummy 512 bytes)
            return 512;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // CSW for SET command
            BuildValidCsw(data, 1);
            return kUsbCswSize;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // Data read for GET command: return the WP area that was set (kRw)
            std::fill(data.begin(), data.end(), 0);
            data[0] = static_cast<uint8_t>(FtFlashWpArea::kRw);
            return 512;
          }))
      .WillOnce(Invoke(
          [](std::span<uint8_t> data, auto) -> std::expected<int, FtError> {
            // CSW for GET command
            BuildValidCsw(data, 2);
            return kUsbCswSize;
          }));

  UsbDevice device(std::move(mock));
  auto res = FtSetFlashProtect(device, FtFlashWpArea::kRw, FtDeviceMode::kBoot);

  EXPECT_TRUE(res.has_value());
}

TEST(UpdaterTest, FtBackToRomBoot_Success) {
  auto mock = std::make_unique<MockUsbTransport>();

  // Expect Erase Page CBW
  EXPECT_CALL(*mock, Send(_, _))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }))
      .WillOnce(Invoke([](auto, auto) -> std::expected<int, FtError> {
        return kUsbCbwSize;
      }));  // Soft Reset CBW

  // Expect Recv for CSW only on Erase Page (Reset is SendOnly)
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
