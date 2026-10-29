/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <memory>
#include <span>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rts5817_mock_usb_backend.h"

namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;
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

TEST_F(RtsUsbDeviceTest, EnableDownloadRequiresOpenDevice)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(false));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _)).Times(0);

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadSendsSetStatusWhenInactive)
{
	InSequence seq;

	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    sizeof(uint8_t), kUsbCtrlTimeoutMs))
		.WillOnce(DoAll(SetArgPointee<4>(0), Return(1)));
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
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    sizeof(uint8_t), kUsbCtrlTimeoutMs))
		.WillOnce(DoAll(SetArgPointee<4>(1), Return(1)));

	EXPECT_TRUE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnGetStatusError)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    sizeof(uint8_t), kUsbCtrlTimeoutMs))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnGetStatusShortRead)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    sizeof(uint8_t), kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, EnableDownloadFailsOnSetStatusError)
{
	InSequence seq;

	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(true));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, _,
				    sizeof(uint8_t), kUsbCtrlTimeoutMs))
		.WillOnce(DoAll(SetArgPointee<4>(0), Return(1)));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dStdDev, kRtsStdSetStatus,
				    kRtsEnableDownloadMode, 0, nullptr, 0,
				    kUsbCtrlTimeoutMs))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->enable_download());
}

TEST_F(RtsUsbDeviceTest, ReleaseClosesBackend)
{
	EXPECT_CALL(*backend_, close()).Times(2);

	device_->release();
}

/* bitCanDnload | bitManifestationTolerant | bitWillDetach */
constexpr uint8_t kDfuAttributes = 0x0B;
constexpr uint8_t kDfuDetachTimeoutLo = 0xFF;
constexpr uint8_t kDfuDetachTimeoutHi = 0x00;
constexpr uint8_t kDfuVersionLo = 0x10; /* bcdDFUVersion 1.10 */
constexpr uint8_t kDfuVersionHi = 0x01;

/* A DFU functional descriptor advertising the given wTransferSize. */
std::vector<uint8_t> dfu_func_desc(uint16_t transfer_size)
{
	return { kDfuDescMinLen,
		 kDfuDescriptorType,
		 kDfuAttributes,
		 kDfuDetachTimeoutLo,
		 kDfuDetachTimeoutHi,
		 static_cast<uint8_t>(transfer_size & 0xFF),
		 static_cast<uint8_t>(transfer_size >> 8),
		 kDfuVersionLo,
		 kDfuVersionHi };
}

TEST(ParseDfuTransferSizeTest, ValidExactMinLengthDescriptor)
{
	auto extra = dfu_func_desc(2048);

	ASSERT_EQ(extra.size(), static_cast<size_t>(kDfuDescMinLen));
	EXPECT_EQ(parse_dfu_transfer_size(extra), 2048);
}

TEST(ParseDfuTransferSizeTest, EmptyUsesDefault)
{
	EXPECT_EQ(parse_dfu_transfer_size({}), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, SingleByteBufferUsesDefault)
{
	const std::vector<uint8_t> extra = { 0x01 };

	EXPECT_EQ(parse_dfu_transfer_size(extra), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, ValidDescriptorLongerThanMinLength)
{
	auto extra = dfu_func_desc(2048);

	extra[0] = kDfuDescMinLen + 1;
	extra.push_back(0x00);
	EXPECT_EQ(parse_dfu_transfer_size(extra), 2048);
}

TEST(ParseDfuTransferSizeTest, DescriptorShorterThanMinLengthUsesDefault)
{
	auto extra = dfu_func_desc(2048);

	extra[0] = kDfuDescMinLen - 1;
	extra.pop_back();
	EXPECT_EQ(parse_dfu_transfer_size(extra),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, TruncatedDescriptorUsesDefault)
{
	auto extra = dfu_func_desc(2048);

	/* Declares kDfuDescMinLen bytes but one byte is missing. */
	EXPECT_EQ(parse_dfu_transfer_size(
			  std::span<const uint8_t>(extra).first(extra.size() - 1)),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, ZeroLengthDescriptorStopsParsing)
{
	std::vector<uint8_t> extra = { 0x00, kDfuDescriptorType, 0x00 };
	auto func = dfu_func_desc(2048);

	extra.insert(extra.end(), func.begin(), func.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, SkipsNonDfuDescriptor)
{
	std::vector<uint8_t> extra = { 0x04, 0x99, 0xAA, 0xBB };
	auto func = dfu_func_desc(512);

	extra.insert(extra.end(), func.begin(), func.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra), 512);
}

TEST(ParseDfuTransferSizeTest, UsesFirstDfuDescriptor)
{
	auto extra = dfu_func_desc(256);
	auto second = dfu_func_desc(4096);

	extra.insert(extra.end(), second.begin(), second.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra), 256);
}

TEST(ParseDfuTransferSizeTest, ZeroTransferSizeUsesDefault)
{
	auto extra = dfu_func_desc(0);

	EXPECT_EQ(parse_dfu_transfer_size(extra),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, DecodesLittleEndianTransferSize)
{
	auto extra = dfu_func_desc(0x1234);

	EXPECT_EQ(parse_dfu_transfer_size(extra), 0x1234);
}

} /* namespace */
