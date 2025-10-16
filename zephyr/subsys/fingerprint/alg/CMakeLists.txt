# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FP_EGIS_EGIS630_ALGORITHM egis_egis630_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_ELAN_ELAN80SG_ALGORITHM elan_elan80sg_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_ELAN_ELANI80SA_ALGORITHM elan_elani80sa_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_BEP_ALGORITHM fpc_bep_bio_alg.c)
zephyr_library_sources_ifdef(CONFIG_FP_FPC_LIBFP_ALGORITHM fpc_libfp_bio_alg.c)
