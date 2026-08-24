// SPDX-License-Identifier: Apache-2.0

#include "cogito/canonical_json.hpp"

#include <array>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

using cogito::Errc;
using cogito::ccj::Json;

struct GoldenVector {
  const char* input;
  const char* expected;
};

constexpr std::array<GoldenVector, 24> kGoldenVectors{{
    {R"json({"b":1,"a":2})json", R"json({"a":2,"b":1})json"},
    {R"json({"a":{"z":1,"y":2}})json", R"json({"a":{"y":2,"z":1}})json"},
    {R"json({"A":1,"a":2})json", R"json({"A":1,"a":2})json"},
    {"0.0", "0"},
    {"-0.0", "0"},
    {"1.0", "1"},
    {"-7.0", "-7"},
    {"9007199254740992.0", "9007199254740992"},
    {"9007199254740994.0", "9007199254740994"},
    {"0.1", "0.1"},
    {"0.7", "0.7"},
    {"0.70000000000000007", "0.7000000000000001"},
    {"1e-7", "1e-07"},
    {"1.5e300", "1.5e+300"},
    {R"json("a\"b")json", R"json("a\"b")json"},
    {R"json("a\\b")json", R"json("a\\b")json"},
    {R"json("\u0000")json", R"json("\u0000")json"},
    {R"json("\u001F")json", R"json("\u001f")json"},
    {R"json("\n")json", R"json("\n")json"},
    {u8R"json("한글")json", u8R"json("한글")json"},
    {u8R"json("😀")json", u8R"json("😀")json"},
    {"[]", "[]"},
    {"{}", "{}"},
    {R"json([1,{"b":[2,3],"a":null}])json",
     R"json([1,{"a":null,"b":[2,3]}])json"},
}};

std::uint64_t DoubleBits(double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

std::uint64_t SplitMix64(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

class NumericLocaleGuard {
 public:
  NumericLocaleGuard() {
    const char* current = std::setlocale(LC_NUMERIC, nullptr);
    if (current != nullptr) {
      saved_ = current;
    }
  }

  ~NumericLocaleGuard() {
    if (!saved_.empty()) {
      (void)std::setlocale(LC_NUMERIC, saved_.c_str());
    }
  }

  NumericLocaleGuard(const NumericLocaleGuard&) = delete;
  NumericLocaleGuard& operator=(const NumericLocaleGuard&) = delete;

 private:
  std::string saved_;
};

}  // namespace

TEST_CASE("CCJ matches all 24 normative golden vectors", "[canonical][golden]") {
  for (std::size_t i = 0; i < kGoldenVectors.size(); ++i) {
    INFO("golden vector index: " << i);
    auto parsed = cogito::ccj::ParseStrict(kGoldenVectors[i].input);
    REQUIRE(parsed.ok());
    auto serialized = cogito::ccj::Serialize(parsed.value());
    REQUIRE(serialized.ok());
    REQUIRE(serialized.value() == kGoldenVectors[i].expected);
  }
  REQUIRE(cogito::ccj::SelfTest().ok());
}

TEST_CASE("ParseStrict rejects duplicate keys and malformed input", "[canonical][negative]") {
  auto duplicate = cogito::ccj::ParseStrict(R"json({"a":1,"a":2})json");
  REQUIRE_FALSE(duplicate.ok());
  REQUIRE(duplicate.error().code == Errc::DuplicateKey);
  REQUIRE(duplicate.error().reason_code == cogito::reason::kInputDuplicateKey);

  auto malformed = cogito::ccj::ParseStrict(R"json({"a":})json");
  REQUIRE_FALSE(malformed.ok());
  REQUIRE(malformed.error().code == Errc::InvalidArgument);
}

TEST_CASE("ParseStrict enforces UTF-8, byte, depth, and per-object key limits",
          "[canonical][limits]") {
  const std::array<char, 4> invalid_utf8{{'"', static_cast<char>(0xC0),
                                          static_cast<char>(0xAF), '"'}};
  auto invalid = cogito::ccj::ParseStrict(
      std::string_view(invalid_utf8.data(), invalid_utf8.size()));
  REQUIRE_FALSE(invalid.ok());
  REQUIRE(invalid.error().code == Errc::NotUtf8);

  cogito::ccj::ParseLimits limits;
  limits.max_bytes = 1;
  auto too_many_bytes = cogito::ccj::ParseStrict("{}", limits);
  REQUIRE_FALSE(too_many_bytes.ok());
  REQUIRE(too_many_bytes.error().code == Errc::TooLarge);

  limits = {};
  limits.max_depth = 2;
  REQUIRE(cogito::ccj::ParseStrict("[[]]", limits).ok());
  auto too_deep = cogito::ccj::ParseStrict("[[[]]]", limits);
  REQUIRE_FALSE(too_deep.ok());
  REQUIRE(too_deep.error().code == Errc::DepthExceeded);

  std::string adversarial_depth(10'000, '[');
  adversarial_depth.append(10'000, ']');
  auto adversarial = cogito::ccj::ParseStrict(adversarial_depth);
  REQUIRE_FALSE(adversarial.ok());
  REQUIRE(adversarial.error().code == Errc::DepthExceeded);

  limits = {};
  limits.max_keys = 1;
  REQUIRE(cogito::ccj::ParseStrict(R"json({"outer":{"inner":1}})json", limits).ok());
  auto too_many_keys = cogito::ccj::ParseStrict(R"json({"a":1,"b":2})json", limits);
  REQUIRE_FALSE(too_many_keys.ok());
  REQUIRE(too_many_keys.error().code == Errc::TooLarge);
}

TEST_CASE("CCJ sorts object keys by UTF-16 code units", "[canonical][unicode]") {
  Json value = Json::object();
  value[u8"\uE000"] = 3;
  value[u8"😀"] = 2;
  value[u8"€"] = 1;

  auto serialized = cogito::ccj::Serialize(value);
  REQUIRE(serialized.ok());
  REQUIRE(serialized.value() == u8R"json({"€":1,"😀":2,"":3})json");
}

TEST_CASE("CCJ preserves valid non-NFC UTF-8 and rejects invalid in-memory strings",
          "[canonical][unicode]") {
  const std::string decomposed = u8"e\u0301";
  auto serialized = cogito::ccj::Serialize(Json(decomposed));
  REQUIRE(serialized.ok());
  REQUIRE(serialized.value() == std::string{"\""} + decomposed + "\"");

  std::string invalid_utf8;
  invalid_utf8.push_back(static_cast<char>(0xED));
  invalid_utf8.push_back(static_cast<char>(0xA0));
  invalid_utf8.push_back(static_cast<char>(0x80));
  auto invalid = cogito::ccj::Serialize(Json(invalid_utf8));
  REQUIRE_FALSE(invalid.ok());
  REQUIRE(invalid.error().code == Errc::NotUtf8);
}

TEST_CASE("CCJ rejects non-finite numbers and enforces serialization limits",
          "[canonical][limits]") {
  auto nan = cogito::ccj::Serialize(Json(std::numeric_limits<double>::quiet_NaN()));
  REQUIRE_FALSE(nan.ok());
  REQUIRE(nan.error().code == Errc::InvalidArgument);

  auto infinity = cogito::ccj::Serialize(Json(std::numeric_limits<double>::infinity()));
  REQUIRE_FALSE(infinity.ok());
  REQUIRE(infinity.error().code == Errc::InvalidArgument);

  Json depth_64 = 0;
  for (std::size_t i = 0; i < 64U; ++i) {
    depth_64 = Json::array({std::move(depth_64)});
  }
  REQUIRE(cogito::ccj::Serialize(depth_64).ok());

  Json depth_65 = Json::array({std::move(depth_64)});
  auto too_deep = cogito::ccj::Serialize(depth_65);
  REQUIRE_FALSE(too_deep.ok());
  REQUIRE(too_deep.error().code == Errc::DepthExceeded);

  const std::string oversized(16U * 1024U * 1024U, 'x');
  auto too_large = cogito::ccj::Serialize(Json(oversized));
  REQUIRE_FALSE(too_large.ok());
  REQUIRE(too_large.error().code == Errc::TooLarge);
}

TEST_CASE("CCJ C99 general formatting round-trips 200000 deterministic finite doubles",
          "[canonical][roundtrip]") {
  std::uint64_t state = 0xC06170C06170C061ULL;
  std::size_t verified = 0;
  while (verified < 200000U) {
    const std::uint64_t bits = SplitMix64(state);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value) || value == 0.0) {
      continue;
    }

    auto serialized = cogito::ccj::Serialize(Json(value));
    REQUIRE(serialized.ok());
    auto parsed = cogito::ccj::ParseStrict(serialized.value());
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().is_number());
    const double round_tripped = parsed.value().get<double>();
    INFO("serialized: " << serialized.value());
    REQUIRE(DoubleBits(round_tripped) == DoubleBits(value));
    ++verified;
  }
}

TEST_CASE("CCJ fixes numeric fast-path boundaries", "[canonical][boundary]") {
  struct Boundary {
    double value;
    const char* expected;
  };
  const std::array<Boundary, 4> boundaries{{
      {1e15, "1000000000000000"},
      {9007199254740992.0, "9007199254740992"},
      {9007199254740994.0, "9007199254740994"},
      {1e17, "1e+17"},
  }};

  for (const Boundary& boundary : boundaries) {
    auto serialized = cogito::ccj::Serialize(Json(boundary.value));
    REQUIRE(serialized.ok());
    REQUIRE(serialized.value() == boundary.expected);
  }
}

TEST_CASE("CCJ number bytes are invariant under C, German, and Korean locales",
          "[canonical][locale]") {
  NumericLocaleGuard restore_locale;
  const std::array<double, 3> values{{0.1, 1e-7, 1234.5}};
  std::array<std::string, 3> expected;

  REQUIRE(std::setlocale(LC_NUMERIC, "C") != nullptr);
  for (std::size_t i = 0; i < values.size(); ++i) {
    auto serialized = cogito::ccj::Serialize(Json(values[i]));
    REQUIRE(serialized.ok());
    expected[i] = serialized.value();
  }

  const auto verify_locale = [&](const char* locale_name) {
    if (std::setlocale(LC_NUMERIC, locale_name) == nullptr) {
      return false;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      auto serialized = cogito::ccj::Serialize(Json(values[i]));
      REQUIRE(serialized.ok());
      REQUIRE(serialized.value() == expected[i]);
    }
    return true;
  };

  const std::array<const char*, 3> german_names{{
      "de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252"}};
  const std::array<const char*, 3> korean_names{{
      "ko_KR.UTF-8", "ko_KR.utf8", "Korean_Korea.949"}};
  bool tested_german = false;
  for (const char* name : german_names) {
    if (verify_locale(name)) {
      tested_german = true;
      break;
    }
  }
  bool tested_korean = false;
  for (const char* name : korean_names) {
    if (verify_locale(name)) {
      tested_korean = true;
      break;
    }
  }

  REQUIRE(tested_german);
  REQUIRE(tested_korean);
}
