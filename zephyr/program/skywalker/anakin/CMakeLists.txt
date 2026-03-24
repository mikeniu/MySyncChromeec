<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Anakin Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/sensor.c" "src/board.c")
zephyr_library_sources("src/thermal.c")
zephyr_library_sources("src/charger.c")
zephyr_library_sources_ifdef(
    CONFIG_PDC_RUNTIME_PORT_CONFIG "src/usbc_runtime.c")
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Anakin Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/sensor.c" "src/board.c")
zephyr_library_sources("src/thermal.c")
zephyr_library_sources_ifdef(
    CONFIG_PDC_RUNTIME_PORT_CONFIG "src/usbc_runtime.c")
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
