/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli_options.h"
#include "crypto_util.h"
#include "egis_util.h"

namespace egis {
namespace {

// 1. Test egis_util.h (Endianness & Struct Serialization)
TEST(EgisUtilTest, EndianSwap) {
  uint32_t val = 0x12345678;
  uint32_t swapped = ToLittleEndian(val);
  // On little-endian systems (like the x86_64 host), ToLittleEndian is a no-op.
  EXPECT_EQ(swapped, val);
}

struct SampleStruct {
  uint16_t a;
  uint32_t b;
} __attribute__((packed));

TEST(EgisUtilTest, ReadWriteStruct) {
  std::array<uint8_t, 6> buffer{};
  SampleStruct in{.a = 0xAAAA, .b = 0xBBBBCCCC};

  auto write_res = WriteStruct<SampleStruct>(buffer, in);
  EXPECT_TRUE(write_res.has_value());

  auto read_res = ReadStruct<SampleStruct>(buffer);
  ASSERT_TRUE(read_res.has_value());
  EXPECT_EQ(read_res->a, 0xAAAA);
  EXPECT_EQ(read_res->b, 0xBBBBCCCC);
}

// 2. Test cli_options.h (CLI Argument Parsing)
TEST(CliOptionsTest, ParseHelpReturnsNullopt) {
  std::array<char *, 2> argv = {const_cast<char *>("et171_flash"),
                                const_cast<char *>("--help")};
  auto config = ParseArgs(argv.size(), argv.data());
  EXPECT_FALSE(config.has_value());
}

TEST(CliOptionsTest, ParseValidFlashFw) {
  std::array<char *, 3> argv = {const_cast<char *>("et171_flash"),
                                const_cast<char *>("--flashfw"),
                                const_cast<char *>("image.bin")};
  auto config = ParseArgs(argv.size(), argv.data());
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->mode, AppMode::kFlashFw);
  EXPECT_EQ(config->fw_path, "image.bin");
  EXPECT_EQ(config->algo_type, EgisCryptoType::kSha256);
}

// 3. Test crypto_util.h (BoringSSL / OpenSSL wrappers)
TEST(CryptoUtilTest, Sha256KnownValue) {
  std::string_view data = "abc";
  std::array<uint8_t, 32> hash{};
  const auto data_span =
      std::span(reinterpret_cast<const uint8_t *>(data.data()), data.size());
  crypto::Sha256(data_span, hash);

  // SHA-256 of "abc" is ba7816bf...
  EXPECT_EQ(hash[0], 0xba);
  EXPECT_EQ(hash[1], 0x78);
  EXPECT_EQ(hash[2], 0x16);
  EXPECT_EQ(hash[3], 0xbf);
}

}  // namespace
}  // namespace egis

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
