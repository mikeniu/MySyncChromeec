/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_comm.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "fake_usb_comm.h"

namespace egis {
namespace {

// ============================================================================
// Test Fixture for EC Communication
// ============================================================================
class EcCommTest : public ::testing::Test {
 protected:
  // Helper to construct mathematically sound EC responses with valid checksums
  std::vector<uint8_t> MakeMockResponse(uint16_t result_code,
                                        std::span<const uint8_t> payload = {}) {
    EcHostCommandResponse hdr{
        .protocol_version = 3,
        .checksum = 0,  // Calculated below
        .result = ToLittleEndian(result_code),
        .data_length = ToLittleEndian(static_cast<uint16_t>(payload.size()))};

    auto packet = PackMessage(hdr, payload);

    // Calculate the two's complement checksum required by the EC protocol
    uint32_t sum = 0;
    for (uint8_t byte : packet) sum += byte;

    packet[offsetof(EcHostCommandResponse, checksum)] =
        static_cast<uint8_t>(-sum);
    return packet;
  }

  FakeUsbComm fake_usb_;
  EcComm ec_{fake_usb_};
};

struct TestResponsePayload {
  uint32_t value1;
  uint8_t value2;
} __attribute__((packed));

// --- Success Flows ---

TEST_F(EcCommTest, SendCommandSuccess) {
  // Push a successful response with no payload
  fake_usb_.PushDeviceResponse(MakeMockResponse(0));

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());

  // Verify the host properly dispatched the command packet
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

// --- Protocol Integrity Boundaries ---

TEST_F(EcCommTest, InvalidChecksumIsRejected) {
  auto packet = MakeMockResponse(0);

  // Flip bits in the checksum byte to simulate line corruption
  packet[offsetof(EcHostCommandResponse, checksum)] ^= 0xFF;
  fake_usb_.PushDeviceResponse(packet);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kInvalidChecksum);
}

TEST_F(EcCommTest, InvalidProtocolVersionIsRejected) {
  auto packet = MakeMockResponse(0);

  // The EC protocol dictates version 3.
  packet[offsetof(EcHostCommandResponse, protocol_version)] = 4;

  // Recalculate checksum after modification.
  packet[offsetof(EcHostCommandResponse, checksum)] = 0;
  uint32_t sum = 0;
  for (uint8_t byte : packet) sum += byte;
  packet[offsetof(EcHostCommandResponse, checksum)] =
      static_cast<uint8_t>(-sum);

  fake_usb_.PushDeviceResponse(packet);
  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kInvalidChecksum);
}

TEST_F(EcCommTest, ResponseTooShortForHeader) {
  // A response with only 4 bytes is too short for a full header.
  const std::vector<uint8_t> short_response = {1, 2, 3, 4};
  fake_usb_.PushDeviceResponse(short_response);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

TEST_F(EcCommTest, TruncatedPayloadIsCaught) {
  // Construct a header that claims a 64-byte payload is attached
  const auto partial_payload = std::to_array<uint8_t>({1, 2, 3, 4});
  auto packet = MakeMockResponse(0, partial_payload);

  // Manually overwrite the data_length field to an invalid value
  *reinterpret_cast<uint16_t*>(
      &packet[offsetof(EcHostCommandResponse, data_length)]) =
      ToLittleEndian(static_cast<uint16_t>(64));

  // After corrupting the length, the original checksum is now invalid. We must
  // recalculate it for the test to proceed past the checksum check.
  packet[offsetof(EcHostCommandResponse, checksum)] = 0;
  uint32_t sum = 0;
  for (uint8_t byte : packet) sum += byte;
  packet[offsetof(EcHostCommandResponse, checksum)] =
      static_cast<uint8_t>(-sum);

  fake_usb_.PushDeviceResponse(packet);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  // The parser must catch the size mismatch and abort safely
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

TEST_F(EcCommTest, SendCommandTypedPayloadTooShort) {
  // Payload is only 2 bytes, but TestResponsePayload is 5 bytes.
  const auto short_payload = std::to_array<uint8_t>({0xAA, 0xBB});
  fake_usb_.PushDeviceResponse(MakeMockResponse(0, short_payload));

  auto result = ec_.SendCommandTyped<TestResponsePayload>(
      std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kResponseTooShort);
}

// --- Device State Handling ---

TEST_F(EcCommTest, DeviceErrorIsMapped) {
  // Simulate the EC returning kAccessDenied (0x0004)
  uint16_t access_denied_code =
      std::to_underlying(EcCommandResult::kAccessDenied);
  fake_usb_.PushDeviceResponse(MakeMockResponse(access_denied_code));

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kDeviceError);
}

TEST_F(EcCommTest, UsbSendFailure) {
  // Assuming FakeUsbComm has a method to simulate send failures.
  fake_usb_.SetSendError(UsbError::kTransferFailed);

  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kUsbSendFailed);
}

TEST_F(EcCommTest, UsbReceiveFailure) {
  // The Fake queue is empty, so Receive() will immediately return kTimeout
  auto result =
      ec_.SendCommand(std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EcError::kUsbReceiveFailed);
}
}  // namespace
}  // namespace egis
