/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMPILER_H
#define __CROS_EC_COMPILER_H

/*
 * See https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
 */
#ifndef CONFIG_ZEPHYR
/* If building with Zephyr, use its GCC version. */
#define GCC_VERSION \
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif /* !CONFIG_ZEPHYR */

/*
 * The EC codebase assumes that typeof() is available but it is not in Zephyr.
 * We use an #ifdef since arch/arm/include/aarch32/cortex_m/cmse.h defines this
 * macro.
 */
#ifndef typeof
#define typeof(x) __typeof__(x)
#endif

/**
 * ISO C forbids forward references to enum types, but gcc allows it as long as
 * the "-pedantic" flag is not used.
 *
 * In C++11 and newer, forward references to enums are only allowed if the
 * underlying type is specified.
 *
 * New uses of this macro are strongly discouraged; instead of forward
 * declaring the enum, provide the definition.
 *
 * TODO(http://b/187105190): Remove uses of this macro.
 */
#ifdef __cplusplus
#define FORWARD_DECLARE_ENUM(x) enum x : int
#else
#define FORWARD_DECLARE_ENUM(x) enum x
#endif /* __cplusplus */

#define DO_PRAGMA(x) _Pragma(#x)

#define _DISABLE_COMPILER_WARNING(compiler, warning) \
	DO_PRAGMA(compiler diagnostic push);         \
	DO_PRAGMA(compiler diagnostic ignored warning);

#define _ENABLE_COMPILER_WARNING(compiler, warning) \
	DO_PRAGMA(compiler diagnostic pop);

/**
 * Disable the specified compiler warning.
 */
#define DISABLE_COMPILER_WARNING(warning) \
	_DISABLE_COMPILER_WARNING(GCC, warning)

/**
 * Re-enable the specified compiler warning. Can only be used after a call to
 * DISABLE_COMPILER_WARNING.
 */
#define ENABLE_COMPILER_WARNING(warning) _ENABLE_COMPILER_WARNING(GCC, warning)

#ifdef __clang__
#define DISABLE_CLANG_WARNING(warning) _DISABLE_COMPILER_WARNING(clang, warning)
#define ENABLE_CLANG_WARNING(warning) _ENABLE_COMPILER_WARNING(clang, warning)
#else
#define DISABLE_CLANG_WARNING(warning)
#define ENABLE_CLANG_WARNING(warning)
#endif

#ifdef __GNUC__
#define DISABLE_GCC_WARNING(warning) _DISABLE_COMPILER_WARNING(GCC, warning)
#define ENABLE_GCC_WARNING(warning) _ENABLE_COMPILER_WARNING(GCC, warning)
#else
#define DISABLE_GCC_WARNING(warning)
#define ENABLE_GCC_WARNING(warning)
#endif

#endif /* __CROS_EC_COMPILER_H */
