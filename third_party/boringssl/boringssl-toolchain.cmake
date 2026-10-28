# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROS_EC_REPO CROSS_COMPILE CC_NAME CXX_NAME)
include("${CROS_EC_REPO}/cmake/toolchain-common.cmake")

# TODO(b/491765858): Remove once ChromeOS third_party/boringssl is uprevved to
# include upstream BoringSSL's C23 const-correct OPENSSL_memchr() for glibc
# 2.43's const-preserving string macros.
if ("${CC_NAME}" STREQUAL gcc)
add_compile_options(-Wno-error=discarded-qualifiers)
else()
add_compile_options(-Wno-error=incompatible-pointer-types-discards-qualifiers)
endif()

# Specify our platform, which disables filesystem, threads, etc.
add_definitions(-DCROS_EC)

# When compiling the code with the portage build system, it will generate very
# long file path strings. This compile options will strip the source path, and
# recude the final code size.
# The reason we don't use "-ffile-prefix-map" here is because we don't want to
# break the debug symbols for debugging. The flag needs to be passed to the
# preprocessor:
# https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html#index-fmacro-prefix-map
add_definitions(-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)
