// SPDX-License-Identifier: Apache-2.0

#include "cogito/clock.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

bool IsRfc3339UtcSeconds(const std::string& value) {
  if (value.size() != 20U || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
      value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
    return false;
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i == 4U || i == 7U || i == 10U || i == 13U || i == 16U || i == 19U) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(value[i])) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_CASE("SystemClock exposes RFC3339 UTC and a nondecreasing steady clock", "[clock]") {
  cogito::SystemClock clock;
  REQUIRE(IsRfc3339UtcSeconds(clock.NowUtcRfc3339()));
  const std::int64_t first = clock.MonotonicNs();
  const std::int64_t second = clock.MonotonicNs();
  REQUIRE(second >= first);
}

TEST_CASE("FakeClock keeps wall and monotonic time independently controllable", "[clock]") {
  cogito::FakeClock clock("2026-08-25T12:00:00Z", 100);
  REQUIRE(clock.NowUtcRfc3339() == "2026-08-25T12:00:00Z");
  REQUIRE(clock.MonotonicNs() == 100);

  clock.SetUtc("2026-08-24T00:00:00Z");
  REQUIRE(clock.NowUtcRfc3339() == "2026-08-24T00:00:00Z");
  REQUIRE(clock.MonotonicNs() == 100);

  clock.Advance(-1);
  clock.Advance(0);
  REQUIRE(clock.MonotonicNs() == 100);
  clock.Advance(25);
  REQUIRE(clock.MonotonicNs() == 125);

  clock.SetMonotonicNs(124);
  REQUIRE(clock.MonotonicNs() == 125);
  clock.SetMonotonicNs(125);
  clock.SetMonotonicNs(130);
  REQUIRE(clock.MonotonicNs() == 130);
}

TEST_CASE("FakeClock saturates positive monotonic overflow", "[clock][boundary]") {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  cogito::FakeClock clock("2026-08-25T00:00:00Z", kMax - 2);
  clock.Advance(2);
  REQUIRE(clock.MonotonicNs() == kMax);
  clock.Advance(1);
  REQUIRE(clock.MonotonicNs() == kMax);
}
