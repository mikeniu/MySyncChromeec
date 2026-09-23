/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_ECTOOL_PDC_PCAP_H_
#define PLATFORM_EC_UTIL_ECTOOL_PDC_PCAP_H_

#include <stdio.h>

#include <sys/time.h>

FILE *pdc_pcap_open(const char *file);
int pdc_pcap_append(FILE *fp, struct timeval tv, const void *pl, size_t pl_sz);
int pdc_pcap_close(FILE *fp);

#endif /* PLATFORM_EC_UTIL_ECTOOL_PDC_PCAP_H_ */
