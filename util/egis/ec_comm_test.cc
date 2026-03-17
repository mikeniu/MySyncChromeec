/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_comm.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "fake_usb_comm.h"

namespace egis {
namespace {

class EcCommTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> MakeMockResponse(uint16_t result_code,
                                        std::span<const uint8_t> payload = {}) {
    EcHostCommandResponse hdr{
        .protocol_version = 3,
        .checksum = 0,
        .result = ToLittleEndian(result_code),
        .data_length = ToLittleEndian(static_cast<uint16_t>(payload.size()))};

    auto packet = PackMessage(hdr, payload);
    ApplyEcChecksum(packet);
    return packet;
  }

  FakeUsbComm fake_usb_;
  EcComm ec_{fake_usb_};
};

struct TestResponsePayload {
  uint32_t value1;
  uint8_t value2;
} __attribute__((packed));

TEST_F(EcCommTest, SendCommandSuccess) {
  fake_usb_.PushDeviceResponse(MakeMockResponse(0));

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
  EXPECT_EQ(fake_usb_.GetHostTransmissions().size(), 1);
}

TEST_F(EcCommTest, SendCommandWithPayloadSuccess) {
  const auto mock_payload = std::to_array<uint8_t>({0xAA, 0xBB, 0xCC});
  fake_usb_.PushDeviceResponse(MakeMockResponse(0, mock_payload));

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), mock_payload.size());
  EXPECT_TRUE(std::equal(result.value().begin(), result.value().end(),
                         mock_payload.begin()));
}

TEST_F(EcCommTest, SendCommandTypedSuccess) {
  TestResponsePayload mock_payload{
      .value1 = ToLittleEndian(0xDEADBEEF),
      .value2 = 0x42,
  };
  fake_usb_.PushDeviceResponse(MakeMockResponse(0, AsBytes(mock_payload)));

  auto result = ec_.SendCommandTyped<TestResponsePayload>(
      std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value1, mock_payload.value1);
  EXPECT_EQ(result->value2, mock_payload.value2);
}

TEST_F(EcCommTest, InvalidChecksumIsRejected) {
  auto packet = MakeMockResponse(0);

  packet[offsetof(EcHostCommandResponse, checksum)] ^= 0xFF;
  fake_usb_.PushDeviceResponse(packet);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kInvalidChecksum);
}

TEST_F(EcCommTest, InvalidProtocolVersionIsRejected) {
  auto packet = MakeMockResponse(0);

  packet[offsetof(EcHostCommandResponse, protocol_version)] = 4;

  ApplyEcChecksum(packet);
  fake_usb_.PushDeviceResponse(packet);
  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kInvalidChecksum);
}

TEST_F(EcCommTest, ResponseTooShortForHeader) {
  const std::vector<uint8_t> short_response = {1, 2, 3, 4};
  fake_usb_.PushDeviceResponse(short_response);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

TEST_F(EcCommTest, TruncatedPayloadIsCaught) {
  const auto partial_payload = std::to_array<uint8_t>({1, 2, 3, 4});
  auto packet = MakeMockResponse(0, partial_payload);

  *reinterpret_cast<uint16_t*>(
      &packet[offsetof(EcHostCommandResponse, data_length)]) =
      ToLittleEndian(static_cast<uint16_t>(64));

  ApplyEcChecksum(packet);
  fake_usb_.PushDeviceResponse(packet);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

TEST_F(EcCommTest, SendCommandTypedPayloadTooShort) {
  const auto short_payload = std::to_array<uint8_t>({0xAA, 0xBB});
  fake_usb_.PushDeviceResponse(MakeMockResponse(0, short_payload));

  auto result = ec_.SendCommandTyped<TestResponsePayload>(
      std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

TEST_F(EcCommTest, DeviceErrorIsMapped) {
  uint16_t access_denied_code =
      std::to_underlying(EcCommandResult::kAccessDenied);
  fake_usb_.PushDeviceResponse(MakeMockResponse(access_denied_code));

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kDeviceError);
}

TEST_F(EcCommTest, UsbSendFailure) {
  fake_usb_.SetSendError(UsbError::kTransferFailed);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kUsbSendFailed);
}

TEST_F(EcCommTest, UsbReceiveFailure) {
  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kUsbReceiveFailed);
}
TEST_F(EcCommTest, UsbPaddingIsIgnored) {
  const std::vector<uint8_t> mock_payload = {0x01, 0x02, 0x03};
  auto packet = MakeMockResponse(0, mock_payload);

  // Simulate USB bulk endpoint padding (e.g., to a 64-byte frame boundary)
  constexpr size_t kUsbPaddedSize = 64;
  packet.resize(kUsbPaddedSize, 0xAA);

  fake_usb_.PushDeviceResponse(packet);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), mock_payload);
}
}  // namespace
}  // namespace egis
