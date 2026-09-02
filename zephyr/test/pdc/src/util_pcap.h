/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_UTIL_PCAP_H_
#define PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_UTIL_PCAP_H_

#include <stdio.h>

/*
 * @brief Create a PCAP file called "rts.pcap" in the
 *        test output directory and return its FILE *.
 *
 * @return FILE * of PCAP file.
 */
FILE *pcap_open(void);

/*
 * @brief Append data to PCAP file. The data is encapsulated
 *        in a PCAP entry header which includes a timestamp
 *        and data length information.
 *
 * @param pl    Pointer to data to write.
 * @param pl_sz Size of data to write.
 */
void pcap_append(FILE *fp, const void *pl, size_t pl_sz);

#endif /* PLATFORM_EC_ZEPHYR_TEST_PDC_SRC_UTIL_PCAP_H_ */
