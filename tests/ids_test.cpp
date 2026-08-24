// SPDX-License-Identifier: Apache-2.0

#include "cogito/ids.hpp"

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

bool IsLowerHex(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

void RequireUuidV4(const std::string& uuid) {
  REQUIRE(uuid.size() == 36U);
  REQUIRE(uuid[8] == '-');
  REQUIRE(uuid[13] == '-');
  REQUIRE(uuid[18] == '-');
  REQUIRE(uuid[23] == '-');
  for (std::size_t i = 0; i < uuid.size(); ++i) {
    if (i != 8U && i != 13U && i != 18U && i != 23U) {
      REQUIRE(IsLowerHex(uuid[i]));
    }
  }
  REQUIRE(uuid[14] == '4');
  REQUIRE((uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b'));
}

}  // namespace

TEST_CASE("Digest hex encoding is lowercase and strict", "[ids][digest]") {
  cogito::Digest digest;
  REQUIRE(digest.is_zero());
  for (std::size_t i = 0; i < digest.bytes.size(); ++i) {
    digest.bytes[i] = static_cast<std::uint8_t>(i);
  }
  REQUIRE_FALSE(digest.is_zero());
  REQUIRE(digest.hex() ==
          "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

  auto decoded = cogito::Digest::FromHex(digest.hex());
  REQUIRE(decoded.ok());
  REQUIRE(decoded.value() == digest);

  auto uppercase = cogito::Digest::FromHex(
      "000102030405060708090A0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  REQUIRE_FALSE(uppercase.ok());
  REQUIRE(uppercase.error().code == cogito::Errc::InvalidArgument);

  auto non_hex = cogito::Digest::FromHex(std::string(63U, '0') + "g");
  REQUIRE_FALSE(non_hex.ok());
  REQUIRE(non_hex.error().code == cogito::Errc::InvalidArgument);

  auto wrong_length = cogito::Digest::FromHex(std::string(62U, '0'));
  REQUIRE_FALSE(wrong_length.ok());
  REQUIRE(wrong_length.error().code == cogito::Errc::InvalidArgument);
}

TEST_CASE("UUID generation sets RFC 4122 version and variant bits", "[ids][uuid]") {
  auto first = cogito::NewUuidV4();
  auto second = cogito::NewUuidV4();
  REQUIRE(first.ok());
  REQUIRE(second.ok());
  RequireUuidV4(first.value());
  RequireUuidV4(second.value());
  REQUIRE(first.value() != second.value());
}

TEST_CASE("Process epoch initializes once and remains stable", "[ids][epoch]") {
  REQUIRE(cogito::ProcessEpochId().empty());
  REQUIRE(cogito::InitProcessEpoch().ok());
  const std::string epoch = cogito::ProcessEpochId();
  RequireUuidV4(epoch);
  REQUIRE(cogito::ProcessEpochId() == epoch);

  const cogito::Error second = cogito::InitProcessEpoch();
  REQUIRE_FALSE(second.ok());
  REQUIRE(second.code == cogito::Errc::Internal);
  REQUIRE(cogito::ProcessEpochId() == epoch);
}
