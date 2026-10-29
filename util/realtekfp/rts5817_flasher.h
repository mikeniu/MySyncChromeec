/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// The flashing sequence of the RTS5817 tool, apart from the command line.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASHER_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASHER_H_

#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "rts5817_usb_backend.h"
#include "rts5817_usb_device.h"

namespace realtekfp {

// USB IDs of the RTS5817: the runtime mode, and the DFU mode it detaches into.
inline constexpr UsbDeviceId kRts5817RuntimeId = {.vid = 0x0BDA, .pid = 0x5817};
inline constexpr UsbDeviceId kRts5817DfuId = {.vid = 0x0BDA, .pid = 0x5800};

// Flashes `firmware`, which must not carry a DFU suffix, to the device.
//
// With `requested` set that device is used as given. Otherwise the runtime
// device is tried first and then the DFU device, which is what a device left
// in DFU mode by an interrupted update enumerates as. A device already in DFU
// mode is flashed directly instead of being detached again, and the function
// returns once the device is back in runtime mode with the new image.
std::expected<void, RtsError> Flash(RtsUsbDevice& device,
                                    const std::optional<UsbDeviceId>& requested,
                                    std::span<const uint8_t> firmware);

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASHER_H_
