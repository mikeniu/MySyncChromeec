/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "mock_usb_transport.h"
#include "usb_transport.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Invoke;
using testing::MockUsbTransport;
using ::testing::Return;

constexpr size_t kArbitraryBufferSize = 10;

constexpr size_t kPartialReadSize = 5;
constexpr uint8_t kArbitraryDataByte = 0xAB;

class UsbDeviceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto m = std::make_unique<MockUsbTransport>();
    mock_ = m.get();
    device_ = std::make_unique<UsbDevice>(std::move(m));
  }

  MockUsbTransport* mock_;
  std::unique_ptr<UsbDevice> device_;
};

}  // namespace

TEST_F(UsbDeviceTest, SendForwardsToTransport) {
  std::vector<uint8_t> test_data = {0x01, 0x02, 0x03};
  auto timeout = 100ms;

  EXPECT_CALL(*mock_, Send(ElementsAreArray(test_data), timeout))
      .WillOnce(Return(test_data.size()));

  auto result = device_->Send(test_data, timeout);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, test_data.size());
}

TEST_F(UsbDeviceTest, SendHandlesError) {
  auto timeout = 100ms;

  EXPECT_CALL(*mock_, Send(_, timeout))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  std::vector<uint8_t> test_data = {0x01};
  auto result = device_->Send(test_data, timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kDeviceNotFound);
}

TEST_F(UsbDeviceTest, RecvForwardsToTransport) {
  std::vector<uint8_t> buffer(kArbitraryBufferSize, 0);
  auto timeout = 200ms;

  EXPECT_CALL(*mock_, Recv(_, timeout))
      .WillOnce(Invoke([&](std::span<uint8_t> data, auto) {
        std::ranges::fill(data.first(kPartialReadSize), kArbitraryDataByte);
        return std::expected<size_t, Error>(kPartialReadSize);
      }));

  auto result = device_->Recv(buffer, timeout);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, kPartialReadSize);
}

TEST_F(UsbDeviceTest, RecvHandlesError) {
  auto timeout = 200ms;

  EXPECT_CALL(*mock_, Recv(_, timeout))
      .WillOnce(Return(std::unexpected(Error::kHardwareFailure)));

  std::vector<uint8_t> buffer(kArbitraryBufferSize);
  auto result = device_->Recv(buffer, timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kHardwareFailure);
}

TEST_F(UsbDeviceTest, WaitForResetForwardsToTransport) {
  auto timeout = 500ms;

  EXPECT_CALL(*mock_, WaitForReset(timeout))
      .WillOnce(Return(std::expected<void, Error>()));

  auto result = device_->WaitForReset(timeout);
  EXPECT_TRUE(result.has_value());
}

TEST_F(UsbDeviceTest, WaitForResetHandlesError) {
  auto timeout = 500ms;

  EXPECT_CALL(*mock_, WaitForReset(timeout))
      .WillOnce(Return(std::unexpected(Error::kDeviceNotFound)));

  auto result = device_->WaitForReset(timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), Error::kDeviceNotFound);
}

TEST_F(UsbDeviceTest, DeviceIdReturnsFromTransport) {
  UsbDeviceId expected_id{.vid = 0x2808, .pid = 0x0001};

  EXPECT_CALL(*mock_, device_id()).WillRepeatedly(Return(expected_id));

  EXPECT_EQ(device_->device_id().vid, expected_id.vid);
  EXPECT_EQ(device_->device_id().pid, expected_id.pid);
}

TEST_F(UsbDeviceTest, GetNextCdbTagIncrements) {
  uint32_t tag1 = device_->GetNextCdbTag();
  uint32_t tag2 = device_->GetNextCdbTag();
  uint32_t tag3 = device_->GetNextCdbTag();

  EXPECT_EQ(tag1, 1);
  EXPECT_EQ(tag2, 2);
  EXPECT_EQ(tag3, 3);
}

TEST_F(UsbDeviceTest, GetNextCdbTagThreadSafe) {
  constexpr int kIterations = 1000;

  auto worker = [this]() {
    for (int i = 0; i < kIterations; ++i) {
      device_->GetNextCdbTag();
    }
  };

  std::thread t1(worker);
  std::thread t2(worker);
  t1.join();
  t2.join();

  EXPECT_EQ(device_->GetNextCdbTag(),
            static_cast<uint32_t>((kIterations * 2) + 1));
}

TEST(UsbDeviceMoveTest, MoveConstructorTransfersOwnership) {
  auto mock = std::make_unique<MockUsbTransport>();
  MockUsbTransport* raw_ptr = mock.get();

  UsbDevice device1(std::move(mock));
  UsbDevice device2(std::move(device1));

  EXPECT_CALL(*raw_ptr, device_id())
      .WillOnce(Return(UsbDeviceId{.vid = 0x1234, .pid = 0x5678}));
  EXPECT_EQ(device2.device_id().vid, 0x1234);
}

TEST(UsbDeviceMoveTest, MoveAssignmentTransfersOwnership) {
  auto mock1 = std::make_unique<MockUsbTransport>();
  auto mock2 = std::make_unique<MockUsbTransport>();
  MockUsbTransport* raw_ptr1 = mock1.get();

  UsbDevice device1(std::move(mock1));
  UsbDevice device2(std::move(mock2));
  device2 = std::move(device1);

  EXPECT_CALL(*raw_ptr1, device_id())
      .WillOnce(Return(UsbDeviceId{.vid = 0x1111, .pid = 0x2222}));
  EXPECT_EQ(device2.device_id().vid, 0x1111);
}

}  // namespace focaltech
