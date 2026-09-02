/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_LINKER_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_LINKER_H_

#ifdef CONFIG_FAKE_SHMEM
/* Define __shared_mem_buf for the fake shared memory which is used with
 * tests.
 */
#define __shared_mem_buf fake_shmem_buf
#else /* CONFIG_FAKE_SHMEM */
/* Put the start of shared memory after all allocated RAM symbols */
#define __shared_mem_buf _image_ram_end
#endif /* CONFIG_FAKE_SHMEM */

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_LINKER_H_ */
