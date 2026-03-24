<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger/chg_isl95522.h"
#include "emul/emul_common_i2c.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ISL95522_CHG_COMPAT

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger/chg_isl95522.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ISL95522_CHG_COMPAT

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
