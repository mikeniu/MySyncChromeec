/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rts5817_mock_usb_backend.h"

namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

constexpr uint16_t kVid = 0x0BDA;
constexpr uint16_t kPid = 0x5817;
constexpr int kTimeoutMs = 1000;
constexpr int kPollIntervalMs = 100;

class RtsUsbDeviceTest : public Test {
    protected:
	void SetUp() override
	{
		auto backend = std::make_unique<StrictMock<MockRtsUsbBackend>>();

		backend_ = backend.get();
		EXPECT_CALL(*backend_, error_name(_))
			.WillRepeatedly(Return("error"));
		/* The destructor releases the backend. */
		EXPECT_CALL(*backend_, close()).Times(AnyNumber());
		device_ = std::make_unique<RtsUsbDevice>(
			std::move(backend),
			[this](int ms) { slept_.push_back(ms); });
	}

	StrictMock<MockRtsUsbBackend> *backend_ = nullptr;
	std::unique_ptr<RtsUsbDevice> device_;
	std::vector<int> slept_;
};

TEST_F(RtsUsbDeviceTest, InitOpensDevice)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(true));

	EXPECT_TRUE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, InitFailsWhenLibusbInitFails)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(-1));
	EXPECT_CALL(*backend_, open(_, _)).Times(0);

	EXPECT_FALSE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, InitReleasesWhenDeviceNotFound)
{
	InSequence seq;

	EXPECT_CALL(*backend_, close());
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(false));
	/* Once on the failure path and once in ~RtsUsbDevice(). */
	EXPECT_CALL(*backend_, close()).Times(2);

	EXPECT_FALSE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, InitReleasesPreviousResourcesFirst)
{
	InSequence seq;

	EXPECT_CALL(*backend_, close());
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(true));
	EXPECT_CALL(*backend_, close());

	EXPECT_TRUE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceOpensImmediately)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(true));

	EXPECT_TRUE(
		device_->wait_for_device(kVid, kPid, kTimeoutMs, kPollIntervalMs));
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceTest, WaitForDevicePollsUntilDeviceAppears)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid))
		.WillOnce(Return(false))
		.WillOnce(Return(false))
		.WillOnce(Return(true));

	EXPECT_TRUE(
		device_->wait_for_device(kVid, kPid, kTimeoutMs, kPollIntervalMs));
	EXPECT_EQ(slept_, (std::vector<int>{ kPollIntervalMs, kPollIntervalMs }));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceTimesOut)
{
	InSequence seq;

	EXPECT_CALL(*backend_, close());
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid))
		.Times(3)
		.WillRepeatedly(Return(false));
	/* Once on the timeout path and once in ~RtsUsbDevice(). */
	EXPECT_CALL(*backend_, close()).Times(2);

	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 3 * kPollIntervalMs,
					      kPollIntervalMs));
	EXPECT_EQ(slept_, (std::vector<int>{ kPollIntervalMs, kPollIntervalMs,
					     kPollIntervalMs }));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceRejectsInvalidIntervals)
{
	EXPECT_CALL(*backend_, init()).Times(0);

	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, kTimeoutMs, 0));
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, kTimeoutMs, -1));
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 0, kPollIntervalMs));
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, -1, kPollIntervalMs));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceFailsWhenLibusbInitFails)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(-1));

	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, kTimeoutMs, kPollIntervalMs));
}

TEST_F(RtsUsbDeviceTest, ReleaseClosesBackend)
{
	EXPECT_CALL(*backend_, close()).Times(2);

	device_->release();
}

} /* namespace */
