/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bootrom.h"

#include <algorithm>
#include <array>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fake_usb_comm.h"

namespace egis {
namespace {

using namespace std::chrono_literals;

constexpr uint32_t kTestVersion = 0x01020304;
constexpr uint32_t kTestMaxCmdSize = 1024;
constexpr uint32_t kTestMaxFwUpgradeSize = 2048;
constexpr uint8_t kTestKeyFillValue = 0x77;

constexpr auto kTestPlaintext =
    std::to_array<uint8_t>({0x11, 0x22, 0x33, 0x44});

class BootromCommTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Standard setup: AES-GCM with a known key
    test_key_.fill(kTestKeyFillValue);
    bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kAesGcm);
    bootrom_comm_.SetAesKey(test_key_);
  }

  std::vector<uint8_t> MakeMockAesResponse(std::span<const uint8_t> plaintext,
                                           bool corrupt_tag = false) {
    std::array<uint8_t, crypto::kAesGcmTagSize> tag{};
    std::vector<uint8_t> ciphertext(plaintext.size());
    uint32_t aad = ToLittleEndian(bootrom_protocol::kChipIdAad);

    auto iv_res = crypto::GenerateGcmIv();
    EXPECT_TRUE(iv_res.has_value());
    std::array<uint8_t, crypto::kAesGcmIvSize> iv =
        iv_res.value_or(std::array<uint8_t, crypto::kAesGcmIvSize>{});

    EXPECT_TRUE(crypto::AesGcmEncrypt(test_key_, iv, AsBytes(aad), plaintext,
                                      ciphertext, tag)
                    .has_value());

    if (corrupt_tag) tag[0] ^= 0xFF;

    bootrom_protocol::SecureResponseHeader hdr{
        .status = 0,
        .payload_len =
            ToLittleEndian(static_cast<uint32_t>(ciphertext.size()))};

    auto resp_packet = PackMessage(hdr, iv);
    resp_packet.append_range(tag);
    resp_packet.append_range(ciphertext);
    return resp_packet;
  }

  std::vector<uint8_t> MakeMockShaResponse(std::span<const uint8_t> payload) {
    bootrom_protocol::SecureResponseHeader hdr{
        .status = 0,
        .payload_len = ToLittleEndian(static_cast<uint32_t>(payload.size()))};
    return PackMessage(hdr, payload);
  }

  FakeUsbComm fake_usb_;
  BootromComm bootrom_comm_{fake_usb_};
  std::array<uint8_t, crypto::kAes256KeySize> test_key_{};
};

TEST_F(BootromCommTest, GetBootromInfoSuccess) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);

  bootrom_protocol::BootromInfoResponse info{
      .version = ToLittleEndian(kTestVersion),
      .max_cmd_size = ToLittleEndian(kTestMaxCmdSize),
      .max_fw_upgrade_size = ToLittleEndian(kTestMaxFwUpgradeSize)};

  uint16_t status = 0;
  fake_usb_.PushDeviceResponse(PackMessage(status, AsBytes(info)));

  auto res = bootrom_comm_.GetBootromInfo();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->version, kTestVersion);
}

TEST_F(BootromCommTest, GetFlashInfoSuccess) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);

  bootrom_protocol::FlashInfoResponse info{
      .flash_size = ToLittleEndian(static_cast<uint32_t>(2 * 1024 * 1024)),
  };
  std::string product_name = "TestFlash";
  std::ranges::copy(product_name, info.product_name);

  uint16_t status = 0;
  fake_usb_.PushDeviceResponse(PackMessage(status, AsBytes(info)));

  auto res = bootrom_comm_.GetFlashInfo();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->flash_size, 2 * 1024 * 1024);
  EXPECT_EQ(std::string_view(res->product_name), "TestFlash");
}

TEST_F(BootromCommTest, SecureCommandShaSuccess) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);
  bootrom_protocol::MemReadCommand read_cmd{
      .address = ToLittleEndian(bootrom_protocol::kFlashBaseAddr),
      .access_size = bootrom_protocol::AccessSize::kUint8,
      .count = ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size()))};

  fake_usb_.PushDeviceResponse(MakeMockShaResponse(kTestPlaintext));

  auto res = bootrom_comm_.SendStruct(read_cmd);

  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(*res, ::testing::ElementsAreArray(kTestPlaintext));
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
}

TEST_F(BootromCommTest, SecureCommandAesSuccess) {
  bootrom_protocol::MemReadCommand read_cmd{
      .address = ToLittleEndian(bootrom_protocol::kFlashBaseAddr),
      .access_size = bootrom_protocol::AccessSize::kUint8,
      .count = ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size()))};

  fake_usb_.PushDeviceResponse(MakeMockAesResponse(kTestPlaintext));

  auto res = bootrom_comm_.SendStruct(read_cmd);

  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(*res, ::testing::ElementsAreArray(kTestPlaintext));
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
}

TEST_F(BootromCommTest, SecureCommandAesAuthenticationFailure) {
  bootrom_protocol::MemReadCommand read_cmd{
      .address = ToLittleEndian(bootrom_protocol::kFlashBaseAddr),
      .access_size = bootrom_protocol::AccessSize::kUint8,
      .count = ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size()))};

  fake_usb_.PushDeviceResponse(
      MakeMockAesResponse(kTestPlaintext, /*corrupt_tag=*/true));

  auto res = bootrom_comm_.SendStruct(read_cmd);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kGcmAuthenticationFailed);
}

TEST_F(BootromCommTest, SecureCommandAesResponseTooShortForCryptoHeader) {
  bootrom_protocol::MemReadCommand read_cmd{
      .address = ToLittleEndian(bootrom_protocol::kFlashBaseAddr),
      .access_size = bootrom_protocol::AccessSize::kUint8,
      .count = ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size()))};

  // Create a response that is larger than the base SecureResponseHeader,
  // but smaller than the full AES header (Header + IV + Tag). This is the
  // exact condition that could trigger the underflow.
  // kScHeaderSizeAes = 6 (hdr) + 12 (iv) + 16 (tag) = 34 bytes.
  // Let's make a response of 20 bytes.
  bootrom_protocol::SecureResponseHeader hdr{
      .status = 0, .payload_len = ToLittleEndian(static_cast<uint32_t>(0))};
  auto packet = PackMessage(hdr);  // 6 bytes
  packet.resize(20, 0);            // resize to 20 bytes

  fake_usb_.PushDeviceResponse(packet);

  auto res = bootrom_comm_.SendStruct(read_cmd);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kResponseTruncated);
}

TEST_F(BootromCommTest, SystemResetDoesNotWaitForResponse) {
  auto res = bootrom_comm_.SystemReset();

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
  EXPECT_EQ(fake_usb_.Receive({}, 0ms).error(), UsbError::kTimeout);
}

TEST_F(BootromCommTest, SendBootromCommandEmptyBuffer) {
  auto res = bootrom_comm_.SendBootromCommand({});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kBufferTooSmall);
}

TEST_F(BootromCommTest, WriteFlashExceedsLimit) {
  std::vector<uint8_t> massive_payload(BootromComm::kMaxFlashWriteSize + 1,
                                       0xFF);
  auto res = bootrom_comm_.WriteFlash(0x0000, massive_payload);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kPayloadSizeExceedsLimit);
}

TEST_F(BootromCommTest, ReadFlashChunking) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);
  const uint32_t total_size =
      BootromComm::kMaxFlashReadSize + 1024;  // 5KB total

  std::vector<uint8_t> chunk1(BootromComm::kMaxFlashReadSize, 0xAA);
  std::vector<uint8_t> chunk2(1024, 0xBB);

  fake_usb_.PushDeviceResponse(MakeMockShaResponse(chunk1));
  fake_usb_.PushDeviceResponse(MakeMockShaResponse(chunk2));

  auto res = bootrom_comm_.ReadFlash(0x0000, total_size);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), total_size);
  EXPECT_EQ((*res)[0], 0xAA);
  EXPECT_EQ((*res)[BootromComm::kMaxFlashReadSize], 0xBB);
}

TEST_F(BootromCommTest, UsbSendFailurePropagates) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);
  fake_usb_.SetSendError(UsbError::kTransferFailed);

  auto res = bootrom_comm_.GetBootromInfo();

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kUsbWriteFailed);
}

TEST_F(BootromCommTest, FlashReadyTimeout) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);

  uint32_t stuck_sr1 = bootrom_protocol::kFlashSr1Wip;
  fake_usb_.SetStickyDeviceResponse(
      MakeMockShaResponse(AsBytes(ToLittleEndian(stuck_sr1))));

  auto res = bootrom_comm_.DisableWp();

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kFlashReadyTimeout);
}

TEST_F(BootromCommTest, WriteFlashOutOfBounds) {
  // Try to write 200 bytes starting 100 bytes from the end of the flash
  const uint32_t offset = bootrom_protocol::kFlashTotalSize - 100;
  std::vector<uint8_t> payload(200, 0xAA);

  auto res = bootrom_comm_.WriteFlash(offset, payload);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kInvalidFlashAddress);
}

TEST_F(BootromCommTest, ReadFlashOutOfBounds) {
  // Try to read exactly starting at the limit (which is 1 byte out of bounds)
  const uint32_t offset = bootrom_protocol::kFlashTotalSize;
  const uint32_t size = 1;

  auto res = bootrom_comm_.ReadFlash(offset, size);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kInvalidFlashAddress);
}

TEST_F(BootromCommTest, EraseFlashOutOfBounds) {
  // Try to erase more than the total flash size
  const uint32_t offset = 0x1000;
  const uint32_t size = bootrom_protocol::kFlashTotalSize;

  auto res = bootrom_comm_.EraseFlash(offset, size);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kInvalidFlashAddress);
}

TEST_F(BootromCommTest, FlashAddressIntegerOverflowProtection) {
  // Provide values that would wrap around a 32-bit unsigned integer.
  // 0xFFFFFFF0 + 0x20 = 0x00000010. If the overflow isn't caught safely,
  // 0x10 is less than kFlashTotalSize, and the check would improperly pass.
  const uint32_t offset = 0xFFFFFFF0;
  const uint32_t size = 0x20;

  auto res = bootrom_comm_.ReadFlash(offset, size);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kInvalidFlashAddress);
}

TEST_F(BootromCommTest, SecureCommandResponsePayloadTruncated) {
  bootrom_comm_.SetCryptoAlgorithm(EgisCryptoType::kSha256);

  bootrom_protocol::MemReadCommand read_cmd{
      .address = ToLittleEndian(bootrom_protocol::kFlashBaseAddr),
      .access_size = bootrom_protocol::AccessSize::kUint8,
      .count = ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size()))};

  // The header claims the payload is strictly larger than what we will actually
  // send
  bootrom_protocol::SecureResponseHeader hdr{
      .status = 0,
      .payload_len =
          ToLittleEndian(static_cast<uint32_t>(kTestPlaintext.size() + 1))};

  // Pack the header with the original (shorter) kTestPlaintext
  fake_usb_.PushDeviceResponse(PackMessage(hdr, kTestPlaintext));

  auto res = bootrom_comm_.SendStruct(read_cmd);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kResponseTruncated);
}

}  // namespace
}  // namespace egis
