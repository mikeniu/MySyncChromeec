/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "rts5817_mock_usb_backend.h"

namespace realtekfp {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Contains;
using ::testing::ElementsAreArray;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Not;
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
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<uint8_t>(value >> (8 * i));
    }
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
                          SizeIs(4), kUsbCtrlTimeout))
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

  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorBootRom)));
  ExpectGetStatus(Invoke(StatusIs(0)));
  EXPECT_CALL(*backend_, ControlTransferOut(kBmReqH2dStdDev, kRtsStdSetStatus,
                                            kRtsEnableDownloadMode, 0,
                                            IsEmpty(), kUsbCtrlTimeout))
      .WillOnce(Return(std::expected<size_t, int>(0)));

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsSetStatusWhenAlreadyActive) {
  InSequence seq;

  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorBootRom)));
  ExpectGetStatus(Invoke(StatusIs(1)));
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsGetStatusInFlashImage) {
  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorFlashImage)));
  EXPECT_CALL(*backend_, ControlTransferIn(kBmReqD2hStdDev, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  EXPECT_TRUE(device_->EnableDownload());
}

TEST_F(RtsUsbDeviceEnableDownloadTest, SkipsGetStatusForUnknownImage) {
  ExpectInitVtorQuery(Invoke(InitVtorIs(0x12345678)));
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
  ExpectInitVtorQuery(Return(std::expected<size_t, int>(2)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnGetStatusError) {
  InSequence seq;

  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorBootRom)));
  ExpectGetStatus(Return(std::unexpected(kUsbErrorPipe)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnGetStatusShortRead) {
  InSequence seq;

  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorBootRom)));
  ExpectGetStatus(Return(std::expected<size_t, int>(0)));

  auto res = device_->EnableDownload();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceEnableDownloadTest, FailsOnSetStatusError) {
  InSequence seq;

  ExpectInitVtorQuery(Invoke(InitVtorIs(kRtsInitVtorBootRom)));
  ExpectGetStatus(Invoke(StatusIs(0)));
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
        RtsError::kProtocolError, RtsError::kNoDfuInterface}) {
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
constexpr uint8_t kHidClass = 0x03;

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
  const std::vector<uint8_t> extra = {0x07, 0x21, 0x0F, 0xD0, 0x07, 0x00, 0x10};

  ASSERT_EQ(extra.size(), kDfuDescMinLen);
  EXPECT_EQ(ParseDfuTransferSize(extra), 4096);
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

// Fixture with an opened device, for the DFU interface tests.
class RtsUsbDeviceDfuTest : public RtsUsbDeviceTest {
 protected:
  RtsUsbInterface DfuInterface(int number, uint16_t transfer_size) {
    return {
        .number = number,
        .usb_class = kDfuClass,
        .usb_subclass = kDfuSubclass,
        .extra = DfuFuncDesc(transfer_size),
    };
  }

  void ExpectInterfaces(std::vector<RtsUsbInterface> interfaces) {
    EXPECT_CALL(*backend_, IsOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*backend_, GetInterfaces()).WillOnce(Return(interfaces));
  }
};

TEST_F(RtsUsbDeviceDfuTest, DetachRejectsTimeoutThatDoesNotFitInWValue) {
  EXPECT_CALL(*backend_, GetInterfaces()).Times(0);
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);

  for (milliseconds timeout : {milliseconds(-1), milliseconds(65536)}) {
    auto res = device_->DfuDetach(timeout);

    ASSERT_FALSE(res);
    EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
  }
}

TEST_F(RtsUsbDeviceDfuTest, DetachAcceptsTheLargestTimeout) {
  ExpectInterfaces({DfuInterface(2, 2048)});
  EXPECT_CALL(*backend_, ClaimInterface(2))
      .WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_,
              ControlTransferOut(kBmReqH2dClsIf,
                                 std::to_underlying(DfuRequest::kDetach), 65535,
                                 2, IsEmpty(), kUsbCtrlTimeout))
      .WillOnce(Return(std::expected<size_t, int>(0)));
  EXPECT_CALL(*backend_, ReleaseInterface(2));

  EXPECT_TRUE(device_->DfuDetach(milliseconds(65535)));
}

TEST_F(RtsUsbDeviceDfuTest, DetachRequiresOpenDevice) {
  EXPECT_CALL(*backend_, IsOpen()).WillRepeatedly(Return(false));
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsWhenConfigDescriptorUnavailable) {
  EXPECT_CALL(*backend_, IsOpen()).WillRepeatedly(Return(true));
  EXPECT_CALL(*backend_, GetInterfaces()).WillOnce(Return(std::unexpected(-1)));
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kUsbError);
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsWithoutDfuInterface) {
  ExpectInterfaces({
      RtsUsbInterface{
          .number = 0,
          .usb_class = kHidClass,
          .usb_subclass = kDfuSubclass,
      },
  });
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kNoDfuInterface);
}

TEST_F(RtsUsbDeviceDfuTest, DetachRequiresMatchingClassAndSubclass) {
  RtsUsbInterface wrong_subclass = DfuInterface(1, 1024);

  wrong_subclass.usb_subclass = kDfuSubclass + 1;
  ExpectInterfaces({wrong_subclass});

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kNoDfuInterface);
}

TEST_F(RtsUsbDeviceDfuTest, DetachClaimsInterfaceAroundRequest) {
  InSequence seq;

  ExpectInterfaces({RtsUsbInterface{.number = 0}, DfuInterface(2, 2048)});
  EXPECT_CALL(*backend_, ClaimInterface(2))
      .WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_,
              ControlTransferOut(kBmReqH2dClsIf,
                                 std::to_underlying(DfuRequest::kDetach), 1234,
                                 2, IsEmpty(), kUsbCtrlTimeout))
      .WillOnce(Return(std::expected<size_t, int>(0)));
  EXPECT_CALL(*backend_, ReleaseInterface(2));

  EXPECT_TRUE(device_->DfuDetach(milliseconds(1234)));
}

TEST_F(RtsUsbDeviceDfuTest, DetachFailsWhenClaimFails) {
  ExpectInterfaces({DfuInterface(2, 2048)});
  EXPECT_CALL(*backend_, ClaimInterface(2))
      .WillOnce(Return(std::unexpected(kUsbErrorAccess)));
  EXPECT_CALL(*backend_, ReleaseInterface(_)).Times(0);
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _)).Times(0);

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
}

TEST_F(RtsUsbDeviceDfuTest, DetachReleasesInterfaceOnTransferError) {
  InSequence seq;

  ExpectInterfaces({DfuInterface(0, 1024)});
  EXPECT_CALL(*backend_, ClaimInterface(0))
      .WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, ControlTransferOut(_, _, _, _, _, _))
      .WillOnce(Return(std::unexpected(kUsbErrorPipe)));
  EXPECT_CALL(*backend_, ReleaseInterface(0));

  auto res = device_->DfuDetach(milliseconds(1000));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
}

// A scripted RTS5817 in DFU mode, behind the mocked control channel. It models
// just enough of the DFU state machine for DfuDownload() to drive it, and
// records what the host sends.
class FakeDfuDevice {
 public:
  struct Block {
    uint16_t number;
    std::vector<uint8_t> data;
  };

  // The state returned by GETSTATUS until a request changes it.
  DfuState state = DfuState::kDfuIdle;
  uint8_t status = 0;
  // State to report after the final zero-length DNLOAD.
  DfuState manifest_result = DfuState::kDfuIdle;
  // Reject block `fail_block` / the final ZLP with a stall.
  int fail_block = -1;
  bool stall_zlp = false;
  // Extra GETSTATUS replies with a poll timeout, before settling.
  int busy_polls = 0;
  // Fail every GETSTATUS once the final ZLP was accepted.
  bool fail_status_after_zlp = false;

  std::vector<Block> blocks;
  std::vector<DfuRequest> requests;
  std::vector<DfuState> state_when_cleared;

  void Attach(MockRtsUsbBackend& backend, int interface) {
    EXPECT_CALL(backend, ControlTransferIn(_, _, _, _, _, _))
        .WillRepeatedly([this](uint8_t, uint8_t, uint16_t, uint16_t,
                               std::span<uint8_t> data, milliseconds) {
          if (zlp_done_ && fail_status_after_zlp) {
            return std::expected<size_t, int>(
                std::unexpected(kUsbErrorNoDevice));
          }
          return GetStatus(data);
        });
    EXPECT_CALL(backend, ControlTransferOut(_, _, _, _, _, _))
        .WillRepeatedly([this, interface](uint8_t, uint8_t request,
                                          uint16_t value, uint16_t index,
                                          std::span<const uint8_t> data,
                                          milliseconds) {
          EXPECT_EQ(index, interface);
          return Out(static_cast<DfuRequest>(request), value, data);
        });
  }

 private:
  bool zlp_done_ = false;

  std::expected<size_t, int> GetStatus(std::span<uint8_t> data) {
    std::fill(data.begin(), data.end(), 0);
    data[kDfuStatusOff] = status;
    data[kDfuStateOff] = std::to_underlying(state);
    if (busy_polls > 0) {
      --busy_polls;
      data[kDfuPollTimeoutOff] = 5;
      data[kDfuStateOff] = std::to_underlying(DfuState::kDfuDnloadBusy);
    }
    return data.size();
  }

  std::expected<size_t, int> Out(DfuRequest request, uint16_t value,
                                 std::span<const uint8_t> data) {
    requests.push_back(request);
    switch (request) {
      case DfuRequest::kDownload:
        if (data.empty()) {
          if (stall_zlp) {
            state = DfuState::kDfuError;
            status = std::to_underlying(DfuStatus::kNotDone);
            return std::unexpected(kUsbErrorPipe);
          }
          blocks.push_back({value, {}});
          state = manifest_result;
          zlp_done_ = true;
          return size_t{0};
        }
        if (static_cast<int>(value) == fail_block) {
          return std::unexpected(kUsbErrorPipe);
        }
        blocks.push_back(
            {value, std::vector<uint8_t>(data.begin(), data.end())});
        state = DfuState::kDfuDnloadIdle;
        return data.size();
      case DfuRequest::kClrStatus:
        state_when_cleared.push_back(state);
        if (state != DfuState::kDfuError) {
          // The RTS5817 stalls CLRSTATUS outside dfuERROR and enters it.
          state = DfuState::kDfuError;
          return std::unexpected(kUsbErrorPipe);
        }
        state = DfuState::kDfuIdle;
        status = 0;
        return size_t{0};
      case DfuRequest::kAbort:
        state = DfuState::kDfuIdle;
        return size_t{0};
      default:
        return size_t{0};
    }
  }
};

constexpr uint16_t kTransferSize = 4;
constexpr int kDfuIf = 1;

// An opened device in DFU mode, advertising kTransferSize.
class RtsUsbDeviceDownloadTest : public RtsUsbDeviceDfuTest {
 protected:
  void SetUp() override {
    RtsUsbDeviceDfuTest::SetUp();
    EXPECT_CALL(*backend_, IsOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*backend_, GetInterfaces())
        .WillRepeatedly(Return(
            std::vector<RtsUsbInterface>{DfuInterface(kDfuIf, kTransferSize)}));
    EXPECT_CALL(*backend_, ClaimInterface(kDfuIf))
        .WillRepeatedly(Return(std::expected<void, int>{}));
    EXPECT_CALL(*backend_, ReleaseInterface(kDfuIf)).Times(AnyNumber());
    fake_.Attach(*backend_, kDfuIf);
  }

  FakeDfuDevice fake_;
};

TEST_F(RtsUsbDeviceDownloadTest, ChunksAcrossTransferSizeBoundaries) {
  // kTransferSize + 1 bytes: one full block, a 1-byte tail, and a ZLP.
  const std::vector<uint8_t> fw = {0x10, 0x20, 0x30, 0x40, 0x50};

  ASSERT_TRUE(device_->DfuDownload(fw));
  ASSERT_EQ(fake_.blocks.size(), 3u);
  EXPECT_EQ(fake_.blocks[0].number, 0);
  EXPECT_EQ(fake_.blocks[0].data,
            (std::vector<uint8_t>{0x10, 0x20, 0x30, 0x40}));
  EXPECT_EQ(fake_.blocks[1].number, 1);
  EXPECT_EQ(fake_.blocks[1].data, (std::vector<uint8_t>{0x50}));
  EXPECT_EQ(fake_.blocks[2].number, 2);
  EXPECT_TRUE(fake_.blocks[2].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageSmallerThanOneBlock) {
  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{0x01, 0x02}));
  ASSERT_EQ(fake_.blocks.size(), 2u);
  EXPECT_EQ(fake_.blocks[0].data, (std::vector<uint8_t>{0x01, 0x02}));
  EXPECT_TRUE(fake_.blocks[1].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageExactlyOneBlock) {
  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4}));
  ASSERT_EQ(fake_.blocks.size(), 2u);
  EXPECT_EQ(fake_.blocks[0].data, (std::vector<uint8_t>{1, 2, 3, 4}));
  EXPECT_TRUE(fake_.blocks[1].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ImageOfExactMultipleOfBlockSize) {
  ASSERT_TRUE(
      device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
  ASSERT_EQ(fake_.blocks.size(), 3u);
  EXPECT_EQ(fake_.blocks[0].data, (std::vector<uint8_t>{1, 2, 3, 4}));
  EXPECT_EQ(fake_.blocks[1].data, (std::vector<uint8_t>{5, 6, 7, 8}));
  EXPECT_EQ(fake_.blocks[2].number, 2);
  EXPECT_TRUE(fake_.blocks[2].data.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, RejectsEmptyFirmware) {
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);

  auto res = device_->DfuDownload({});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
}

TEST_F(RtsUsbDeviceDownloadTest, RejectsImageNeedingTooManyBlocks) {
  // wValue is 16 bits, so a block count that does not fit must be refused
  // before anything is sent.
  const std::vector<uint8_t> fw(static_cast<size_t>(kTransferSize) * 65535);

  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);

  auto res = device_->DfuDownload(fw);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
  EXPECT_TRUE(fake_.blocks.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, AcceptsLargestNumberableImage) {
  // The ZLP is numbered after the last data block, so the block count itself
  // must still fit in 16 bits.
  const std::vector<uint8_t> fw(static_cast<size_t>(kTransferSize) * 65534);

  ASSERT_TRUE(device_->DfuDownload(fw));
  EXPECT_EQ(fake_.blocks.back().number, 65534);
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWithoutOpenDevice) {
  EXPECT_CALL(*backend_, IsOpen()).WillOnce(Return(false));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenNoDfuInterface) {
  EXPECT_CALL(*backend_, GetInterfaces())
      .WillOnce(Return(std::vector<RtsUsbInterface>{}));
  EXPECT_CALL(*backend_, ClaimInterface(_)).Times(0);

  auto res = device_->DfuDownload(std::vector<uint8_t>{1});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kNoDfuInterface);
}

TEST_F(RtsUsbDeviceDownloadTest, ClaimFailureLeavesNothingToRelease) {
  EXPECT_CALL(*backend_, ClaimInterface(kDfuIf))
      .WillOnce(Return(std::unexpected(kUsbErrorAccess)));
  EXPECT_CALL(*backend_, ReleaseInterface(_)).Times(0);

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
}

TEST_F(RtsUsbDeviceDownloadTest, ReleasesInterfaceOnSuccess) {
  InSequence seq;

  EXPECT_CALL(*backend_, ClaimInterface(kDfuIf))
      .WillOnce(Return(std::expected<void, int>{}));
  EXPECT_CALL(*backend_, ReleaseInterface(kDfuIf)).Times(1);

  EXPECT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
}

TEST_F(RtsUsbDeviceDownloadTest, WaitsThroughBusyStatesWithPollTimeout) {
  fake_.busy_polls = 2;

  EXPECT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
  EXPECT_FALSE(slept_.empty());
  EXPECT_EQ(slept_.front(), milliseconds(5));
}

TEST_F(RtsUsbDeviceDownloadTest, TransferFailureReleasesInterface) {
  fake_.fail_block = 1;
  EXPECT_CALL(*backend_, ReleaseInterface(kDfuIf)).Times(1);

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4, 5, 6});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
  ASSERT_EQ(fake_.blocks.size(), 1u);
}

TEST_F(RtsUsbDeviceDownloadTest, ShortWriteIsAnError) {
  EXPECT_CALL(*backend_, ControlTransferOut(kBmReqH2dClsIf, _, 0, kDfuIf,
                                            SizeIs(kTransferSize), _))
      .WillOnce(Return(std::expected<size_t, int>(kTransferSize - 1)));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4, 5, 6});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceDownloadTest, ClearStatusIsOnlySentInErrorState) {
  // After a failed block the device is not in dfuERROR, so CLRSTATUS (which
  // the RTS5817 would stall, and enter dfuERROR) must not be sent.
  fake_.fail_block = 0;

  EXPECT_FALSE(device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4}));
  EXPECT_TRUE(fake_.state_when_cleared.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ClearsStatusWhenDeviceIsInErrorState) {
  fake_.fail_block = 0;
  EXPECT_CALL(*backend_,
              ControlTransferOut(kBmReqH2dClsIf,
                                 std::to_underlying(DfuRequest::kDownload), 0,
                                 kDfuIf, _, _))
      .WillOnce([this](uint8_t, uint8_t, uint16_t, uint16_t,
                       std::span<const uint8_t>, milliseconds) {
        fake_.state = DfuState::kDfuError;
        fake_.status = std::to_underlying(DfuStatus::kNotDone);
        return std::expected<size_t, int>(std::unexpected(kUsbErrorPipe));
      });

  EXPECT_FALSE(device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4}));
  EXPECT_EQ(fake_.state_when_cleared,
            (std::vector<DfuState>{DfuState::kDfuError}));
}

TEST_F(RtsUsbDeviceDownloadTest, PollFailureReleasesInterface) {
  EXPECT_CALL(*backend_, ReleaseInterface(kDfuIf)).Times(1);
  fake_.status = std::to_underlying(DfuStatus::kNotDone);
  fake_.state = DfuState::kDfuIdle;

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2, 3, 4, 5, 6});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceRejected);
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenFinalZlpIsStalled) {
  // The RTS5817 validates the image in the final ZLP. Rejecting it must fail
  // the download instead of only warning.
  fake_.stall_zlp = true;

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
  // The device is in dfuERROR after the stall, so it is cleared.
  EXPECT_EQ(fake_.state_when_cleared,
            (std::vector<DfuState>{DfuState::kDfuError}));
  // The device is not told to boot an image it rejected.
  EXPECT_THAT(fake_.requests, Not(Contains(DfuRequest::kDetach)));
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenManifestDoesNotReachIdle) {
  // dfuDNLOAD-IDLE after the ZLP means the image was not accepted.
  fake_.manifest_result = DfuState::kDfuDnloadIdle;

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
  EXPECT_THAT(fake_.requests, Not(Contains(DfuRequest::kDetach)));
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenManifestStatusPollFails) {
  // The device goes away while it is verifying the image.
  fake_.fail_status_after_zlp = true;

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
  EXPECT_THAT(fake_.requests, Not(Contains(DfuRequest::kDetach)));
}

TEST_F(RtsUsbDeviceDownloadTest, DetachesAfterManifestSoDeviceBoots) {
  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));

  // Download blocks and the ZLP, and only then DFU_DETACH.
  ASSERT_FALSE(fake_.requests.empty());
  EXPECT_EQ(fake_.requests.back(), DfuRequest::kDetach);
  EXPECT_EQ(std::count(fake_.requests.begin(), fake_.requests.end(),
                       DfuRequest::kDetach),
            1);
}

TEST_F(RtsUsbDeviceDownloadTest, DetachFailureIsReported) {
  EXPECT_CALL(*backend_,
              ControlTransferOut(kBmReqH2dClsIf,
                                 std::to_underlying(DfuRequest::kDetach), _,
                                 kDfuIf, _, _))
      .WillOnce(Return(std::unexpected(kUsbErrorNoDevice)));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
}

// The states a previous, interrupted transfer can leave the device in.
TEST_F(RtsUsbDeviceDownloadTest, RecoversFromErrorStateBeforeDownloading) {
  fake_.state = DfuState::kDfuError;
  fake_.status = std::to_underlying(DfuStatus::kNotDone);

  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
  ASSERT_FALSE(fake_.requests.empty());
  EXPECT_EQ(fake_.requests.front(), DfuRequest::kClrStatus);
  EXPECT_EQ(fake_.state_when_cleared,
            (std::vector<DfuState>{DfuState::kDfuError}));
}

TEST_F(RtsUsbDeviceDownloadTest, AbortsLeftoverDownloadBeforeDownloading) {
  fake_.state = DfuState::kDfuDnloadIdle;

  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
  ASSERT_FALSE(fake_.requests.empty());
  EXPECT_EQ(fake_.requests.front(), DfuRequest::kAbort);
  EXPECT_TRUE(fake_.state_when_cleared.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, AbortsLeftoverUploadBeforeDownloading) {
  fake_.state = DfuState::kDfuUploadIdle;

  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
  ASSERT_FALSE(fake_.requests.empty());
  EXPECT_EQ(fake_.requests.front(), DfuRequest::kAbort);
}

TEST_F(RtsUsbDeviceDownloadTest, LeavesIdleDeviceAloneBeforeDownloading) {
  ASSERT_TRUE(device_->DfuDownload(std::vector<uint8_t>{1, 2}));
  ASSERT_FALSE(fake_.requests.empty());
  EXPECT_EQ(fake_.requests.front(), DfuRequest::kDownload);
}

TEST_F(RtsUsbDeviceDownloadTest, FailsWhenRecoveryStatusReadFails) {
  EXPECT_CALL(*backend_, ControlTransferIn(_, _, _, _, _, _))
      .WillOnce(Return(std::unexpected(kUsbErrorNoDevice)));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
  EXPECT_TRUE(fake_.blocks.empty());
}

TEST_F(RtsUsbDeviceDownloadTest, ReportsShortStatusRead) {
  EXPECT_CALL(*backend_, ControlTransferIn(_, _, _, _, _, _))
      .WillOnce(
          Return(std::expected<size_t, int>(kDfuGetStatusResponseLen - 1)));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
}

TEST_F(RtsUsbDeviceDownloadTest, ResetsDeviceWhenItRequestsOneAfterManifest) {
  fake_.manifest_result = DfuState::kDfuManifestWaitReset;
  EXPECT_CALL(*backend_, ResetDevice()).WillOnce(Return(0));

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  // The device settled in a state other than dfuIDLE, so the image is not
  // reported as accepted, and the device is not detached.
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kProtocolError);
  EXPECT_THAT(fake_.requests, Not(Contains(DfuRequest::kDetach)));
}

TEST_F(RtsUsbDeviceDownloadTest, TimesOutWhenDeviceStaysBusy) {
  fake_.busy_polls = 1 << 20;

  auto res = device_->DfuDownload(std::vector<uint8_t>{1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kTimeout);
  // The sleeps never add up to more than the overall timeout.
  milliseconds total{0};
  for (milliseconds delay : slept_) {
    total += delay;
  }
  EXPECT_LE(total, milliseconds(kUsbDfuTimeout));
}

TEST(DfuStatusResponseTest, Decodes24BitLittleEndianPollTimeout) {
  const std::array<uint8_t, kDfuGetStatusResponseLen> raw = {0x09, 0x03, 0x02,
                                                             0x01, 0x04, 0x00};

  const DfuStatusResponse response = DfuStatusResponse::FromBytes(raw);

  EXPECT_EQ(response.status, 0x09);
  // Needs all three bytes: 0x010203 ms.
  EXPECT_EQ(response.poll_timeout, milliseconds(0x010203));
  EXPECT_EQ(response.state, 4);
}

TEST(DfuStatusResponseTest, ZeroPollTimeout) {
  const std::array<uint8_t, kDfuGetStatusResponseLen> raw = {};

  EXPECT_EQ(DfuStatusResponse::FromBytes(raw).poll_timeout, milliseconds(0));
}

// A valid DFU suffix with the given bLength.
std::vector<uint8_t> DfuSuffix(uint8_t length = kDfuSuffixLen) {
  return {0x00, 0x00, 0x17, 0x58,   0xDA, 0x0B, 0x00, 0x01,
          'U',  'F',  'D',  length, 0x11, 0x22, 0x33, 0x44};
}

std::vector<uint8_t> WithSuffix(std::vector<uint8_t> payload,
                                const std::vector<uint8_t>& suffix) {
  payload.insert(payload.end(), suffix.begin(), suffix.end());
  return payload;
}

TEST(StripDfuSuffixTest, RemovesValidSuffix) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
  const std::vector<uint8_t> fw = WithSuffix(payload, DfuSuffix());

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(payload));
}

TEST(StripDfuSuffixTest, SuffixOnlyLeavesEmptyImage) {
  const std::vector<uint8_t> fw = DfuSuffix();

  EXPECT_TRUE(StripDfuSuffix(fw).empty());
}

TEST(StripDfuSuffixTest, HonorsLongerBLength) {
  constexpr size_t kExtraSuffixBytes = 4;
  const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
  const std::vector<uint8_t> extra(kExtraSuffixBytes, 0xEE);
  const std::vector<uint8_t> fw = WithSuffix(
      WithSuffix(payload, extra), DfuSuffix(kDfuSuffixLen + kExtraSuffixBytes));

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(payload));
}

TEST(StripDfuSuffixTest, KeepsImageWithoutSignature) {
  std::vector<uint8_t> suffix = DfuSuffix();
  suffix[kDfuSuffixSigOff] = 'X';
  const std::vector<uint8_t> fw = WithSuffix({1, 2, 3}, suffix);

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(fw));
}

TEST(StripDfuSuffixTest, KeepsImageWhenBLengthSmallerThanSuffix) {
  const std::vector<uint8_t> fw =
      WithSuffix({1, 2, 3}, DfuSuffix(kDfuSuffixLen - 1));

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(fw));
}

TEST(StripDfuSuffixTest, KeepsImageWhenBLengthIsZero) {
  const std::vector<uint8_t> fw = WithSuffix({1, 2, 3}, DfuSuffix(0));

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(fw));
}

TEST(StripDfuSuffixTest, BLengthMayCoverTheWholeImage) {
  // The largest bLength that is still accepted is the image size, which
  // leaves nothing but the suffix and so an empty payload.
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  const std::vector<uint8_t> fw = WithSuffix(
      payload, DfuSuffix(static_cast<uint8_t>(kDfuSuffixLen + payload.size())));

  ASSERT_EQ(fw.size(), kDfuSuffixLen + payload.size());
  EXPECT_TRUE(StripDfuSuffix(fw).empty());
}

TEST(StripDfuSuffixTest, KeepsImageWhenBLengthExceedsImageSize) {
  // One more byte than the image has.
  const std::vector<uint8_t> payload = {1, 2, 3};
  const std::vector<uint8_t> fw = WithSuffix(
      payload,
      DfuSuffix(static_cast<uint8_t>(kDfuSuffixLen + payload.size() + 1)));

  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(fw));
}

TEST(StripDfuSuffixTest, KeepsImageOneByteShorterThanSuffix) {
  std::vector<uint8_t> fw = DfuSuffix();
  fw.pop_back();

  ASSERT_EQ(fw.size(), kDfuSuffixLen - 1);
  EXPECT_THAT(StripDfuSuffix(fw), ElementsAreArray(fw));
}

TEST(StripDfuSuffixTest, KeepsEmptyImage) {
  EXPECT_TRUE(StripDfuSuffix({}).empty());
}

}  // namespace
}  // namespace realtekfp
