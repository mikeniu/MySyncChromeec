/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "mock_usb_comm.h"

namespace egis {
namespace {
using ::testing::_;
using ::testing::Return;

constexpr uint32_t kTestVersion = 0x01020304;
constexpr uint32_t kTestMaxCmdSize = 1024;
constexpr uint32_t kTestMaxFwUpgradeSize = 2048;
constexpr uint8_t kTestKeyFillValue = 0x77;
constexpr uint32_t kTestReadCount = 4;
const std::array<uint8_t, kTestReadCount> kTestPlaintext = {0x11, 0x22, 0x33,
                                                            0x44};

std::vector<uint8_t> MakeMockAesResponse(
    std::span<const uint8_t, crypto::kAes256KeySize> key,
    std::span<const uint8_t> plaintext, bool corrupt_tag = false) {
  std::array<uint8_t, crypto::kAesGcmIvSize> iv{};
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};
  std::vector<uint8_t> ciphertext(plaintext.size());
  uint32_t aad = ToLittleEndian(EgisBootrom::kChipIdAad);

  EXPECT_TRUE(crypto::GenerateGcmIv(iv).has_value());
  EXPECT_TRUE(
      crypto::AesGcmEncrypt(key, iv, AsBytes(aad), plaintext, ciphertext, tag)
          .has_value());

  if (corrupt_tag) tag[0] ^= 0xFF;

  BootromComm::SecureResponseHeader hdr{
      .status = 0,
      .payload_len = ToLittleEndian(static_cast<uint32_t>(ciphertext.size()))};

  auto resp_packet = PackMessage(hdr, iv);
  resp_packet.insert(resp_packet.end(), tag.begin(), tag.end());
  resp_packet.insert(resp_packet.end(), ciphertext.begin(), ciphertext.end());
  return resp_packet;
}

TEST(BootromCommTest, GetBootromInfoSuccess) {
  MockUsbComm mock_usb;
  BootromComm brom(mock_usb);

  EXPECT_CALL(mock_usb, Send(_, _))
      .WillOnce(Return(std::expected<void, UsbError>{}));
  EXPECT_CALL(mock_usb, Receive(_, _))
      .WillOnce([](std::span<uint8_t> out_buffer, auto) {
        uint16_t status = 0;
        EgisBootrom::BootromInfoResponse info{
            .version = ToLittleEndian(kTestVersion),
            .max_cmd_size = ToLittleEndian(kTestMaxCmdSize),
            .max_fw_upgrade_size = ToLittleEndian(kTestMaxFwUpgradeSize)};
        auto payload = PackMessage(status, AsBytes(info));
        std::ranges::copy(payload, out_buffer.begin());
        return static_cast<int>(payload.size());
      });

  auto res = brom.GetBootromInfo();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->version, kTestVersion);
}

TEST(BootromCommTest, SecureCommandAesSuccess) {
  MockUsbComm mock_usb;
  BootromComm brom(mock_usb);

  brom.SetWrappedAlgo(EgisCryptoType::kAesGcm);
  std::array<uint8_t, crypto::kAes256KeySize> key{};
  key.fill(kTestKeyFillValue);
  brom.SetAesKey(key);

  EgisBootrom::MemReadCommand read_cmd{
      .address = ToLittleEndian(EgisBootrom::kFlashBaseAddr),
      .access_size = EgisBootrom::kAccessSizeUint8,
      .count = ToLittleEndian(kTestReadCount)};

  EXPECT_CALL(mock_usb, Send(_, _))
      .WillOnce(Return(std::expected<void, UsbError>{}));
  EXPECT_CALL(mock_usb, Receive(_, _))
      .WillOnce([&key](std::span<uint8_t> out_buffer, auto) {
        auto resp_packet = MakeMockAesResponse(key, kTestPlaintext, false);
        std::ranges::copy(resp_packet, out_buffer.begin());
        return static_cast<int>(resp_packet.size());
      });

  auto res = brom.SendStruct(read_cmd);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), kTestReadCount);
  EXPECT_TRUE(std::equal(res->begin(), res->end(), kTestPlaintext.begin()));
}

TEST(BootromCommTest, SecureCommandAesAuthenticationFailure) {
  MockUsbComm mock_usb;
  BootromComm brom(mock_usb);

  brom.SetWrappedAlgo(EgisCryptoType::kAesGcm);
  std::array<uint8_t, crypto::kAes256KeySize> key{};
  key.fill(kTestKeyFillValue);
  brom.SetAesKey(key);

  EgisBootrom::MemReadCommand read_cmd{
      .address = ToLittleEndian(EgisBootrom::kFlashBaseAddr),
      .access_size = EgisBootrom::kAccessSizeUint8,
      .count = ToLittleEndian(kTestReadCount)};

  EXPECT_CALL(mock_usb, Send(_, _))
      .WillOnce(Return(std::expected<void, UsbError>{}));
  EXPECT_CALL(mock_usb, Receive(_, _))
      .WillOnce([&key](std::span<uint8_t> out_buffer, auto) {
        auto resp_packet = MakeMockAesResponse(key, kTestPlaintext, true);
        std::ranges::copy(resp_packet, out_buffer.begin());
        return static_cast<int>(resp_packet.size());
      });

  auto res = brom.SendStruct(read_cmd);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), BootromError::kGcmAuthenticationFailed);
}
}  // namespace
}  // namespace egis
