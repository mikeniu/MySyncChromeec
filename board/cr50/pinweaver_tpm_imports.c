/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pinweaver_tpm_imports.h>

#include <Global.h>

/* This would be included in util.h but naming collisions prevent that from
 * being used, so a forward declaration takes its place as a workaround.
 */
void *memcpy(void *dest, const void *src, size_t len);

void get_storage_seed(void *buf, size_t *len)
{
	*len = *len < sizeof(gp.SPSeed) ? *len : sizeof(gp.SPSeed);
	memcpy(buf, &gp.SPSeed, *len);
}
