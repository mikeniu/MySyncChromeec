// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iap_control.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "fake_usb_backend.h"
#include "mock_usb_backend.h"

using ::testing::_;
using ::testing::Return;

namespace elan {
namespace {

class IapControlTest : public testing::Test {
 protected:
  // Helper to quickly setup a controller with a Fake backend
  std::pair<FakeUsbBackend*, std::unique_ptr<IapControl>>
  CreateFakeController() {
    auto fake = std::make_unique<FakeUsbBackend>();
    auto* ptr = fake.get();
    auto ctrl = std::make_unique<IapControl>(std::move(fake));
    return {ptr, std::move(ctrl)};
  }
};

// ---------------------------------------------------------
// Hardware Failure Tests (Using MockUsbBackend)
// ---------------------------------------------------------

TEST_F(IapControlTest, InitializeFailsWhenLibusbInitFails) {
  auto mock = std::make_unique<MockUsbBackend>();
  EXPECT_CALL(*mock, Initialize()).WillOnce(Return(-99));

  IapControl ctrl(std::move(mock));
  EXPECT_EQ(ctrl.Initialize(kBootDevice).error(), IapError::UsbInitFailed);
}

// ---------------------------------------------------------
// Application Logic Tests (Using FakeUsbBackend)
// ---------------------------------------------------------

TEST_F(IapControlTest, RunIapProcessHappyPathCompletesSuccessfully) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  std::vector<uint8_t> my_rom(64, 0xAA);
  auto result = ctrl->RunIapProcess(my_rom);

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(fake_ptr->GetFlashedData(), my_rom);
}

TEST_F(IapControlTest, RejectsUnalignedRomData) {
  auto [_, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  std::vector<uint8_t> unaligned_rom(61, 0xBB);
  auto result = ctrl->RunIapProcess(unaligned_rom);

  ASSERT_FALSE(result.has_value());
  // Update to check for the new error code
  EXPECT_EQ(result.error(), IapError::InvalidAlignment);
}

TEST_F(IapControlTest, HandlesOddSizedPayloadChunking) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  // 100 bytes forces the loop to handle a 64-byte chunk and a 36-byte chunk
  std::vector<uint8_t> odd_rom(100);
  for (size_t i = 0; i < odd_rom.size(); ++i)
    odd_rom[i] = static_cast<uint8_t>(i);

  auto result = ctrl->RunIapProcess(odd_rom);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(fake_ptr->GetFlashedData(), odd_rom);
}

TEST_F(IapControlTest, ProgressCallbackFiresCorrectly) {
  auto [_, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  int trigger_count = 0;
  ctrl->SetProgressCallback([&](size_t progress, size_t total) {
    trigger_count++;
    EXPECT_EQ(total, 100);
  });

  ASSERT_TRUE(ctrl->RunIapProcess(std::vector<uint8_t>(100, 0xCC)).has_value());

  // Write (64 + 36) + Read (64 + 36) = 4 loop iterations
  EXPECT_EQ(trigger_count, 4);
}

TEST_F(IapControlTest, AbortsOnInvalidFirmwarePrefix) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  // Inject a bad prefix (e.g., 0x20 instead of the required 0x10)
  fake_ptr->fw_version = (0x20 << IapControl::kFwVerPrefixShift);

  std::vector<uint8_t> valid_rom(64, 0xAA);
  auto result = ctrl->RunIapProcess(valid_rom);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::InvalidFirmware);
}

TEST_F(IapControlTest, AbortsOnDataMismatch) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootDevice).has_value());

  // Trigger the corruption hook for the read-back phase
  fake_ptr->corrupt_readback = true;

  std::vector<uint8_t> valid_rom(64, 0xAA);
  auto result = ctrl->RunIapProcess(valid_rom);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::DataMismatch);
}

TEST_F(IapControlTest, AbortsOnHardwareTransferFailure) {
  auto mock = std::make_unique<MockUsbBackend>();

  // Setup basic initialization
  EXPECT_CALL(*mock, Initialize()).WillOnce(Return(0));
  EXPECT_CALL(*mock, OpenDevice(_)).WillOnce(Return(0));

  // The first action in RunIapProcess is AbortProcess, which sends a command.
  // We simulate a raw libusb I/O error happening on that first transfer.
  EXPECT_CALL(*mock, InterruptTransfer(IapControl::kEpCmdOut, _, _))
      .WillOnce(Return(LIBUSB_ERROR_IO));

  IapControl ctrl(std::move(mock));
  ASSERT_TRUE(ctrl.Initialize(kBootDevice).has_value());

  std::vector<uint8_t> valid_rom(64, 0xAA);
  auto result = ctrl.RunIapProcess(valid_rom);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::TransferFailed);
}

}  // namespace
}  // namespace elan
