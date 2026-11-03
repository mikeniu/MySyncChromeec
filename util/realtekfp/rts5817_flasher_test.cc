/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flasher.h"

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
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "rts5817_usb_backend.h"
#include "rts5817_usb_device.h"

namespace realtekfp {
namespace {

using ::testing::ElementsAre;

using std::chrono::milliseconds;

constexpr uint16_t kTransferSize = 4;

// A valid 16-byte DFU suffix: bcdDevice, idProduct, idVendor, bcdDFU, "UFD",
// bLength 16 and dwCRC.
const std::vector<uint8_t> kDfuSuffixBytes = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'U',  'F',  'D',  0x10, 0x00, 0x00, 0x00, 0x00};

// Number of failed polls that still leave the last one succeeding.
constexpr int kMaxReEnumPolls =
    static_cast<int>(kReEnumTimeout / kReEnumPollInterval);

// What the fake did, in order, for the tests to assert on.
enum class Event {
  kOpenedRuntime,
  kOpenedDfu,
  kReadInitVtor,
  kGetStatusBootRom,
  kSetStatus,
  kDetach,
  kDownloadBlock,
  kDownloadZlp,
  kReenumeratedAsDfu,
  kReenumeratedAsRuntime,
};

// A whole RTS5817 behind RtsUsbBackend: the USB bus, a device that changes
// identity as the real one does, and its DFU state machine.
class FakeBus : public RtsUsbBackend {
 public:
  enum class Mode { kBootRom, kFlashImage, kDfu };

  explicit FakeBus(Mode start) : mode_(start) { Present(IdFor(start)); }

  // Bus control.
  void Present(UsbDeviceId id) { present_.insert(Key(id)); }
  void Absent(UsbDeviceId id) { present_.erase(Key(id)); }
  void Unreadable(UsbDeviceId id) { unreadable_.insert(Key(id)); }
  // Opens needed after DFU_DETACH before the new identity shows up.
  int polls_before_reenumeration = 0;

  // Device behavior.
  bool reject_image = false;
  bool stall_mode_query = false;
  bool fail_get_status = false;
  bool fail_detach = false;

  std::vector<Event> events;
  std::vector<uint8_t> flashed;
  std::vector<DfuRequest> dfu_requests;

  Mode mode() const { return mode_; }

  // RtsUsbBackend.
  std::expected<void, int> Init() override { return {}; }

  std::expected<void, int> Open(UsbDeviceId id) override {
    Settle();
    if (!present_.contains(Key(id))) {
      return std::unexpected(kUsbErrorNotFound);
    }
    if (unreadable_.contains(Key(id))) {
      return std::unexpected(kUsbErrorAccess);
    }
    open_ = id;
    events.push_back(id.pid == kRts5817DfuId.pid ? Event::kOpenedDfu
                                                 : Event::kOpenedRuntime);
    return {};
  }

  void Close() override { open_.reset(); }
  bool IsOpen() const override { return open_.has_value(); }

  std::expected<size_t, int> ControlTransferIn(uint8_t request_type,
                                               uint8_t request, uint16_t,
                                               uint16_t,
                                               std::span<uint8_t> data,
                                               milliseconds) override {
    if (!IsOpen()) {
      return std::unexpected(kUsbErrorNoDevice);
    }

    if (request_type == kBmReqD2hVendorDev) {
      events.push_back(Event::kReadInitVtor);
      if (stall_mode_query || mode_ == Mode::kDfu) {
        return std::unexpected(kUsbErrorPipe);
      }
      const uint32_t vtor = mode_ == Mode::kBootRom ? kRtsInitVtorBootRom
                                                    : kRtsInitVtorFlashImage;
      std::ranges::copy(std::bit_cast<std::array<uint8_t, sizeof(vtor)>>(vtor),
                        data.begin());
      return data.size();
    }

    if (request == kRtsStdGetStatus) {
      events.push_back(Event::kGetStatusBootRom);
      // Only the Boot ROM implements this request.
      if (mode_ != Mode::kBootRom || fail_get_status) {
        return std::unexpected(kUsbErrorPipe);
      }
      data[0] = download_enabled_ ? 1 : 0;
      return size_t{1};
    }

    // DFU_GETSTATUS.
    std::ranges::fill(data, 0);
    data[kDfuStatusOff] = dfu_status_;
    data[kDfuStateOff] = std::to_underlying(dfu_state_);
    return data.size();
  }

  std::expected<size_t, int> ControlTransferOut(uint8_t request_type,
                                                uint8_t request, uint16_t,
                                                uint16_t,
                                                std::span<const uint8_t> data,
                                                milliseconds) override {
    if (!IsOpen()) {
      return std::unexpected(kUsbErrorNoDevice);
    }

    if (request_type == kBmReqH2dStdDev) {
      events.push_back(Event::kSetStatus);
      download_enabled_ = true;
      return size_t{0};
    }

    dfu_requests.push_back(static_cast<DfuRequest>(request));
    switch (static_cast<DfuRequest>(request)) {
      case DfuRequest::kDetach:
        events.push_back(Event::kDetach);
        if (fail_detach) {
          return std::unexpected(kUsbErrorPipe);
        }
        Detach();
        return size_t{0};

      case DfuRequest::kDownload:
        if (data.empty()) {
          events.push_back(Event::kDownloadZlp);
          if (reject_image) {
            dfu_state_ = DfuState::kDfuError;
            dfu_status_ = std::to_underlying(DfuStatus::kNotDone);
            return std::unexpected(kUsbErrorPipe);
          }
          dfu_state_ = DfuState::kDfuIdle;
          return size_t{0};
        }
        events.push_back(Event::kDownloadBlock);
        flashed.insert(flashed.end(), data.begin(), data.end());
        dfu_state_ = DfuState::kDfuDnloadIdle;
        return data.size();

      default:
        return size_t{0};
    }
  }

  std::expected<std::vector<RtsUsbInterface>, int> GetInterfaces() override {
    // The DFU interface exists in both modes (the runtime one is what
    // DFU_DETACH is sent to).
    return std::vector<RtsUsbInterface>{{
        .number = 0,
        .usb_class = kDfuClass,
        .usb_subclass = kDfuSubclass,
        .extra = {0x07, 0x21, 0x0F, 0xD0, 0x07, kTransferSize & 0xFF,
                  kTransferSize >> 8},
    }};
  }

  std::expected<void, int> ClaimInterface(int) override { return {}; }
  void ReleaseInterface(int) override {}
  std::string_view ErrorName(int) const override { return "error"; }

 private:
  static UsbDeviceId IdFor(Mode mode) {
    return mode == Mode::kDfu ? kRts5817DfuId : kRts5817RuntimeId;
  }

  static uint32_t Key(UsbDeviceId id) {
    return (static_cast<uint32_t>(id.vid) << 16) | id.pid;
  }

  // DFU_DETACH: the device leaves the bus and comes back as the other mode.
  void Detach() {
    const bool to_dfu = mode_ != Mode::kDfu;

    Absent(IdFor(mode_));
    mode_ = to_dfu ? Mode::kDfu : Mode::kFlashImage;
    pending_ = IdFor(mode_);
    pending_polls_ = polls_before_reenumeration;
    open_.reset();
    dfu_state_ = DfuState::kDfuIdle;
    dfu_status_ = 0;
  }

  // Brings a pending re-enumeration closer by one poll.
  void Settle() {
    if (!pending_) {
      return;
    }
    if (pending_polls_-- > 0) {
      return;
    }
    Present(*pending_);
    events.push_back(pending_->pid == kRts5817DfuId.pid
                         ? Event::kReenumeratedAsDfu
                         : Event::kReenumeratedAsRuntime);
    pending_.reset();
  }

  Mode mode_;
  std::set<uint32_t> present_;
  std::set<uint32_t> unreadable_;
  std::optional<UsbDeviceId> open_;
  std::optional<UsbDeviceId> pending_;
  int pending_polls_ = 0;
  bool download_enabled_ = false;
  DfuState dfu_state_ = DfuState::kDfuIdle;
  uint8_t dfu_status_ = 0;
};

class FlasherTest : public ::testing::Test {
 protected:
  // Takes ownership of `bus` in the device but keeps a pointer to inspect it.
  void Start(FakeBus::Mode mode) {
    auto bus = std::make_unique<FakeBus>(mode);

    bus_ = bus.get();
    device_ = std::make_unique<RtsUsbDevice>(
        std::move(bus), [this](milliseconds delay) { slept_ += delay; });
  }

  std::expected<void, RtsError> Run(
      const std::vector<uint8_t>& fw,
      std::optional<UsbDeviceId> requested = std::nullopt) {
    return Flash(*device_, requested, fw);
  }

  bool Saw(Event event) const {
    return std::ranges::contains(bus_->events, event);
  }

  FakeBus* bus_ = nullptr;
  std::unique_ptr<RtsUsbDevice> device_;
  milliseconds slept_{0};
};

TEST_F(FlasherTest, FlashesARuntimeDeviceThroughDfuMode) {
  Start(FakeBus::Mode::kBootRom);
  const std::vector<uint8_t> fw = {1, 2, 3, 4, 5, 6};

  ASSERT_TRUE(Run(fw));

  EXPECT_EQ(bus_->flashed, fw);
  EXPECT_THAT(
      bus_->events,
      ElementsAre(Event::kOpenedRuntime, Event::kReadInitVtor,
                  Event::kGetStatusBootRom, Event::kSetStatus, Event::kDetach,
                  Event::kReenumeratedAsDfu, Event::kOpenedDfu,
                  Event::kDownloadBlock, Event::kDownloadBlock,
                  Event::kDownloadZlp, Event::kDetach,
                  Event::kReenumeratedAsRuntime, Event::kOpenedRuntime));
  EXPECT_EQ(bus_->mode(), FakeBus::Mode::kFlashImage);
}

TEST_F(FlasherTest, PollsUntilTheDfuDeviceAppears) {
  constexpr int kPollsBeforeReenum = 3;
  Start(FakeBus::Mode::kBootRom);
  bus_->polls_before_reenumeration = kPollsBeforeReenum;

  ASSERT_TRUE(Run({1, 2}));

  // Each of the two re-enumerations (to DFU and back to runtime) sleeps once
  // per failed poll.
  EXPECT_EQ(slept_, 2 * kPollsBeforeReenum * kReEnumPollInterval);
}

TEST_F(FlasherTest, SkipsDownloadModeSetupForAFlashImage) {
  // A Flash Image does not implement GET_STATUS/SET_STATUS: the mode query
  // tells the tool not to send them, and DFU_DETACH still gets it into DFU
  // mode.
  Start(FakeBus::Mode::kFlashImage);

  ASSERT_TRUE(Run({1, 2}));

  EXPECT_FALSE(Saw(Event::kGetStatusBootRom));
  EXPECT_FALSE(Saw(Event::kSetStatus));
  EXPECT_TRUE(Saw(Event::kDetach));
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2}));
}

TEST_F(FlasherTest, ProceedsWhenTheModeQueryStalls) {
  Start(FakeBus::Mode::kFlashImage);
  bus_->stall_mode_query = true;

  ASSERT_TRUE(Run({1, 2}));
  EXPECT_FALSE(Saw(Event::kSetStatus));
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2}));
}

TEST_F(FlasherTest, FlashesADeviceAlreadyInDfuModeWithoutDetachingFirst) {
  // A device left in DFU mode by an interrupted update.
  Start(FakeBus::Mode::kDfu);

  ASSERT_TRUE(Run({1, 2, 3}));

  EXPECT_FALSE(Saw(Event::kReadInitVtor));
  EXPECT_FALSE(Saw(Event::kGetStatusBootRom));
  EXPECT_FALSE(Saw(Event::kSetStatus));
  EXPECT_FALSE(Saw(Event::kReenumeratedAsDfu));
  // Its only DFU_DETACH is the one that boots the new image.
  EXPECT_EQ(std::ranges::count(bus_->dfu_requests, DfuRequest::kDetach), 1);
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_EQ(bus_->mode(), FakeBus::Mode::kFlashImage);
}

TEST_F(FlasherTest, FindsTheDfuDeviceWhenTheRuntimeDeviceIsAbsent) {
  Start(FakeBus::Mode::kDfu);

  ASSERT_TRUE(Run({1, 2}));

  // The runtime ID was tried and not found, and the DFU one was opened.
  EXPECT_TRUE(Saw(Event::kOpenedDfu));
}

TEST_F(FlasherTest, ReportsNoDeviceWhenNeitherIsPresent) {
  Start(FakeBus::Mode::kBootRom);
  bus_->Absent(kRts5817RuntimeId);

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
  EXPECT_TRUE(bus_->flashed.empty());
}

TEST_F(FlasherTest, ReportsAccessDeniedInsteadOfTryingTheDfuDevice) {
  // The runtime device exists but cannot be opened. That must not be taken as
  // "absent", which would send the tool looking for the DFU device.
  Start(FakeBus::Mode::kBootRom);
  bus_->Present(kRts5817DfuId);
  bus_->Unreadable(kRts5817RuntimeId);

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kAccessDenied);
  EXPECT_FALSE(Saw(Event::kOpenedDfu));
}

TEST_F(FlasherTest, UsesTheRequestedDeviceOnly) {
  Start(FakeBus::Mode::kBootRom);
  const UsbDeviceId other = {.vid = 0x1234, .pid = 0x5678};

  auto res = Run({1, 2}, other);

  // It is not on the bus, and the defaults are not tried in its place.
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kDeviceNotFound);
  EXPECT_FALSE(Saw(Event::kOpenedRuntime));
  EXPECT_FALSE(Saw(Event::kOpenedDfu));
}

TEST_F(FlasherTest, FlashesTheRequestedDfuDeviceDirectly) {
  Start(FakeBus::Mode::kDfu);

  ASSERT_TRUE(Run({1, 2}, kRts5817DfuId));

  EXPECT_FALSE(Saw(Event::kReadInitVtor));
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2}));
}

TEST_F(FlasherTest, StopsWhenTheDeviceRejectsTheImage) {
  Start(FakeBus::Mode::kBootRom);
  bus_->reject_image = true;

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
  // The only DFU_DETACH is the one that entered DFU mode; the device is not
  // told to boot an image it refused, and it never comes back in runtime mode.
  EXPECT_EQ(std::ranges::count(bus_->dfu_requests, DfuRequest::kDetach), 1);
  EXPECT_FALSE(Saw(Event::kReenumeratedAsRuntime));
  EXPECT_EQ(bus_->mode(), FakeBus::Mode::kDfu);
}

TEST_F(FlasherTest, FailsWhenTheDfuDeviceNeverReappears) {
  Start(FakeBus::Mode::kBootRom);
  bus_->polls_before_reenumeration = kMaxReEnumPolls;

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kTimeout);
  EXPECT_EQ(slept_, kReEnumTimeout);
  EXPECT_TRUE(bus_->flashed.empty());
}

TEST_F(FlasherTest, AcceptsTheDfuDeviceOnTheLastPoll) {
  Start(FakeBus::Mode::kBootRom);
  bus_->polls_before_reenumeration = kMaxReEnumPolls - 1;

  ASSERT_TRUE(Run({1, 2}));
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2}));
}

TEST_F(FlasherTest, FailsWhenTheRuntimeDeviceNeverComesBack) {
  Start(FakeBus::Mode::kDfu);
  bus_->polls_before_reenumeration = kMaxReEnumPolls;

  auto res = Run({1, 2});

  // The image went in, but the tool cannot confirm the device booted it.
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kTimeout);
  EXPECT_EQ(slept_, kReEnumTimeout);
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2}));
}

TEST_F(FlasherTest, AcceptsTheRuntimeDeviceOnTheLastPoll) {
  Start(FakeBus::Mode::kDfu);
  bus_->polls_before_reenumeration = kMaxReEnumPolls - 1;

  ASSERT_TRUE(Run({1, 2}));
  EXPECT_EQ(bus_->mode(), FakeBus::Mode::kFlashImage);
}

TEST_F(FlasherTest, StopsWhenEnablingDownloadModeFails) {
  Start(FakeBus::Mode::kBootRom);
  bus_->fail_get_status = true;

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
  EXPECT_FALSE(Saw(Event::kDetach));
  EXPECT_TRUE(bus_->flashed.empty());
}

TEST_F(FlasherTest, StopsWhenDetachFails) {
  Start(FakeBus::Mode::kBootRom);
  bus_->fail_detach = true;

  auto res = Run({1, 2});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kStall);
  EXPECT_FALSE(Saw(Event::kReenumeratedAsDfu));
  EXPECT_TRUE(bus_->flashed.empty());
}

TEST_F(FlasherTest, FlashesOnlyThePayloadOfAnImageWithADfuSuffix) {
  Start(FakeBus::Mode::kBootRom);
  std::vector<uint8_t> image = {1, 2, 3, 4, 5};

  image.insert(image.end(), kDfuSuffixBytes.begin(), kDfuSuffixBytes.end());

  ASSERT_TRUE(Run(image));
  EXPECT_EQ(bus_->flashed, (std::vector<uint8_t>{1, 2, 3, 4, 5}));
}

TEST_F(FlasherTest, RejectsAnEmptyImageBeforeTouchingTheBus) {
  Start(FakeBus::Mode::kBootRom);

  auto res = Run({});

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
  EXPECT_TRUE(bus_->events.empty());
}

TEST_F(FlasherTest, RejectsAnImageThatIsOnlyADfuSuffix) {
  Start(FakeBus::Mode::kBootRom);

  auto res = Run(kDfuSuffixBytes);

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), RtsError::kInvalidParameter);
  EXPECT_TRUE(bus_->events.empty());
}

}  // namespace
}  // namespace realtekfp
