/**
 * ${ANDROID_BUILD_TOP}/vendor/focaltech/source/base/focaltech/ff_defs.h
 *
 * Copyright (C) 2017-2025 FocalTech Systems Co., Ltd. All Rights Reserved.
 *
 * Initial Author: James.Lee <lijing@focaltech-electronics.com>
 *
 **/

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
