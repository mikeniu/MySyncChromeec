<<<<<<< PATCH SET (1453e788f70905e1537b7037445d9018459c2ba0 Convert the code to C++.  Remove USB suspend/resume function)
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_LOG_H_
#define UTIL_FOCALTECH_FT_LOG_H_

#include <chrono>
#include <print>
#include <source_location>
#include <string_view>

enum class LogLevel {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

constexpr LogLevel kCurrentLogLevel = LogLevel::kDebug;
constexpr const char* kLogTag = "focaltech";

template <typename... Args>
void FtLog(LogLevel level, const std::source_location& loc,
           std::format_string<Args...> fmt, Args&&... args) {
  if (level < kCurrentLogLevel) {
    return;
  }

  auto now = std::chrono::system_clock::now();
  std::string_view level_str;
  std::string_view color;
  std::string_view reset = "\033[0m";

  switch (level) {
    case LogLevel::kVerbose:
      level_str = "V";
      color = "";
      break;
    case LogLevel::kDebug:
      level_str = "D";
      color = "";
      break;
    case LogLevel::kInfo:
      level_str = "I";
      color = "\033[36m";
      break;
    case LogLevel::kWarning:
      level_str = "W";
      color = "\033[33m";
      break;
    case LogLevel::kError:
      level_str = "E";
      color = "\033[31m";
      break;
  }

  // Extract filename from the path.
  std::string_view file = loc.file_name();
  if (auto pos = file.find_last_of('/'); pos != std::string_view::npos) {
    file = file.substr(pos + 1);
  }

  std::print("{}[{:%m-%d %H:%M:%S}] {} [{}][{}:{}] ", color, now, level_str,
             kLogTag, file, loc.line());
  std::println(fmt, std::forward<Args>(args)...);

  if (!color.empty()) {
    std::print("{}", reset);
  }
}

// Convenience macros for standardizing call sites.
#define LOGE(fmt, ...) \
  FtLog(LogLevel::kError, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) \
  FtLog(LogLevel::kWarning, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) \
  FtLog(LogLevel::kInfo, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) \
  FtLog(LogLevel::kDebug, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGV(fmt, ...) \
  FtLog(LogLevel::kVerbose, std::source_location::current(), fmt, ##__VA_ARGS__)

#endif  // UTIL_FOCALTECH_FT_LOG_H_
||||||| BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
=======
#ifndef UTIL_FOCALTECH_FT_LOG_H_
#define UTIL_FOCALTECH_FT_LOG_H_

#include <chrono>
#include <print>
#include <source_location>
#include <string_view>

enum class LogLevel {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

constexpr LogLevel kCurrentLogLevel = LogLevel::kDebug;

template <typename... Args>
void FtLog(LogLevel level, const std::source_location& loc,
           std::format_string<Args...> fmt, Args&&... args) {
  if (level < kCurrentLogLevel) {
    return;
  }

  auto now = std::chrono::system_clock::now();
  std::string_view level_str;
  std::string_view color;
  std::string_view reset = "\033[0m";

  switch (level) {
    case LogLevel::kVerbose:
      level_str = "V";
      color = "";
      break;
    case LogLevel::kDebug:
      level_str = "D";
      color = "";
      break;
    case LogLevel::kInfo:
      level_str = "I";
      color = "\033[36m";
      break;
    case LogLevel::kWarning:
      level_str = "W";
      color = "\033[33m";
      break;
    case LogLevel::kError:
      level_str = "E";
      color = "\033[31m";
      break;
  }

  // Extract filename from the path.
  std::string_view file = loc.file_name();
  if (auto pos = file.find_last_of('/'); pos != std::string_view::npos) {
    file = file.substr(pos + 1);
  }

  std::print("{}[{:%m-%d %H:%M:%S}] {} [{}:{}] ", color, now, level_str, file,
             loc.line());
  std::println(fmt, std::forward<Args>(args)...);

  if (!color.empty()) {
    std::print("{}", reset);
  }
}

// Convenience macros for standardizing call sites.
#define LOGE(fmt, ...) \
  FtLog(LogLevel::kError, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) \
  FtLog(LogLevel::kWarning, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) \
  FtLog(LogLevel::kInfo, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) \
  FtLog(LogLevel::kDebug, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOGV(fmt, ...) \
  FtLog(LogLevel::kVerbose, std::source_location::current(), fmt, ##__VA_ARGS__)

#endif  // UTIL_FOCALTECH_FT_LOG_H_
>>>>>>> BASE      (aea89f2ea6978aa9a7fe54dd61b110e76f30dea8 util: focaltech: Add focaltech IAP tool)
