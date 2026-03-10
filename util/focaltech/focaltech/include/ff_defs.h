/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __FF_DEFINES_H__
#define __FF_DEFINES_H__

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

/*
 * Convenient macro to convert buffer to special format.
 */
#define TYPE_OF(type, buf) (type *)(buf)

/* Unused variables */
#define UNUSED_VAR(v) ((void)v)

#endif /* __FF_DEFINES_H__ */
