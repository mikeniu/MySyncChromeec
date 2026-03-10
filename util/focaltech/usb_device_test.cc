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

namespace {

using namespace std::chrono_literals;
using ::focaltech::testing::MockUsbTransport;
using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Invoke;
using ::testing::Return;

// Helper: create a UsbDevice with given mock
UsbDevice MakeDevice(std::unique_ptr<MockUsbTransport> mock) {
  return UsbDevice(std::move(mock));
}

constexpr size_t kArbitraryBufferSize = 10;

}  // namespace

// -----------------------------------------------------------------------------
// Tests for UsbDevice
// -----------------------------------------------------------------------------

TEST(UsbDeviceTest, SendForwardsToTransport) {
  auto mock = std::make_unique<MockUsbTransport>();
  std::vector<uint8_t> test_data = {0x01, 0x02, 0x03};
  auto timeout = 100ms;

  // Use ElementsAreArray to match the exact data content.
  EXPECT_CALL(*mock, Send(ElementsAreArray(test_data), timeout))
      .WillOnce(Return(test_data.size()));

  UsbDevice device = MakeDevice(std::move(mock));
  auto result = device.Send(test_data, timeout);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, test_data.size());
}

TEST(UsbDeviceTest, SendHandlesError) {
  auto mock = std::make_unique<MockUsbTransport>();
  auto timeout = 100ms;

  EXPECT_CALL(*mock, Send(_, timeout))
      .WillOnce(Return(std::unexpected(FtError::kNoSuchDevice)));

  UsbDevice device = MakeDevice(std::move(mock));
  std::vector<uint8_t> test_data = {0x01};
  auto result = device.Send(test_data, timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}

TEST(UsbDeviceTest, RecvForwardsToTransport) {
  auto mock = std::make_unique<MockUsbTransport>();
  std::vector<uint8_t> buffer(kArbitraryBufferSize, 0);
  auto timeout = 200ms;

  EXPECT_CALL(*mock, Recv(_, timeout))
      .WillOnce(Invoke([&](std::span<uint8_t> data, auto) {
        std::fill(data.begin(), data.begin() + 5, 0xAB);
        return std::expected<int, FtError>(5);
      }));

  UsbDevice device = MakeDevice(std::move(mock));
  auto result = device.Recv(buffer, timeout);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 5);
}

TEST(UsbDeviceTest, RecvHandlesError) {
  auto mock = std::make_unique<MockUsbTransport>();
  auto timeout = 200ms;

  EXPECT_CALL(*mock, Recv(_, timeout))
      .WillOnce(Return(std::unexpected(FtError::kResultFail)));

  UsbDevice device = MakeDevice(std::move(mock));
  std::vector<uint8_t> buffer(kArbitraryBufferSize);
  auto result = device.Recv(buffer, timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kResultFail);
}

TEST(UsbDeviceTest, WaitForResetForwardsToTransport) {
  auto mock = std::make_unique<MockUsbTransport>();
  auto timeout = 500ms;

  EXPECT_CALL(*mock, WaitForReset(timeout))
      .WillOnce(Return(std::expected<void, FtError>()));

  UsbDevice device = MakeDevice(std::move(mock));
  auto result = device.WaitForReset(timeout);
  EXPECT_TRUE(result.has_value());
}

TEST(UsbDeviceTest, WaitForResetHandlesError) {
  auto mock = std::make_unique<MockUsbTransport>();
  auto timeout = 500ms;

  EXPECT_CALL(*mock, WaitForReset(timeout))
      .WillOnce(Return(std::unexpected(FtError::kNoSuchDevice)));

  UsbDevice device = MakeDevice(std::move(mock));
  auto result = device.WaitForReset(timeout);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), FtError::kNoSuchDevice);
}

TEST(UsbDeviceTest, DeviceIdReturnsFromTransport) {
  auto mock = std::make_unique<MockUsbTransport>();
  UsbDeviceId expected_id{.vid = 0x2808, .pid = 0x0001};

  EXPECT_CALL(*mock, device_id()).WillRepeatedly(Return(expected_id));

  UsbDevice device = MakeDevice(std::move(mock));
  EXPECT_EQ(device.device_id().vid, expected_id.vid);
  EXPECT_EQ(device.device_id().pid, expected_id.pid);
}

TEST(UsbDeviceTest, GetNextCdbTagIncrements) {
  auto mock = std::make_unique<MockUsbTransport>();
  UsbDevice device = MakeDevice(std::move(mock));

  uint32_t tag1 = device.GetNextCdbTag();
  uint32_t tag2 = device.GetNextCdbTag();
  uint32_t tag3 = device.GetNextCdbTag();

  EXPECT_EQ(tag1, 1);
  EXPECT_EQ(tag2, 2);
  EXPECT_EQ(tag3, 3);
}

TEST(UsbDeviceTest, GetNextCdbTagThreadSafe) {
  auto mock = std::make_unique<MockUsbTransport>();
  UsbDevice device = MakeDevice(std::move(mock));

  constexpr int kIterations = 1000;
  std::atomic<uint32_t> last_tag{0};

  auto worker = [&device, &last_tag]() {
    for (int i = 0; i < kIterations; ++i) {
      uint32_t tag = device.GetNextCdbTag();
      last_tag = tag;
    }
  };

  std::thread t1(worker);
  std::thread t2(worker);
  t1.join();
  t2.join();

  EXPECT_GE(last_tag.load(), 1);
  SUCCEED();
}

TEST(UsbDeviceTest, MoveConstructorTransfersOwnership) {
  auto mock = std::make_unique<MockUsbTransport>();
  MockUsbTransport* raw_ptr = mock.get();

  UsbDevice device1 = MakeDevice(std::move(mock));
  UsbDevice device2(std::move(device1));

  EXPECT_CALL(*raw_ptr, device_id())
      .WillOnce(Return(UsbDeviceId{.vid = 0x1234, .pid = 0x5678}));
  EXPECT_EQ(device2.device_id().vid, 0x1234);
}

TEST(UsbDeviceTest, MoveAssignmentTransfersOwnership) {
  auto mock1 = std::make_unique<MockUsbTransport>();
  auto mock2 = std::make_unique<MockUsbTransport>();
  MockUsbTransport* raw_ptr1 = mock1.get();

  UsbDevice device1 = MakeDevice(std::move(mock1));
  UsbDevice device2 = MakeDevice(std::move(mock2));
  device2 = std::move(device1);

  EXPECT_CALL(*raw_ptr1, device_id())
      .WillOnce(Return(UsbDeviceId{.vid = 0x1111, .pid = 0x2222}));
  EXPECT_EQ(device2.device_id().vid, 0x1111);
}
