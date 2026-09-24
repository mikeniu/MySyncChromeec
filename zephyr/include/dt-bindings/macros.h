/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_DT_BINDINGS_MACROS_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_DT_BINDINGS_MACROS_H_

/* Internal, recursive helpers to construct the integer from individual bits. */
#define _BINARY1(b0) (b0)
#define _BINARY2(b1, ...) (((b1) << 1) | _BINARY1(__VA_ARGS__))
#define _BINARY3(b2, ...) (((b2) << 2) | _BINARY2(__VA_ARGS__))
#define _BINARY4(b3, ...) (((b3) << 3) | _BINARY3(__VA_ARGS__))
#define _BINARY5(b4, ...) (((b4) << 4) | _BINARY4(__VA_ARGS__))
#define _BINARY6(b5, ...) (((b5) << 5) | _BINARY5(__VA_ARGS__))
#define _BINARY7(b6, ...) (((b6) << 6) | _BINARY6(__VA_ARGS__))
#define _BINARY8(b7, ...) (((b7) << 7) | _BINARY7(__VA_ARGS__))

/* Internal, a macro to count the number of arguments */
#define _NARG(...) _NARG_I(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _NARG_I(_8, _7, _6, _5, _4, _3, _2, _1, N, ...) N

/* Internal, a dispatcher that concatenates "_BINARY" with the argument count */
#define _BINARY_DISPATCH(N) _BINARY##N
#define _BINARY_DISPATCHER(N) _BINARY_DISPATCH(N)

/*
 * Create an integer constant from a binary representation.
 *
 * The Device Tree Compiler (dtc) lacks support for binary literals (e.g.,
 * 0b110). This macro is a helper to achieve this functionality. For example,
 * BINARY(1, 1, 0) is equivalent to 0b110, which is 6.
 */
#define BINARY(...) _BINARY_DISPATCHER(_NARG(__VA_ARGS__))(__VA_ARGS__)

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DT_BINDINGS_MACROS_H_ */
