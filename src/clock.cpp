// SPDX-License-Identifier: Apache-2.0

#include "cogito/clock.hpp"

#include <chrono>
#include <ctime>
#include <limits>
#include <utility>

namespace cogito {
namespace {

bool ToUtc(std::time_t value, std::tm& utc) noexcept {
#if defined(_WIN32)
  return ::gmtime_s(&utc, &value) == 0;
#else
  return ::gmtime_r(&value, &utc) != nullptr;
#endif
}

}  // namespace

std::string SystemClock::NowUtcRfc3339() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  if (!ToUtc(seconds, utc)) {
    return {};
  }

  char buffer[21]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) != 20U) {
    return {};
  }
  return std::string(buffer, 20U);
}

std::int64_t SystemClock::MonotonicNs() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

FakeClock::FakeClock(std::string start_utc, std::int64_t start_ns)
    : utc_(std::move(start_utc)), ns_(start_ns) {}

std::string FakeClock::NowUtcRfc3339() const { return utc_; }

std::int64_t FakeClock::MonotonicNs() const { return ns_; }

void FakeClock::Advance(std::int64_t ns) {
  if (ns <= 0) {
    return;
  }
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  if (ns_ > kMax - ns) {
    ns_ = kMax;
    return;
  }
  ns_ += ns;
}

void FakeClock::SetUtc(std::string utc) { utc_ = std::move(utc); }

void FakeClock::SetMonotonicNs(std::int64_t ns) {
  if (ns >= ns_) {
    ns_ = ns;
  }
}

}  // namespace cogito
