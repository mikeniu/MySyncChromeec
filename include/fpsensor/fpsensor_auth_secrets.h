/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint authentication secrets */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_AUTH_SECRETS_H
#define __CROS_EC_FPSENSOR_FPSENSOR_AUTH_SECRETS_H

#include "ec_commands.h"

#include <array>

inline std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_AUTH_SECRETS_H */
