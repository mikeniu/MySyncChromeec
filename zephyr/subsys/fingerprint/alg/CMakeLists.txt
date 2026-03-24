<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FP_EGIS_EGIS630_ALGORITHM egis_egis630_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_ELAN_ELAN80SERIES_ALGORITHM elan_elan80series_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_BEP_ALGORITHM fpc_bep_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_LIBFP_ALGORITHM fpc_libfp_bio_alg.c)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FP_EGIS_EGIS630_ALGORITHM egis_egis630_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_ELAN_ELAN80SG_ALGORITHM elan_elan80sg_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_BEP_ALGORITHM fpc_bep_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_LIBFP_ALGORITHM fpc_libfp_bio_alg.c)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
