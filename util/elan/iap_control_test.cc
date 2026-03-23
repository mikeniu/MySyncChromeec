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

class IapControlTest : public ::testing::Test {
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
  EXPECT_EQ(ctrl.Initialize(kBootVid, kBootPid).error(),
            IapError::UsbInitFailed);
}

// ---------------------------------------------------------
// Application Logic Tests (Using FakeUsbBackend)
// ---------------------------------------------------------

TEST_F(IapControlTest, RunIapProcessHappyPathCompletesSuccessfully) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootVid, kBootPid).has_value());

  std::vector<uint8_t> my_rom(64, 0xAA);
  auto result = ctrl->RunIapProcess(my_rom);

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(fake_ptr->GetFlashedData(), my_rom);
}

TEST_F(IapControlTest, RejectsUnalignedRomData) {
  auto [_, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootVid, kBootPid).has_value());

  std::vector<uint8_t> unaligned_rom(61, 0xBB);  // Fails alignment check
  auto result = ctrl->RunIapProcess(unaligned_rom);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::BufferTooSmall);
}

TEST_F(IapControlTest, HandlesOddSizedPayloadChunking) {
  auto [fake_ptr, ctrl] = CreateFakeController();
  ASSERT_TRUE(ctrl->Initialize(kBootVid, kBootPid).has_value());

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
  ASSERT_TRUE(ctrl->Initialize(kBootVid, kBootPid).has_value());

  int trigger_count = 0;
  ASSERT_TRUE(ctrl->SetProgressCallback([&](size_t progress, size_t total) {
                    trigger_count++;
                    EXPECT_EQ(total, 100);
                  })
                  .has_value());

  ASSERT_TRUE(ctrl->RunIapProcess(std::vector<uint8_t>(100, 0xCC)).has_value());

  // Write (64 + 36) + Read (64 + 36) = 4 loop iterations
  EXPECT_EQ(trigger_count, 4);
}

}  // namespace
}  // namespace elan
