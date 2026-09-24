/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CHRE_PLATFORM_SHARED_BT_HCI_REQUEST_H
#define CHRE_PLATFORM_SHARED_BT_HCI_REQUEST_H

#include <cinttypes>
#include <cstddef>

#include "chre/util/non_copyable.h"

namespace chre {

/**
 * Used to track a BT client's HCI request.
 */
// Role in bt_debug_collector.h should be updated with any changes to this list.
// LINT.IfChange(BtClientType)
enum class BtClientType : uint8_t {
  //! Used to track the Host's HCI requests.
  HOST,

  //! Used to track the CHRE SCANNER client's HCI requests.
  CHRE_SCANNER,

  //! Used  to track the CHRE RSSI Read client's HCI requests.
  CHRE_RSSI,

#ifdef CHRE_ENABLE_SNIFF_OFFLOAD
  //! Manages sniff mode requests for ACL connections
  CHRE_SNIFF_MANAGER,
#endif  // CHRE_ENABLE_SNIFF_OFFLOAD

#ifdef CHRE_ENABLE_BT_TEST_CLIENT
  //! Used to test CHRE BT via the CLI
  // Note, CHRE_TEST should come last so it doesn't change the values of the
  // enumerators above it (which are used in chre_parser.py).
  CHRE_TEST,
#endif  // CHRE_ENABLE_BT_TEST_CLIENT

#ifdef CHRE_BQR_OFFLOAD_ENABLED
  //!  Manages bqr offload events
  CHRE_BQR_OFFLOAD,
#endif  // #ifdef CHRE_BQR_OFFLOAD_ENABLED

  NUM_CLIENTS,
};
// LINT.ThenChange(bt_debug_collector.h:Role)

static constexpr uint8_t kMaxBtClients =
    static_cast<uint8_t>(BtClientType::NUM_CLIENTS);

/**
 * BT request expecting a response from the BT Controller.
 */
class BtHciRequest : public chre::NonCopyable {
 public:
  BtHciRequest() = delete;

  BtHciRequest(BtClientType client, uint16_t opCode)
      : mClient(client), mOpCode(opCode) {}

  BtClientType getClient() const {
    return mClient;
  }

  uint16_t getOpCode() const {
    return mOpCode;
  }

 private:
  //! Requesting client.
  BtClientType mClient;

  //! Request op code.
  uint16_t mOpCode;
};

}  // namespace chre

#endif  // CHRE_PLATFORM_SHARED_BT_HCI_REQUEST_H
