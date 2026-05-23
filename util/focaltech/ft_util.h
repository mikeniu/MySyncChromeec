<<<<<<< PATCH SET (1453e788f70905e1537b7037445d9018459c2ba0 Convert the code to C++.  Remove USB suspend/resume function)
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_UTIL_H_
#define UTIL_FOCALTECH_FT_UTIL_H_

#include <cassert>
#include <string_view>

template <typename T>
constexpr T FtDivRoundUp(T n, T d) {
  assert(d != 0);
  return (n + d - 1) / d;
}

enum class FtError {
  kSuccess = 0,
  kNoSuchDevice = -1,
  kParameterError = -2,
  kNoSuchFile = -3,
  kResultFail = -4,
  kInvalidMode = -5,
};

inline std::string_view ToString(FtError err) {
  switch (err) {
    case FtError::kSuccess:
      return "Success";
    case FtError::kNoSuchDevice:
      return "No such device";
    case FtError::kParameterError:
      return "Parameter error";
    case FtError::kNoSuchFile:
      return "No such file";
    case FtError::kResultFail:
      return "Result fail";
    case FtError::kInvalidMode:
      return "Invalid mode";
    default:
      return "Unknown error";
  }
}

#endif  // UTIL_FOCALTECH_FT_UTIL_H_
||||||| BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
=======
#ifndef UTIL_FOCALTECH_FT_UTIL_H_
#define UTIL_FOCALTECH_FT_UTIL_H_

#include <cassert>

template <typename T>
constexpr T FtDivRoundUp(T n, T d) {
  assert(d != 0);
  return (n + d - 1) / d;
}

#endif  // UTIL_FOCALTECH_FT_UTIL_H_
>>>>>>> BASE      (aea89f2ea6978aa9a7fe54dd61b110e76f30dea8 util: focaltech: Add focaltech IAP tool)
