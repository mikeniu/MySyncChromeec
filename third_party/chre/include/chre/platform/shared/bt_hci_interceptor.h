/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "chre/platform/shared/bt_hci_request.h"

/**
 * Observes the HCI commands the BtArbiter is sending to the controller. Also
 * observes the corresponding HCI command response events (command
 * complete, command status, loopback) that the BtArbiter is receiving from the
 * controller.
 *
 * This class can be used to intercept a command or a command response event and
 * overtake the HCI stream. It can also be used passively to observe specific
 * commands or command response events.
 *
 * It is up to the container to initialize and register HCI interceptors in the
 * correct order so that they can see all the commands and events they are
 * expected to see.
 */

namespace chre {
class BtHciInterceptor {
 public:
  virtual ~BtHciInterceptor() = default;

  /**
   * Intercepts the HCI command.
   *
   * @param h4Buffer The H4 buffer containing the HCI command.
   * @param length Length of the provided buffer.
   * @param client BT HCI client sending the command
   *
   * @return true if the command was intercepted. It is up to the
   * BtHciInterceptor to send the command to the controller.
   *
   * @return false if the command was not intercepted. Continue checking the
   * command with the remaining interceptors.
   */
  virtual bool interceptCommand(const uint8_t *h4Buffer, size_t length,
                                BtClientType client) = 0;

  /**
   * Intercepts the HCI command response event.
   *
   * @param hciBuffer The HCI buffer containing the HCI event.
   * @param length Length of the provided buffer.
   *
   * @return true if the command response event was intercepted. It is up to the
   * BtHciInterceptor to send the event to the host if intended. This will also
   * leave the current HCI command at the front of the BtArbiter's
   * mQueuedRequests. It is up to the BtHciInterceptor to ensure a subsequent
   * HCI command response event pops the request from the queue.
   *
   * @return false if the command response event was not intercepted. Continue
   * checking the command response event with the remaining interceptors.
   */
  virtual bool interceptCommandResponseEvent(const uint8_t *h4Buffer,
                                             size_t length) = 0;

  /**
   * Notifies the BtHciInterceptor of a BT controller reset.
   */
  virtual void notifyReset() = 0;
};

}  // namespace chre