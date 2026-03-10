/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <ranges>

// Third-party libraries come AFTER standard C++ libraries
#include <openssl/sha.h>

#include "usb_device.h"

constexpr size_t kUsbCbwSize = 31;
constexpr size_t kUsbCswSize = 13;
constexpr uint32_t kUsbMsCswSignature = 0x53425355;

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

// Fake device that simulates a real FocalTech fingerprint MCU in bootloader
// mode.
class FakeFocalDeviceTransport : public UsbTransport {
 public:
  UsbDeviceId device_id() const override { return {0x2808, 0x0001}; }

  void SetExpectedHash(const std::array<uint8_t, SHA256_DIGEST_LENGTH>& hash) {
    expected_hash_ = hash;
  }

  std::expected<int, FtError> Send(std::span<const uint8_t> data,
                                   std::chrono::milliseconds) override {
    if (data.size() == kUsbCbwSize) {
      // Extract tag from CBW (little-endian, bytes 4-7)
      current_cbw_tag_ = *reinterpret_cast<const uint32_t*>(&data[4]);
      last_cbw_.assign(data.begin(), data.end());
      ParseLastCommand();
      return data.size();
    }

    if (data.size() > kUsbCbwSize && expected_write_length_ > 0) {
      written_firmware_.insert(written_firmware_.end(), data.begin(),
                               data.end());
      return data.size();
    }
    return data.size();
  }

  std::expected<int, FtError> Recv(std::span<uint8_t> data,
                                   std::chrono::milliseconds) override {
    // CSW request (13 bytes)
    if (data.size() == kUsbCswSize) {
      BulkCsWrap csw{.signature = htole32(kUsbMsCswSignature),
                     .tag = htole32(current_cbw_tag_),
                     .data_residue = 0,
                     .status = 0};
      std::memcpy(data.data(), &csw, sizeof(csw));
      return sizeof(csw);
    }

    // Boot version request (36 bytes)
    if (data.size() == 36) {
      std::string ver = "boot_v1.0";
      std::ranges::copy(ver, data.begin());
      return ver.size();
    }

    // Verify command: return the pre‑computed hash
    if (data.size() == SHA256_DIGEST_LENGTH && verify_expected_) {
      std::ranges::copy(expected_hash_, data.begin());
      verify_expected_ = false;
      return SHA256_DIGEST_LENGTH;
    }

    std::fill(data.begin(), data.end(), 0);
    return data.size();
  }

  std::expected<void, FtError> WaitForReset(
      std::chrono::milliseconds) override {
    return {};
  }

 private:
  void ParseLastCommand() {
    if (last_cbw_.size() < 31) return;

    uint8_t opcode = last_cbw_[15];
    if (opcode == 0xDC) {  // kFtCmdAction
      // subcmd is at offset 28 (15 + 13) in the CBW
      uint8_t subcmd = last_cbw_[28];
      if (subcmd == 0x03) {  // kFtSubcmdWriteBin
        expected_write_length_ =
            *reinterpret_cast<const uint32_t*>(&last_cbw_[20]);
      } else if (subcmd == 0xBB) {  // kFtSubcmdVerify
        verify_expected_ = true;
      }
    }
  }

  std::vector<uint8_t> last_cbw_;
  uint32_t current_cbw_tag_ = 0;
  uint32_t expected_write_length_ = 0;
  bool verify_expected_ = false;
  std::vector<uint8_t> written_firmware_;
  std::array<uint8_t, SHA256_DIGEST_LENGTH>
      expected_hash_{};  // Pre‑computed hash
};

TEST(UpdaterTest, UpdateFwSuccessBootloaderMode) {
  // 1. Build a valid config page.
  FtConfigPage config{};
  config.code_valid_control_word = 0x3AEC3721;  // Magic number
  config.code_start_address = 0x10081000;       // Bootloader run address

  // 2. Prepend the config page to the firmware data.
  std::vector<uint8_t> fw_data(sizeof(FtConfigPage) + 4096, 0xFF);
  std::memcpy(fw_data.data(), &config, sizeof(config));
  // Dummy code byte after config page (prevents empty firmware rejection)
  fw_data[sizeof(FtConfigPage)] = 0x00;

  // 3. Compute the correct SHA256 of the complete firmware file.
  auto expected_hash = ComputeSha256(fw_data);

  // 4. Create a temporary firmware file.
  ScopedTempFile fw_file(fw_data);

  // 5. Create the fake transport and inject the expected hash.
  auto transport = std::make_unique<FakeFocalDeviceTransport>();
  transport->SetExpectedHash(expected_hash);
  UsbDevice device(std::move(transport));

  std::string fw_path = fw_file.path();
  FtDeviceInfo info{.current_mode = FtDeviceMode::kBoot, .ec_bin = fw_path};

  // 6. Attempt the firmware update.
  auto update_res = UpdateFw(device, info);

  EXPECT_TRUE(update_res.has_value());
}  // fw_file destructor automatically deletes the temporary file.
