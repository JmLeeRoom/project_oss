// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 시계 계약 (Clock, SystemClock, FakeClock)
//
// 규범 근거 : Cogito++_구현명세서.md §4-2(:408-443), 체크리스트 S1-03(:288-297)
#ifndef COGITO_CLOCK_HPP
#define COGITO_CLOCK_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Clock — 시간 인터페이스
//
// [시간 규약]
// - UTC 는 RFC 3339 형식(예: "2026-08-24T00:00:00Z") 문자열로만 노출되며, 표시/감사/상관관계 전용이다.
// - 만료(expiry), 제한시간(timeout), 지속시간(duration) 판단에는 오직 monotonic ns 만 사용한다.
// - wall clock 의 역행(NTP 스텝 등)은 TTL/타임아웃 판단에 절대 영향을 주지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
class Clock {
 public:
  virtual ~Clock() = default;
  virtual std::string  NowUtcRfc3339() const = 0;   // 표시·감사용 UTC (RFC 3339)
  virtual std::int64_t MonotonicNs()   const = 0;   // 순서·경과시간 전용 단조 증가 ns
};

// 운영용 실제 시스템 시계
class SystemClock final : public Clock {
 public:
  std::string  NowUtcRfc3339() const override;
  std::int64_t MonotonicNs()   const override;
};

// 테스트용 모의 시계 (시간 조작 및 시퀀스 주입)
class FakeClock final : public Clock {
 public:
  explicit FakeClock(std::string start_utc = "2026-08-24T00:00:00Z", std::int64_t start_ns = 1'000'000'000);
  std::string  NowUtcRfc3339() const override;
  std::int64_t MonotonicNs()   const override;

  // 단조 증가 진행 (ns <= 0 이면 무시, INT64_MAX 초과 시 INT64_MAX 로 포화)
  void Advance(std::int64_t ns);
  void SetUtc(std::string utc);                     // UTC 역행 테스트용 (벽시계 역행 주입 허용)
  // 단조 시간 설정 (ns < ns_ 이면 단조 비감소성 보장을 위해 역행하지 않고 현재값 유지)
  void SetMonotonicNs(std::int64_t ns);

 private:
  mutable std::string  utc_;
  mutable std::int64_t ns_;
};

}  // namespace cogito

#endif  // COGITO_CLOCK_HPP
