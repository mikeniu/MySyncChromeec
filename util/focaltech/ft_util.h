#ifndef UTIL_FOCALTECH_FT_UTIL_H_
#define UTIL_FOCALTECH_FT_UTIL_H_

#include <cassert>

template <typename T>
constexpr T FtDivRoundUp(T n, T d) {
  assert(d != 0);
  return (n + d - 1) / d;
}

#endif  // UTIL_FOCALTECH_FT_UTIL_H_