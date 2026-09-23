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

#pragma once

#include <optional>

#include "pw_function/function.h"
#include "pw_span/span.h"
#include "pw_status/status.h"

namespace chre {

/**
 * Generic API for a transport between CHRE and the host.
 */
class HostTransport {
 public:
  /** Callback provided to user for responding to a transaction. */
  using RespondToHost =
      pw::InlineCallback<pw::Status(
                             pw::Status status,
                             std::optional<pw::span<const uint8_t>> data),
                         16 /* inline_target_size */>;

  /**
   * Callback that this instance invokes on an incoming message from the host.
   *
   * txnRespCb is only provided for messages expecting a response from CHRE. If
   * it was not invoked, the transport will send a default response based on the
   * status returned by the HandleHostMsg.
   */
  using HandleHostMsg = pw::InlineFunction<pw::Status(
      uint32_t type, pw::span<const uint8_t> data, RespondToHost &txnRespCb)>;

  virtual ~HostTransport() = default;

  /**
   * Initializes the transport and begins message handling.
   *
   * @param msgHandler The callback to invoke on incoming message.
   * @return pw::OkStatus() on success.
   */
  virtual pw::Status Start(HandleHostMsg &&msgHandler) = 0;

  /**
   * Sends a message to the host.
   *
   * Blocks the calling thread.
   *
   * @param type The type of the message.
   * @param data Span over the message body.
   * @param wakeUp Whether to wake the AP.
   * @return pw::OkStatus() on success.
   */
  virtual pw::Status Write(uint32_t type, pw::span<const uint8_t> data,
                           bool wakeUp) = 0;

 protected:
  HostTransport() = default;
};

}  // namespace chre
