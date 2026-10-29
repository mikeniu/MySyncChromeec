/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
using ::testing::Invoke;
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
        RtsError::kProtocolError}) {
    EXPECT_NE(ToString(error), "Unknown error");
  }
}

}  // namespace
}  // namespace realtekfp
