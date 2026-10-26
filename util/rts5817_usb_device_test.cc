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

/* A DFU functional descriptor advertising the given wTransferSize. */
std::vector<uint8_t> dfu_func_desc(uint16_t transfer_size)
{
	return { kDfuDescMinLen,
		 kDfuDescriptorType,
		 0x0B,
		 0xFF,
		 0x00,
		 static_cast<uint8_t>(transfer_size & 0xFF),
		 static_cast<uint8_t>(transfer_size >> 8),
		 0x10,
		 0x01 };
}

TEST(ParseDfuTransferSizeTest, ValidExactMinLengthDescriptor)
{
	auto extra = dfu_func_desc(2048);

	ASSERT_EQ(extra.size(), static_cast<size_t>(kDfuDescMinLen));
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()), 2048);
}

TEST(ParseDfuTransferSizeTest, NullOrEmptyUsesDefault)
{
	const uint8_t byte = 0;

	EXPECT_EQ(parse_dfu_transfer_size(nullptr, 0), kDfuDefaultTransferSize);
	EXPECT_EQ(parse_dfu_transfer_size(nullptr, 9), kDfuDefaultTransferSize);
	EXPECT_EQ(parse_dfu_transfer_size(&byte, 0), kDfuDefaultTransferSize);
	EXPECT_EQ(parse_dfu_transfer_size(&byte, -1), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, DescriptorShorterThanMinLengthUsesDefault)
{
	auto extra = dfu_func_desc(2048);

	extra[0] = kDfuDescMinLen - 1;
	extra.pop_back();
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, TruncatedDescriptorUsesDefault)
{
	auto extra = dfu_func_desc(2048);

	/* Declares kDfuDescMinLen bytes but one byte is missing. */
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size() - 1),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, ZeroLengthDescriptorStopsParsing)
{
	std::vector<uint8_t> extra = { 0x00, kDfuDescriptorType, 0x00 };
	auto func = dfu_func_desc(2048);

	extra.insert(extra.end(), func.begin(), func.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, SkipsNonDfuDescriptor)
{
	std::vector<uint8_t> extra = { 0x04, 0x99, 0xAA, 0xBB };
	auto func = dfu_func_desc(512);

	extra.insert(extra.end(), func.begin(), func.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()), 512);
}

TEST(ParseDfuTransferSizeTest, UsesFirstDfuDescriptor)
{
	auto extra = dfu_func_desc(256);
	auto second = dfu_func_desc(4096);

	extra.insert(extra.end(), second.begin(), second.end());
	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()), 256);
}

TEST(ParseDfuTransferSizeTest, ZeroTransferSizeUsesDefault)
{
	auto extra = dfu_func_desc(0);

	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()),
		  kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, DecodesLittleEndianTransferSize)
{
	auto extra = dfu_func_desc(0x1234);

	EXPECT_EQ(parse_dfu_transfer_size(extra.data(), extra.size()), 0x1234);
}

/* Fixture with an opened device, for the DFU interface tests. */
class RtsUsbDeviceDfuTest : public RtsUsbDeviceTest {
    protected:
	RtsUsbInterface dfu_interface(int number, uint16_t transfer_size)
	{
		RtsUsbInterface info;

		info.number = number;
		info.usb_class = kDfuClass;
		info.usb_subclass = kDfuSubclass;
		info.extra = dfu_func_desc(transfer_size);
		return info;
	}

	void expect_interfaces(std::vector<RtsUsbInterface> interfaces)
	{
		EXPECT_CALL(*backend_, is_open())
			.WillRepeatedly(Return(true));
		EXPECT_CALL(*backend_, get_interfaces(_))
			.WillOnce(DoAll(SetArgPointee<0>(std::move(interfaces)),
					Return(0)));
	}
};

TEST_F(RtsUsbDeviceDfuTest, DetachRequiresOpenDevice)
{
	EXPECT_CALL(*backend_, is_open()).WillRepeatedly(Return(false));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _)).Times(0);

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsWhenConfigDescriptorUnavailable)
{
	EXPECT_CALL(*backend_, is_open()).WillRepeatedly(Return(true));
	EXPECT_CALL(*backend_, get_interfaces(_)).WillOnce(Return(-1));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _)).Times(0);

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsWithoutDfuInterface)
{
	RtsUsbInterface hid;

	hid.number = 0;
	hid.usb_class = 0x03;
	hid.usb_subclass = 0x01;
	expect_interfaces({ hid });
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _)).Times(0);

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, DetachRequiresMatchingClassAndSubclass)
{
	RtsUsbInterface wrong_subclass = dfu_interface(1, 1024);

	wrong_subclass.usb_subclass = kDfuSubclass + 1;
	expect_interfaces({ wrong_subclass });

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, DetachSendsRequestToDfuInterface)
{
	RtsUsbInterface other;

	other.number = 0;
	expect_interfaces({ other, dfu_interface(2, 1024) });
	EXPECT_CALL(*backend_, release_interface(2));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuDetach, 1234, 2,
				    nullptr, 0, kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));

	EXPECT_TRUE(device_->dfu_detach(1234));
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsOnTransferError)
{
	expect_interfaces({ dfu_interface(0, 1024) });
	EXPECT_CALL(*backend_, release_interface(0));
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, ReleaseGivesBackFoundInterfaceOnce)
{
	expect_interfaces({ dfu_interface(3, 1024) });
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(0));
	ASSERT_TRUE(device_->dfu_detach(1000));

	EXPECT_CALL(*backend_, release_interface(3)).Times(1);
	device_->release();
	device_->release();
}

} /* namespace */
