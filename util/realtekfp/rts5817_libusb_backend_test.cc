/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_libusb_backend.h"

#include <gtest/gtest.h>
#include <libusb-1.0/libusb.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>

namespace realtekfp {
namespace {

using std::chrono::milliseconds;

constexpr UsbDeviceId kDevId = {.vid = 0x0BDA, .pid = 0x5817};
constexpr milliseconds kTimeout{1000};

TEST(RtsLibusbBackendTest, IsNotOpenInitially) {
  RtsLibusbBackend backend;

  EXPECT_FALSE(backend.IsOpen());
}

TEST(RtsLibusbBackendTest, CloseAndReleaseAreSafeWhenClosed) {
  RtsLibusbBackend backend;

  backend.ReleaseInterface(0);
  backend.Close();
  EXPECT_FALSE(backend.IsOpen());
}

TEST(RtsLibusbBackendTest, OpenFailsBeforeInit) {
  RtsLibusbBackend backend;

  auto res = backend.Open(kDevId);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), LIBUSB_ERROR_NO_DEVICE);
}

TEST(RtsLibusbBackendTest, ControlTransferInFailsWithoutDevice) {
  RtsLibusbBackend backend;
  std::array<uint8_t, 4> data{};

  auto res = backend.ControlTransferIn(0xC0, 0, 0, 0, data, kTimeout);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), LIBUSB_ERROR_NO_DEVICE);
}

TEST(RtsLibusbBackendTest, ControlTransferOutFailsWithoutDevice) {
  RtsLibusbBackend backend;
  const std::array<uint8_t, 4> data{};

  auto res = backend.ControlTransferOut(0x21, 0, 0, 0, data, kTimeout);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), LIBUSB_ERROR_NO_DEVICE);
}

TEST(RtsLibusbBackendTest, GetInterfacesFailsWithoutDevice) {
  RtsLibusbBackend backend;

  auto res = backend.GetInterfaces();

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), LIBUSB_ERROR_NO_DEVICE);
}

TEST(RtsLibusbBackendTest, ClaimInterfaceFailsWithoutDevice) {
  RtsLibusbBackend backend;

  auto res = backend.ClaimInterface(0);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), LIBUSB_ERROR_NO_DEVICE);
}

TEST(RtsLibusbBackendTest, ErrorNameIsLibusbsName) {
  RtsLibusbBackend backend;

  for (int error : {LIBUSB_ERROR_ACCESS, LIBUSB_ERROR_NO_DEVICE,
                    LIBUSB_ERROR_NOT_FOUND, LIBUSB_ERROR_PIPE}) {
    EXPECT_EQ(backend.ErrorName(error), libusb_error_name(error));
  }
}

}  // namespace
}  // namespace realtekfp
