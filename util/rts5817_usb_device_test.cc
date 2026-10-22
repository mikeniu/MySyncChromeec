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
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr uint16_t kVid = 0x0BDA;
constexpr uint16_t kPid = 0x5817;

class RtsUsbDeviceTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		auto backend = std::make_unique<StrictMock<MockRtsUsbBackend>>();

		backend_ = backend.get();
		EXPECT_CALL(*backend_, error_name(_))
			.WillRepeatedly(Return("error"));
		/* The destructor releases the backend. */
		EXPECT_CALL(*backend_, close()).Times(::testing::AnyNumber());
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
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(false));
	/* Once up front and once on the failure path. */
	EXPECT_CALL(*backend_, close()).Times(::testing::AtLeast(2));

	EXPECT_FALSE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, InitReleasesPreviousResourcesFirst)
{
	::testing::InSequence seq;

	EXPECT_CALL(*backend_, close());
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(true));

	EXPECT_TRUE(device_->init(kVid, kPid));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceOpensImmediately)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid)).WillOnce(Return(true));

	EXPECT_TRUE(device_->wait_for_device(kVid, kPid, 1000, 100));
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceTest, WaitForDevicePollsUntilDeviceAppears)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid))
		.WillOnce(Return(false))
		.WillOnce(Return(false))
		.WillOnce(Return(true));

	EXPECT_TRUE(device_->wait_for_device(kVid, kPid, 1000, 100));
	EXPECT_EQ(slept_, (std::vector<int>{ 100, 100 }));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceTimesOut)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(0));
	EXPECT_CALL(*backend_, open(kVid, kPid))
		.Times(3)
		.WillRepeatedly(Return(false));

	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 300, 100));
	EXPECT_EQ(slept_, (std::vector<int>{ 100, 100, 100 }));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceRejectsInvalidIntervals)
{
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 1000, 0));
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 1000, -1));
	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, -1, 100));
}

TEST_F(RtsUsbDeviceTest, WaitForDeviceFailsWhenLibusbInitFails)
{
	EXPECT_CALL(*backend_, init()).WillOnce(Return(-1));

	EXPECT_FALSE(device_->wait_for_device(kVid, kPid, 1000, 100));
}

TEST_F(RtsUsbDeviceTest, EnableDownloadRequiresOpenDevice)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(false));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _)).Times(0);

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadSendsSetStatusWhenInactive)
{
	::testing::InSequence seq;

	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    1, kUsbCtrlTimeoutMs))
		.WillOnce(DoAll(::testing::SetArgPointee<4>(0), Return(1)));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dStdDev, kRtsStdSetStatus,
				    kRtsEnableDownloadMode, 0, nullptr, 0,
				    kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));

	EXPECT_TRUE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadSkipsSetStatusWhenAlreadyActive)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, _, _, _,
				    _, _))
		.WillOnce(DoAll(::testing::SetArgPointee<4>(1), Return(1)));

	EXPECT_TRUE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnGetStatusError)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnGetStatusShortRead)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(0));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnSetStatusError)
{
	::testing::InSequence seq;

	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_, control_transfer(kBmReqD2hStdDev, _, _, _, _, _, _))
		.WillOnce(DoAll(::testing::SetArgPointee<4>(0), Return(1)));
	EXPECT_CALL(*backend_, control_transfer(kBmReqH2dStdDev, _, _, _, _, _, _))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, ReleaseClosesBackend)
{
	EXPECT_CALL(*backend_, close()).Times(::testing::AtLeast(1));

	device_->release();
}

} /* namespace */
