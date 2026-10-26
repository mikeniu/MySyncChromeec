/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <algorithm>
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
constexpr uint8_t kHidClass = 0x03;

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

/* Fixture with an opened device, for the DFU interface tests. */
class RtsUsbDeviceDfuTest : public RtsUsbDeviceTest {
    protected:
	RtsUsbInterface dfu_interface(int number, uint16_t transfer_size)
	{
		return {
			.number = number,
			.usb_class = kDfuClass,
			.usb_subclass = kDfuSubclass,
			.extra = dfu_func_desc(transfer_size),
		};
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
	expect_interfaces({
		RtsUsbInterface{
			.number = 0,
			.usb_class = kHidClass,
			.usb_subclass = kDfuSubclass,
		},
	});
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
	expect_interfaces({ RtsUsbInterface{ .number = 0 },
			    dfu_interface(2, 2048) });
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuDetach, 1234, 2,
				    nullptr, 0, kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));

	EXPECT_TRUE(device_->dfu_detach(1234));
	EXPECT_EQ(device_->transfer_size(), 2048);
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsOnTransferError)
{
	expect_interfaces({ dfu_interface(0, 1024) });
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->dfu_detach(1000));
}

TEST_F(RtsUsbDeviceDfuTest, ReleaseForgetsFoundInterface)
{
	expect_interfaces({ dfu_interface(3, 2048) });
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(0));
	ASSERT_TRUE(device_->dfu_detach(1000));
	ASSERT_EQ(device_->transfer_size(), 2048);

	device_->release();
	EXPECT_EQ(device_->transfer_size(), kDfuDefaultTransferSize);
}

/* Exposes the protected DFU helpers and lets tests set the DFU interface. */
class TestableRtsUsbDevice : public RtsUsbDevice {
    public:
	using RtsUsbDevice::dfu_clear_status;
	using RtsUsbDevice::dfu_poll_idle;
	using RtsUsbDevice::RtsUsbDevice;
};

constexpr uint16_t kTransferSize = 4;
constexpr int kDfuIf = 1;

/* Records the DFU_DOWNLOAD blocks the device receives. */
struct DownloadRecorder {
	struct Block {
		uint16_t number;
		std::vector<uint8_t> data;
	};
	std::vector<Block> blocks;
};

class RtsUsbDeviceStatusTest : public Test {
    protected:
	void SetUp() override
	{
		auto backend = std::make_unique<StrictMock<MockRtsUsbBackend>>();

		backend_ = backend.get();
		EXPECT_CALL(*backend_, error_name(_))
			.WillRepeatedly(Return("error"));
		EXPECT_CALL(*backend_, close()).Times(AnyNumber());
		EXPECT_CALL(*backend_, is_open())
			.WillRepeatedly(Return(false));
		device_ = std::make_unique<TestableRtsUsbDevice>(
			std::move(backend),
			[this](int ms) { slept_.push_back(ms); });
	}

	/* Reply to the next DFU_GETSTATUS with the given status fields. */
	void expect_status(uint8_t status, uint32_t poll_timeout_ms,
			   uint8_t state, int ret = kDfuGetStatusResponseLen)
	{
		EXPECT_CALL(*backend_,
			    control_transfer(kBmReqD2hClsIf, kDfuGetStatus, 0,
					    _, _, kDfuGetStatusResponseLen,
					    kUsbDfuTimeoutMs))
			.WillOnce([=](uint8_t, uint8_t, uint16_t, uint16_t,
				      uint8_t *data, uint16_t, unsigned int) {
				data[kDfuStatusOff] = status;
				data[kDfuPollTimeoutOff] = poll_timeout_ms & 0xFF;
				data[kDfuPollTimeoutOff + 1] =
					(poll_timeout_ms >> 8) & 0xFF;
				data[kDfuPollTimeoutOff + 2] =
					(poll_timeout_ms >> 16) & 0xFF;
				data[kDfuStateOff] = state;
				data[kDfuStateOff + 1] = 0;
				return ret;
			});
	}

	StrictMock<MockRtsUsbBackend> *backend_ = nullptr;
	std::unique_ptr<TestableRtsUsbDevice> device_;
	std::vector<int> slept_;
};

TEST_F(RtsUsbDeviceStatusTest, PollIdleSucceedsOnIdleStatesWithoutSleeping)
{
	for (uint8_t state : { kDfuStateDfuIdle, kDfuStateDfuDnloadIdle }) {
		expect_status(kDfuStatusOk, 0, state);
		EXPECT_TRUE(device_->dfu_poll_idle());
	}
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleResetsDeviceOnManifestWaitReset)
{
	expect_status(kDfuStatusOk, 0, kDfuStateDfuManifestWaitReset);
	EXPECT_CALL(*backend_, reset_device()).WillOnce(Return(0));

	EXPECT_TRUE(device_->dfu_poll_idle());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleWaitsThroughBusyStates)
{
	InSequence seq;

	expect_status(kDfuStatusOk, 5, kDfuStateDfuDnloadSync);
	expect_status(kDfuStatusOk, 7, kDfuStateDfuDnloadBusy);
	expect_status(kDfuStatusOk, 9, kDfuStateDfuManifestSync);
	expect_status(kDfuStatusOk, 11, kDfuStateDfuManifest);
	expect_status(kDfuStatusOk, 0, kDfuStateDfuIdle);

	EXPECT_TRUE(device_->dfu_poll_idle());
	EXPECT_EQ(slept_, (std::vector<int>{ 5, 7, 9, 11 }));
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleDecodes24BitLittleEndianPollTimeout)
{
	InSequence seq;

	/* Needs all three bytes: 0x010203 ms = 66051 ms, clamped below. */
	expect_status(kDfuStatusOk, 0x000203, kDfuStateDfuDnloadBusy);
	expect_status(kDfuStatusOk, 0, kDfuStateDfuIdle);

	EXPECT_TRUE(device_->dfu_poll_idle());
	EXPECT_EQ(slept_, (std::vector<int>{ 0x203 }));
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleUsesMinimumDelayForZeroPollTimeout)
{
	InSequence seq;

	expect_status(kDfuStatusOk, 0, kDfuStateDfuDnloadBusy);
	expect_status(kDfuStatusOk, 0, kDfuStateDfuIdle);

	EXPECT_TRUE(device_->dfu_poll_idle());
	EXPECT_EQ(slept_, (std::vector<int>{ kDfuMinPollDelayMs }));
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleClampsDelayToRemainingTimeout)
{
	/*
	 * A huge 24-bit poll timeout must neither overflow nor sleep past the
	 * overall timeout; the single sleep uses up the remaining budget.
	 */
	expect_status(kDfuStatusOk, 0xFFFFFF, kDfuStateDfuManifest);

	EXPECT_FALSE(device_->dfu_poll_idle());
	EXPECT_EQ(slept_, (std::vector<int>{ kUsbDfuTimeoutMs }));
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleTimesOutAtExactBoundary)
{
	InSequence seq;

	expect_status(kDfuStatusOk, kUsbDfuTimeoutMs - 1, kDfuStateDfuManifest);
	expect_status(kDfuStatusOk, 1, kDfuStateDfuManifest);

	EXPECT_FALSE(device_->dfu_poll_idle());
	EXPECT_EQ(slept_, (std::vector<int>{ kUsbDfuTimeoutMs - 1, 1 }));
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleSucceedsOnLastPollBeforeTimeout)
{
	InSequence seq;

	expect_status(kDfuStatusOk, kUsbDfuTimeoutMs - 1, kDfuStateDfuManifest);
	expect_status(kDfuStatusOk, 0, kDfuStateDfuIdle);

	EXPECT_TRUE(device_->dfu_poll_idle());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleFailsOnTransferError)
{
	expect_status(0, 0, 0, -1);

	EXPECT_FALSE(device_->dfu_poll_idle());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleFailsOnShortRead)
{
	expect_status(kDfuStatusOk, 0, kDfuStateDfuIdle,
		      kDfuGetStatusResponseLen - 1);

	EXPECT_FALSE(device_->dfu_poll_idle());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleFailsOnDfuErrorState)
{
	expect_status(kDfuStatusOk, 0, kDfuStateDfuError);

	EXPECT_FALSE(device_->dfu_poll_idle());
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleFailsOnNonZeroStatus)
{
	expect_status(0x0E, 0, kDfuStateDfuDnloadBusy);

	EXPECT_FALSE(device_->dfu_poll_idle());
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceStatusTest, PollIdleFailsImmediatelyOnUnexpectedState)
{
	/* appIDLE (0), appDETACH (1), dfuUPLOAD-IDLE (9) and invalid (11). */
	for (uint8_t state : { 0, 1, 9, 11 }) {
		expect_status(kDfuStatusOk, 100, state);
		EXPECT_FALSE(device_->dfu_poll_idle());
	}
	EXPECT_TRUE(slept_.empty());
}

TEST_F(RtsUsbDeviceStatusTest, ClearStatusSendsRequest)
{
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuClrStatus, 0, _,
				    nullptr, 0, kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));

	EXPECT_TRUE(device_->dfu_clear_status());
}

TEST_F(RtsUsbDeviceStatusTest, ClearStatusFailsOnTransferError)
{
	EXPECT_CALL(*backend_, control_transfer(_, _, _, _, _, _, _))
		.WillOnce(Return(-1));

	EXPECT_FALSE(device_->dfu_clear_status());
}

/* Opened device with a DFU interface advertising kTransferSize. */
class RtsUsbDeviceDownloadTest : public RtsUsbDeviceDfuTest {
    protected:
	void SetUp() override
	{
		RtsUsbDeviceDfuTest::SetUp();
		EXPECT_CALL(*backend_, is_open()).WillRepeatedly(Return(true));
		EXPECT_CALL(*backend_, get_interfaces(_))
			.WillRepeatedly(DoAll(
				SetArgPointee<0>(std::vector<RtsUsbInterface>{
					dfu_interface(kDfuIf, kTransferSize) }),
				Return(0)));
		EXPECT_CALL(*backend_, detach_kernel_driver(kDfuIf))
			.WillRepeatedly(Return(0));
		EXPECT_CALL(*backend_, claim_interface(kDfuIf))
			.WillRepeatedly(Return(0));
	}

	/* Accept every DFU_DOWNLOAD and record it. */
	void record_downloads(int fail_at_block = -1)
	{
		EXPECT_CALL(*backend_,
			    control_transfer(kBmReqH2dClsIf, kDfuDownload, _,
					    kDfuIf, _, _, kUsbDfuTimeoutMs))
			.WillRepeatedly([this, fail_at_block](
						uint8_t, uint8_t, uint16_t value,
						uint16_t, uint8_t *data,
						uint16_t length, unsigned int) {
				if (static_cast<int>(value) == fail_at_block)
					return -1;
				rec_.blocks.push_back(
					{ value,
					  std::vector<uint8_t>(data,
							       data + length) });
				return static_cast<int>(length);
			});
	}

	static int fill_idle_status(uint8_t, uint8_t, uint16_t, uint16_t,
				    uint8_t *data, uint16_t, unsigned int)
	{
		std::fill_n(data, kDfuGetStatusResponseLen, 0);
		data[kDfuStateOff] = kDfuStateDfuDnloadIdle;
		return static_cast<int>(kDfuGetStatusResponseLen);
	}

	/* Every DFU_GETSTATUS reports dfuDNLOAD-IDLE. */
	void status_always_idle()
	{
		EXPECT_CALL(*backend_,
			    control_transfer(kBmReqD2hClsIf, kDfuGetStatus, 0,
					    kDfuIf, _, kDfuGetStatusResponseLen,
					    kUsbDfuTimeoutMs))
			.WillRepeatedly(fill_idle_status);
	}

	DownloadRecorder rec_;
};

TEST_F(RtsUsbDeviceDownloadTest, ChunksAcrossTransferSizeBoundaries)
{
	record_downloads();
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	/* kTransferSize + 1 bytes: one full block, a 1-byte tail, and a ZLP. */
	const std::vector<uint8_t> fw = { 0x10, 0x20, 0x30, 0x40, 0x50 };

	ASSERT_TRUE(device_->dfu_download(fw));
	ASSERT_EQ(rec_.blocks.size(), 3u);
	EXPECT_EQ(rec_.blocks[0].number, 0);
	EXPECT_EQ(rec_.blocks[0].data,
		  (std::vector<uint8_t>{ 0x10, 0x20, 0x30, 0x40 }));
	EXPECT_EQ(rec_.blocks[1].number, 1);
	EXPECT_EQ(rec_.blocks[1].data, (std::vector<uint8_t>{ 0x50 }));
	EXPECT_EQ(rec_.blocks[2].number, 2);
	EXPECT_TRUE(rec_.blocks[2].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageSmallerThanOneBlock)
{
	record_downloads();
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	ASSERT_TRUE(device_->dfu_download({ 0x01, 0x02 }));
	ASSERT_EQ(rec_.blocks.size(), 2u);
	EXPECT_EQ(rec_.blocks[0].data, (std::vector<uint8_t>{ 0x01, 0x02 }));
	EXPECT_TRUE(rec_.blocks[1].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageExactlyOneBlock)
{
	record_downloads();
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	ASSERT_TRUE(device_->dfu_download({ 1, 2, 3, 4 }));
	ASSERT_EQ(rec_.blocks.size(), 2u);
	EXPECT_EQ(rec_.blocks[0].data, (std::vector<uint8_t>{ 1, 2, 3, 4 }));
	EXPECT_TRUE(rec_.blocks[1].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageOfExactMultipleOfBlockSize)
{
	record_downloads();
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	ASSERT_TRUE(device_->dfu_download({ 1, 2, 3, 4, 5, 6, 7, 8 }));
	ASSERT_EQ(rec_.blocks.size(), 3u);
	EXPECT_EQ(rec_.blocks[0].data, (std::vector<uint8_t>{ 1, 2, 3, 4 }));
	EXPECT_EQ(rec_.blocks[1].data, (std::vector<uint8_t>{ 5, 6, 7, 8 }));
	EXPECT_EQ(rec_.blocks[2].number, 2);
	EXPECT_TRUE(rec_.blocks[2].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, RejectsEmptyFirmware)
{
	EXPECT_CALL(*backend_, claim_interface(_)).Times(0);

	EXPECT_FALSE(device_->dfu_download({}));
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWithoutOpenDevice)
{
	EXPECT_CALL(*backend_, is_open()).WillOnce(Return(false));

	EXPECT_FALSE(device_->dfu_download({ 1 }));
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenNoDfuInterface)
{
	EXPECT_CALL(*backend_, get_interfaces(_)).WillOnce(Return(0));
	EXPECT_CALL(*backend_, claim_interface(_)).Times(0);

	EXPECT_FALSE(device_->dfu_download({ 1 }));
}

TEST_F(RtsUsbDeviceDownloadTest, ContinuesWhenKernelDriverDetachFails)
{
	EXPECT_CALL(*backend_, detach_kernel_driver(kDfuIf))
		.WillOnce(Return(-1));
	record_downloads();
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf));

	EXPECT_TRUE(device_->dfu_download({ 1, 2 }));
}

TEST_F(RtsUsbDeviceDownloadTest, ClaimFailureLeavesNothingToRelease)
{
	EXPECT_CALL(*backend_, claim_interface(kDfuIf)).WillOnce(Return(-1));
	EXPECT_CALL(*backend_, release_interface(_)).Times(0);

	/* The destructor must not release an interface that was never claimed. */
	EXPECT_FALSE(device_->dfu_download({ 1, 2 }));
}

TEST_F(RtsUsbDeviceDownloadTest, TransferFailureClearsStatusAndReleases)
{
	record_downloads(/*fail_at_block=*/1);
	status_always_idle();
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuClrStatus, 0, kDfuIf,
				    nullptr, 0, kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	EXPECT_FALSE(device_->dfu_download({ 1, 2, 3, 4, 5, 6 }));
	ASSERT_EQ(rec_.blocks.size(), 1u);
}

TEST_F(RtsUsbDeviceDownloadTest, ShortWriteIsAnError)
{
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuDownload, 0, kDfuIf, _,
				    kTransferSize, kUsbDfuTimeoutMs))
		.WillOnce(Return(kTransferSize - 1));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuClrStatus, 0, kDfuIf,
				    nullptr, 0, kUsbCtrlTimeoutMs))
		.WillOnce(Return(0));
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	EXPECT_FALSE(device_->dfu_download({ 1, 2, 3, 4, 5, 6 }));
}

TEST_F(RtsUsbDeviceDownloadTest, PollFailureReleasesInterface)
{
	record_downloads();
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hClsIf, kDfuGetStatus, 0, kDfuIf,
				    _, kDfuGetStatusResponseLen,
				    kUsbDfuTimeoutMs))
		.WillOnce(Return(-1));
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	EXPECT_FALSE(device_->dfu_download({ 1, 2, 3, 4, 5, 6 }));
	ASSERT_EQ(rec_.blocks.size(), 1u);
}

TEST_F(RtsUsbDeviceDownloadTest, FinalZlpFailureIsOnlyAWarning)
{
	/* Blocks are accepted, the trailing zero-length packet is rejected. */
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuDownload, 0, kDfuIf, _,
				    2, kUsbDfuTimeoutMs))
		.WillOnce(Return(2));
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqH2dClsIf, kDfuDownload, 1, kDfuIf,
				    nullptr, 0, kUsbDfuTimeoutMs))
		.WillOnce(Return(-1));
	status_always_idle();
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	EXPECT_TRUE(device_->dfu_download({ 1, 2 }));
}

TEST_F(RtsUsbDeviceDownloadTest, ManifestNotIdleStillSucceeds)
{
	record_downloads();
	EXPECT_CALL(*backend_,
		    control_transfer(kBmReqD2hClsIf, kDfuGetStatus, 0, kDfuIf,
				    _, kDfuGetStatusResponseLen,
				    kUsbDfuTimeoutMs))
		.WillOnce(fill_idle_status)
		.WillOnce(Return(-1));
	EXPECT_CALL(*backend_, release_interface(kDfuIf)).Times(1);

	EXPECT_TRUE(device_->dfu_download({ 1, 2 }));
}

} /* namespace */
