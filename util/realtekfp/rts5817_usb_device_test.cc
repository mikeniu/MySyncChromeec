/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "rts5817_mock_usb_backend.h"

namespace realtekfp {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SizeIs;
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

// Replies to the R_INITVTOR query with `value` (little endian).
auto InitVtorIs(uint32_t value) {
  return [value](uint8_t, uint8_t, uint16_t, uint16_t, std::span<uint8_t> data,
                 milliseconds) {
    std::ranges::copy(std::bit_cast<std::array<uint8_t, sizeof(value)>>(value),
                      data.begin());
    return std::expected<size_t, int>(data.size());
  };
}

// Replies to GET_STATUS with the given one-byte status.
auto StatusIs(uint8_t status) {
  return [status](uint8_t, uint8_t, uint16_t, uint16_t, std::span<uint8_t> data,
                  milliseconds) {
    data[0] = status;
    return std::expected<size_t, int>(1);
  };
}

class RtsUsbDeviceEnableDownloadTest : public RtsUsbDeviceTest {
 protected:
  void SetUp() override {
    RtsUsbDeviceTest::SetUp();
    EXPECT_CALL(*backend_, IsOpen()).WillRepeatedly(Return(true));
  }

  void ExpectInitVtorQuery(testing::Action<std::expected<size_t, int>(
                               uint8_t, uint8_t, uint16_t, uint16_t,
                               std::span<uint8_t>, milliseconds)>
                               reply) {
    EXPECT_CALL(
        *backend_,
        ControlTransferIn(kBmReqD2hVendorDev, kRtsVreqXmemRead,
                          kRtsInitVtorAddr & 0xFFFF, kRtsInitVtorAddr >> 16,
                          SizeIs(sizeof(uint32_t)), kUsbCtrlTimeout))
        .WillOnce(reply);
  }

  void ExpectGetStatus(testing::Action<std::expected<size_t, int>(
                           uint8_t, uint8_t, uint16_t, uint16_t,
                           std::span<uint8_t>, milliseconds)>
                           reply) {
    EXPECT_CALL(*backend_, ControlTransferIn(kBmReqD2hStdDev, kRtsStdGetStatus,
                                             0, 0, SizeIs(1), kUsbCtrlTimeout))
        .WillOnce(reply);
  }
};

TEST_F(RtsUsbDeviceTest, EnableDownloadRequiresOpenDevice) {
  EXPECT_CALL(*backend_, IsOpen()).WillOnce(Return(false));
  EXPECT_CALL(*backend_, ControlTransferIn(_, _, _, _, _, _)).Times(0);

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SendsSetStatusWhenInactive) {
  InSequence seq;

  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorBootRom));
  ExpectGetStatus(StatusIs(0));
  EXPECT_CALL(*backend_, ControlTransferOut(kBmReqH2dStdDev, kRtsStdSetStatus,
                                            kRtsEnableDownloadMode, 0,
                                            IsEmpty(), kUsbCtrlTimeout))
      .WillOnce(Return(std::expected<size_t, int>(0)));

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsSetStatusWhenAlreadyActive) {
  InSequence seq;

  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorBootRom));
  ExpectGetStatus(StatusIs(1));
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsGetStatusInFlashImage) {
  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorFlashImage));
  EXPECT_CALL(*backend_, ControlTransferIn(kBmReqD2hStdDev, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsGetStatusForUnknownImage) {
  ExpectInitVtorQuery(InitVtorIs(0x12345678));
  EXPECT_CALL(*backend_, ControlTransferIn(kBmReqD2hStdDev, _, _, _, _, _))
      .Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, ProceedsWhenModeQueryStalls) {
  ExpectInitVtorQuery(Return(std::unexpected(kUsbErrorPipe)));
  EXPECT_CALL(*backend_, ControlTransferIn(kBmReqD2hStdDev, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnModeQueryTransportError) {
  ExpectInitVtorQuery(Return(std::unexpected(kUsbErrorNoDevice)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnModeQueryShortRead) {
  ExpectInitVtorQuery(Return(std::expected<size_t, int>(sizeof(uint32_t) - 1)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnGetStatusError) {
  InSequence seq;

  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorBootRom));
  ExpectGetStatus(Return(std::unexpected(kUsbErrorPipe)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnGetStatusShortRead) {
  InSequence seq;

  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorBootRom));
  ExpectGetStatus(Return(std::expected<size_t, int>(0)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnSetStatusError) {
  InSequence seq;

  ExpectInitVtorQuery(InitVtorIs(kRtsInitVtorBootRom));
  ExpectGetStatus(StatusIs(0));
  EXPECT_CALL(*backend_, ControlTransferOut(kBmReqH2dStdDev, kRtsStdSetStatus,
                                            kRtsEnableDownloadMode, 0,
                                            IsEmpty(), kUsbCtrlTimeout))
      .WillOnce(Return(std::unexpected(kUsbErrorAccess)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
}

TEST_F(RtsUsbDeviceTest, ReleaseClosesBackend) {
  EXPECT_CALL(*backend_, Close()).Times(2);

  device_->Release();
}

TEST(RtsErrorTest, ToStringCoversAllValues) {
  for (RtsError error :
       {RtsError::kDeviceNotFound, RtsError::kAccessDenied, RtsError::kUsbError,
        RtsError::kInvalidParameter, RtsError::kTimeout, RtsError::kStall,
        RtsError::kProtocolError}) {
    EXPECT_NE(ToString(error), "Unknown error");
  }
}

// bitCanDnload | bitManifestationTolerant | bitWillDetach
constexpr uint8_t kDfuAttributes = 0x0B;
constexpr uint8_t kDfuDetachTimeoutLo = 0xFF;
constexpr uint8_t kDfuDetachTimeoutHi = 0x00;
constexpr uint8_t kDfuVersionLo = 0x10;  // bcdDFUVersion 1.10
constexpr uint8_t kDfuVersionHi = 0x01;
// Length of the DFU 1.1 functional descriptor, which adds bcdDFUVersion.
constexpr uint8_t kDfu11DescLen = 9;

// A DFU 1.1 functional descriptor advertising the given wTransferSize.
std::vector<uint8_t> DfuFuncDesc(uint16_t transfer_size) {
  return {kDfu11DescLen,
          kDfuDescriptorType,
          kDfuAttributes,
          kDfuDetachTimeoutLo,
          kDfuDetachTimeoutHi,
          static_cast<uint8_t>(transfer_size & 0xFF),
          static_cast<uint8_t>(transfer_size >> 8),
          kDfuVersionLo,
          kDfuVersionHi};
}

TEST(ParseDfuTransferSizeTest, ValidDfu11Descriptor) {
  std::vector<uint8_t> extra = DfuFuncDesc(2048);

  ASSERT_EQ(extra.size(), kDfu11DescLen);
  EXPECT_EQ(ParseDfuTransferSize(extra), 2048);
}

TEST(ParseDfuTransferSizeTest, ParsesRts5817SevenByteDescriptor) {
  // What the RTS5817 reports in both Runtime and DFU mode: bLength 7 (no
  // bcdDFUVersion), bmAttributes 0x0F, wDetachTimeOut 2000, wTransferSize 4096.
  // A second descriptor advertising a different size follows, so skipping or
  // rejecting the 7-byte one cannot pass via the 4096-byte default.
  std::vector<uint8_t> extra = {0x07, 0x21, 0x0F, 0xD0, 0x07, 0x00, 0x10};
  const std::vector<uint8_t> second = DfuFuncDesc(2048);

  ASSERT_EQ(extra.size(), kDfuDescMinLen);
  extra.insert(extra.end(), second.begin(), second.end());
  EXPECT_EQ(ParseDfuTransferSize(extra), 4096);
}

TEST(ParseDfuTransferSizeTest, ValidExactMinLengthDescriptor) {
  std::vector<uint8_t> extra = DfuFuncDesc(2048);

  extra[0] = kDfuDescMinLen;
  extra.resize(kDfuDescMinLen);
  EXPECT_EQ(ParseDfuTransferSize(extra), 2048);
}

TEST(ParseDfuTransferSizeTest, EmptyUsesDefault) {
  EXPECT_EQ(ParseDfuTransferSize({}), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, SingleByteBufferUsesDefault) {
  const std::vector<uint8_t> extra = {0x01};

  EXPECT_EQ(ParseDfuTransferSize(extra), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, ValidDescriptorLongerThanMinLength) {
  std::vector<uint8_t> extra = DfuFuncDesc(2048);

  extra[0] = kDfu11DescLen + 1;
  extra.push_back(0x00);
  EXPECT_EQ(ParseDfuTransferSize(extra), 2048);
}

TEST(ParseDfuTransferSizeTest, DescriptorShorterThanMinLengthUsesDefault) {
  std::vector<uint8_t> extra = DfuFuncDesc(2048);

  // Declares one byte less than needed to hold wTransferSize.
  extra[0] = kDfuDescMinLen - 1;
  extra.resize(kDfuDescMinLen - 1);
  EXPECT_EQ(ParseDfuTransferSize(extra), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, TruncatedDescriptorUsesDefault) {
  std::vector<uint8_t> extra = DfuFuncDesc(2048);

  // Declares kDfu11DescLen bytes but one byte is missing.
  extra.pop_back();
  EXPECT_EQ(ParseDfuTransferSize(extra), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, ZeroLengthDescriptorStopsParsing) {
  std::vector<uint8_t> extra = {0x00, kDfuDescriptorType, 0x00};
  std::vector<uint8_t> func = DfuFuncDesc(2048);

  extra.insert(extra.end(), func.begin(), func.end());
  EXPECT_EQ(ParseDfuTransferSize(extra), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, SkipsNonDfuDescriptor) {
  // Same length as a DFU descriptor, so only the type check rejects it.
  std::vector<uint8_t> extra = DfuFuncDesc(2048);
  std::vector<uint8_t> func = DfuFuncDesc(512);

  extra[kDfuDescTypeOff] = 0x99;
  extra.insert(extra.end(), func.begin(), func.end());
  EXPECT_EQ(ParseDfuTransferSize(extra), 512);
}

TEST(ParseDfuTransferSizeTest, SkipsShortNonDfuDescriptor) {
  std::vector<uint8_t> extra = {0x04, 0x99, 0xAA, 0xBB};
  std::vector<uint8_t> func = DfuFuncDesc(512);

  extra.insert(extra.end(), func.begin(), func.end());
  EXPECT_EQ(ParseDfuTransferSize(extra), 512);
}

TEST(ParseDfuTransferSizeTest, UsesFirstDfuDescriptor) {
  std::vector<uint8_t> extra = DfuFuncDesc(256);
  std::vector<uint8_t> second = DfuFuncDesc(4096);

  extra.insert(extra.end(), second.begin(), second.end());
  EXPECT_EQ(ParseDfuTransferSize(extra), 256);
}

TEST(ParseDfuTransferSizeTest, ZeroTransferSizeUsesDefault) {
  EXPECT_EQ(ParseDfuTransferSize(DfuFuncDesc(0)), kDfuDefaultTransferSize);
}

TEST(ParseDfuTransferSizeTest, DecodesLittleEndianTransferSize) {
  EXPECT_EQ(ParseDfuTransferSize(DfuFuncDesc(0x1234)), 0x1234);
}

TEST(DfuStatusResponseTest, Decodes24BitLittleEndianPollTimeout) {
  const std::array<uint8_t, kDfuGetStatusResponseLen> raw = {0x09, 0x03, 0x02,
                                                             0x01, 0x04, 0x00};

  const DfuStatusResponse response = DfuStatusResponse::FromBytes(raw);

  EXPECT_EQ(response.status, DfuStatus::kNotDone);
  // Needs all three bytes: 0x010203 ms.
  EXPECT_EQ(response.poll_timeout, milliseconds(0x010203));
  EXPECT_EQ(response.state, DfuState::kDfuDnloadBusy);
}

TEST(DfuStatusResponseTest, ZeroPollTimeout) {
  const std::array<uint8_t, kDfuGetStatusResponseLen> raw = {};

  EXPECT_EQ(DfuStatusResponse::FromBytes(raw).poll_timeout, milliseconds(0));
}

}  // namespace
}  // namespace realtekfp
