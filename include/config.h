/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Configuration file for Chrome EC.
 *
 * This file should not be included directly. Use config.h instead.
 */

#ifndef __CROS_EC_CONFIG_H
#define __CROS_EC_CONFIG_H

/*
 * The following section contains the default values for all the configuration
 * options. If you want to change a value, you should do it in the board
 * configuration file.
 */

/*
 * The following section contains the configuration options that are not
 * specific to any board.
 */

#include "config_chip.h"
#include "board.h"

/* Default hcdebug mode, e.g. HCDEBUG_OFF or HCDEBUG_NORMAL */
#ifndef CONFIG_HCDEBUG_MODE
#define CONFIG_HCDEBUG_MODE HCDEBUG_NORMAL
#endif

/* Enable task profiling auto-reset by default */
#define CONFIG_TASK_PROFILING_AUTO_RESET
#define CONFIG_TASK_PROFILING_AUTO_RESET_PERIOD_MS 2000

#endif /* __CROS_EC_CONFIG_H */
