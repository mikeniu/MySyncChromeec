/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "rts5817_mock_usb_backend.h"

namespace realtekfp {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

using std::chrono::milliseconds;

constexpr UsbDeviceId kDevId = {.vid = 0x0BDA, .pid = 0x5817};
constexpr milliseconds kTimeout{1000};
constexpr milliseconds kPollInterval{100};

const std::unexpected<int> kNotFound{kUsbErrorNotFound};
const std::unexpected<int> kNoDevice{kUsbErrorNoDevice};
const std::unexpected<int> kAccess{kUsbErrorAccess};
const std::unexpected<int> kOtherError{-1};

class RtsUsbDeviceTest : public Test {
 protected:
  void SetUp() override {
    auto backend = std::make_unique<StrictMock<MockRtsUsbBackend>>();

    backend_ = backend.get();
    EXPECT_CALL(*backend_, ErrorName(_)).WillRepeatedly(Return("error"));
    // The destructor releases the backend.
    EXPECT_CALL(*backend_, Close()).Times(AnyNumber());
    device_ = std::make_unique<RtsUsbDevice>(
        std::move(backend),
        [this](milliseconds delay) { slept_.push_back(delay); });
  }

  StrictMock<MockRtsUsbBackend>* backend_ = nullptr;
  std::unique_ptr<RtsUsbDevice> device_;
  std::vector<milliseconds> slept_;
};

TEST_F(RtsUsbDeviceTest, InitOpensDevice) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .WillOnce(Return(std::expected<void, int>{}));

  EXPECT_TRUE(device_->Init(kDevId));
}

TEST_F(RtsUsbDeviceTest, InitFailsWhenLibusbInitFails) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(kOtherError));
  EXPECT_CALL(*backend_, Open(_)).Times(0);

  auto res = device_->Init(kDevId);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kUsbError);
}

TEST_F(RtsUsbDeviceTest, InitReleasesWhenDeviceNotFound) {
  InSequence seq;

  EXPECT_CALL(*backend_, Close());
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId)).WillOnce(Return(kNotFound));
  // Once on the failure path and once in ~RtsUsbDevice().
  EXPECT_CALL(*backend_, Close()).Times(2);

  auto res = device_->Init(kDevId);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

TEST_F(RtsUsbDeviceTest, InitReportsAccessDenied) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId)).WillOnce(Return(kAccess));

  auto res = device_->Init(kDevId);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
}

TEST_F(RtsUsbDeviceTest, InitReleasesPreviousResourcesFirst) {
  InSequence seq;

  EXPECT_CALL(*backend_, Close());
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Close());

  EXPECT_TRUE(device_->Init(kDevId));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceOpensImmediately) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .WillOnce(Return(std::expected<void, int>{}));

  EXPECT_TRUE(device_->WaitForDevice(kDevId, kTimeout, kPollInterval));
  EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceTest, WaitForDevicePollsUntilDeviceAppears) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .WillOnce(Return(kNotFound))
      .WillOnce(Return(kNoDevice))
      .WillOnce(Return(std::expected<void, int>{}));

  EXPECT_TRUE(device_->WaitForDevice(kDevId, kTimeout, kPollInterval));
  EXPECT_EQ(slept_, (std::vector<milliseconds>{kPollInterval, kPollInterval}));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceTimesOut) {
  InSequence seq;

  EXPECT_CALL(*backend_, Close());
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .Times(3)
      .WillRepeatedly(Return(kNotFound));
  // Once on the timeout path and once in ~RtsUsbDevice().
  EXPECT_CALL(*backend_, Close()).Times(2);

  auto res = device_->WaitForDevice(kDevId, 3 * kPollInterval, kPollInterval);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kTimeout);
  EXPECT_EQ(slept_, (std::vector<milliseconds>{kPollInterval, kPollInterval,
                                               kPollInterval}));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceAbortsOnAccessDenied) {
  InSequence seq;

  EXPECT_CALL(*backend_, Close());
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId))
      .WillOnce(Return(kNotFound))
      .WillOnce(Return(kAccess));
  // Once on the failure path and once in ~RtsUsbDevice().
  EXPECT_CALL(*backend_, Close()).Times(2);

  auto res = device_->WaitForDevice(kDevId, kTimeout, kPollInterval);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
  EXPECT_EQ(slept_.size(), 1u);
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceAbortsOnOtherUsbError) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, Open(kDevId)).WillOnce(Return(kOtherError));

  auto res = device_->WaitForDevice(kDevId, kTimeout, kPollInterval);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kUsbError);
  EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceRejectsInvalidIntervals) {
  EXPECT_CALL(*backend_, Init()).Times(0);

  for (auto res :
       {device_->WaitForDevice(kDevId, kTimeout, milliseconds{0}),
        device_->WaitForDevice(kDevId, kTimeout, milliseconds{-1}),
        device_->WaitForDevice(kDevId, milliseconds{0}, kPollInterval),
        device_->WaitForDevice(kDevId, milliseconds{-1}, kPollInterval)}) {
    ASSERT_FALSE(res);
    EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
  }
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceFailsWhenLibusbInitFails) {
  EXPECT_CALL(*backend_, Init()).WillOnce(Return(kOtherError));

  auto res = device_->WaitForDevice(kDevId, kTimeout, kPollInterval);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kUsbError);
}

TEST_F(RtsUsbDeviceTest, ReleaseClosesBackend) {
  EXPECT_CALL(*backend_, Close()).Times(2);

  device_->Release();
}

TEST(RtsErrorTest, ToStringCoversAllValues) {
  for (RtsError error :
       {RtsError::kDeviceNotFound, RtsError::kAccessDenied, RtsError::kUsbError,
        RtsError::kInvalidParameter, RtsError::kTimeout}) {
    EXPECT_NE(ToString(error), "Unknown error");
  }
}

}  // namespace
}  // namespace realtekfp
