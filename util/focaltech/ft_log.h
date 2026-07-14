/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_LOG_H_
#define UTIL_FOCALTECH_FT_LOG_H_

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <print>
#include <source_location>
#include <string_view>

namespace focaltech {

// ANSI terminal color escape sequences.
inline constexpr std::string_view kColorCyan = "\033[36m";
inline constexpr std::string_view kColorYellow = "\033[33m";
inline constexpr std::string_view kColorRed = "\033[31m";
inline constexpr std::string_view kColorReset = "\033[0m";

enum class LogLevel {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

namespace log {
constexpr LogLevel kCurrentLogLevel = LogLevel::kDebug;
inline constexpr std::string_view kLogTag = "focaltech";

// Evaluate and cache whether stdout is attached to a terminal
inline bool UseColor() {
#ifdef FT_LOG_COLOR_EN
  static const bool use_color = isatty(fileno(stdout));
  return use_color;
#else
  return false;
#endif
}

}  // namespace log

template <typename... Args>
void FtLog(LogLevel level, const std::source_location& loc,
           std::format_string<Args...> fmt, Args&&... args) {
  if (level < log::kCurrentLogLevel) {
    return;
  }

  const auto now = std::chrono::system_clock::now();
  std::string_view level_str;
  std::string_view color;
  const bool enable_color = log::UseColor();

  switch (level) {
    case LogLevel::kVerbose:
      level_str = "V";
      break;
    case LogLevel::kDebug:
      level_str = "D";
      break;
    case LogLevel::kInfo:
      level_str = "I";
      if (enable_color) color = kColorCyan;
      break;
    case LogLevel::kWarning:
      level_str = "W";
      if (enable_color) color = kColorYellow;
      break;
    case LogLevel::kError:
      level_str = "E";
      if (enable_color) color = kColorRed;
      break;
  }

  // Extract filename from the path.
  std::string_view file = loc.file_name();
  if (auto pos = file.find_last_of('/'); pos != std::string_view::npos) {
    file = file.substr(pos + 1);
  }

  std::print("{}[{:%m-%d %H:%M:%S}] {} [{}][{}:{}] ", color, now, level_str,
             log::kLogTag, file, loc.line());

  std::println(fmt, std::forward<Args>(args)...);

  if (!color.empty()) {
    std::print("{}", kColorReset);
  }
}

}  // namespace focaltech

#ifndef FT_LOG_DIS

// Convenience macros for standardizing call sites.
#define FT_LOGE(fmt, ...)                           \
  ::focaltech::FtLog(::focaltech::LogLevel::kError, \
                     std::source_location::current(), fmt, ##__VA_ARGS__)
#define FT_LOGW(fmt, ...)                             \
  ::focaltech::FtLog(::focaltech::LogLevel::kWarning, \
                     std::source_location::current(), fmt, ##__VA_ARGS__)
#define FT_LOGI(fmt, ...)                          \
  ::focaltech::FtLog(::focaltech::LogLevel::kInfo, \
                     std::source_location::current(), fmt, ##__VA_ARGS__)
#define FT_LOGD(fmt, ...)                           \
  ::focaltech::FtLog(::focaltech::LogLevel::kDebug, \
                     std::source_location::current(), fmt, ##__VA_ARGS__)
#define FT_LOGV(fmt, ...)                             \
  ::focaltech::FtLog(::focaltech::LogLevel::kVerbose, \
                     std::source_location::current(), fmt, ##__VA_ARGS__)

#else  // FT_LOG_DIS

#define FT_LOGE(fmt, ...) \
  do {                    \
  } while (0)
#define FT_LOGW(fmt, ...) \
  do {                    \
  } while (0)
#define FT_LOGI(fmt, ...) \
  do {                    \
  } while (0)
#define FT_LOGD(fmt, ...) \
  do {                    \
  } while (0)
#define FT_LOGV(fmt, ...) \
  do {                    \
  } while (0)

#endif  // FT_LOG_DIS

#endif  // UTIL_FOCALTECH_FT_LOG_H_
