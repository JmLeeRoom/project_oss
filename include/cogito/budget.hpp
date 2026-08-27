// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 턴 예산 및 자원 추적 (BudgetTracker, TurnBudget)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12, 체크리스트 S2-01
#ifndef COGITO_BUDGET_HPP
#define COGITO_BUDGET_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "cogito/result.hpp"

namespace cogito {

struct TurnBudget {
  std::size_t   max_prompt_tokens = 8192;             // 턴당 최대 프롬프트 토큰
  std::size_t   max_completion_tokens = 4096;         // 턴당 최대 생성 토큰
  std::size_t   max_total_tokens = 16384;             // 턴당 최대 총 토큰
  std::size_t   max_tool_calls = 10;                  // 턴당 최대 도구 호출 횟수
  std::size_t   max_total_output_bytes = 1024 * 1024; // 턴당 최대 총 출력 바이트 (1MB)
  std::int64_t  timeout_ns = 30'000'000'000LL;        // 턴 총 제한시간 (30s, monotonic ns)
};

// ─────────────────────────────────────────────────────────────────────────────
// BudgetTracker — 턴 내 추론 토큰, 도구 호출 수, 출력 바이트 및 타임아웃 추적
//
// [규약]
// - 모든 시간 비교는 monotonic_ns 기준이며 시작 시각(start_monotonic_ns_)에 timeout_ns 를 더해 deadline 산출
// - 추론 전 ReserveTokens 로 예약 후, 실제 수신 시 SettleTokens 로 정산 (S4-03)
// - 상한 초과 시 Errc::BudgetExhausted 및 적절한 reason code 반환
// ─────────────────────────────────────────────────────────────────────────────
class BudgetTracker {
 public:
  explicit BudgetTracker(const TurnBudget& budget = TurnBudget{})
      : budget_(budget) {}

  // 턴 시작 시점 기록 (monotonic ns)
  void StartTurn(std::int64_t start_monotonic_ns) noexcept {
    start_monotonic_ns_ = start_monotonic_ns;
    turn_started_ = true;
    if (budget_.timeout_ns <= 0) {
      deadline_ns_ = start_monotonic_ns;
    } else if (start_monotonic_ns > INT64_MAX - budget_.timeout_ns) {
      deadline_ns_ = INT64_MAX;
    } else {
      deadline_ns_ = start_monotonic_ns + budget_.timeout_ns;
    }
  }

  bool turn_started() const noexcept { return turn_started_; }
  std::int64_t start_monotonic_ns() const noexcept { return start_monotonic_ns_; }
  std::int64_t deadline_ns() const noexcept { return deadline_ns_; }

  // 마감시한 검사 (시작된 턴에서 데드라인 정각/이후이면 초과로 판정)
  bool IsDeadlineExceeded(std::int64_t now_ns) const noexcept {
    return turn_started_ && now_ns >= deadline_ns_;
  }

  std::int64_t remaining_ns(std::int64_t now_ns) const noexcept {
    if (!turn_started_ || now_ns >= deadline_ns_) {
      return 0;
    }
    if (now_ns < 0 && deadline_ns_ > INT64_MAX + now_ns) {
      return INT64_MAX;
    }
    return deadline_ns_ - now_ns;
  }

  Error CheckDeadline(std::int64_t now_ns) const {
    if (IsDeadlineExceeded(now_ns)) {
      return Error{Errc::BudgetExhausted, reason::kBudgetDeadline, "Turn deadline exceeded"};
    }
    return Error::Ok();
  }

  // ───────────────────────────────────────────────────────────────────────────
  // 토큰 예약 및 정산 (S4-03)
  // ───────────────────────────────────────────────────────────────────────────
  Error ReserveTokens(std::size_t prompt_tokens, std::size_t max_completion_tokens) {
    if (reservation_active_) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Token reservation already active in current turn"};
    }
    if (prompt_tokens > budget_.max_prompt_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Prompt tokens exceed turn max_prompt_tokens budget"};
    }
    if (max_completion_tokens > budget_.max_completion_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Max completion tokens exceed turn max_completion_tokens budget"};
    }
    if (prompt_tokens > SIZE_MAX - max_completion_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Token count overflow in reservation"};
    }
    const std::size_t requested_total = prompt_tokens + max_completion_tokens;
    if (requested_total > budget_.max_total_tokens ||
        total_tokens_used_ > budget_.max_total_tokens - requested_total) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Total token reservation exceeds turn max_total_tokens budget"};
    }

    reserved_tokens_ = requested_total;
    reservation_active_ = true;
    return Error::Ok();
  }

  Error SettleTokens(std::size_t actual_prompt_tokens, std::size_t actual_completion_tokens) {
    if (!reservation_active_) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "No active token reservation to settle"};
    }
    if (actual_prompt_tokens > budget_.max_prompt_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Actual prompt tokens exceed turn max_prompt_tokens budget"};
    }
    if (actual_completion_tokens > budget_.max_completion_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Actual completion tokens exceed turn max_completion_tokens budget"};
    }
    if (actual_prompt_tokens > SIZE_MAX - actual_completion_tokens) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Token count overflow in settlement"};
    }
    const std::size_t actual_total = actual_prompt_tokens + actual_completion_tokens;
    if (actual_total > budget_.max_total_tokens ||
        total_tokens_used_ > budget_.max_total_tokens - actual_total) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Actual total token usage exceeds turn max_total_tokens budget"};
    }
    if (prompt_tokens_used_ > SIZE_MAX - actual_prompt_tokens ||
        completion_tokens_used_ > SIZE_MAX - actual_completion_tokens ||
        total_tokens_used_ > SIZE_MAX - actual_total) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Token counter overflow in settlement"};
    }

    reserved_tokens_ = 0;
    reservation_active_ = false;
    prompt_tokens_used_ += actual_prompt_tokens;
    completion_tokens_used_ += actual_completion_tokens;
    total_tokens_used_ += actual_total;
    return Error::Ok();
  }

  void ReleaseReservation() noexcept {
    reserved_tokens_ = 0;
    reservation_active_ = false;
  }

  // ───────────────────────────────────────────────────────────────────────────
  // 도구 호출 횟수 및 출력 바이트 기록 (S4-04)
  // ───────────────────────────────────────────────────────────────────────────
  Error RecordToolCall(std::size_t output_bytes = 0) {
    if (tool_calls_count_ >= budget_.max_tool_calls) {
      return Error{Errc::BudgetExhausted, reason::kBudgetToolCalls,
                   "Turn tool call limit exceeded"};
    }
    if (output_bytes > budget_.max_total_output_bytes ||
        total_output_bytes_ > budget_.max_total_output_bytes - output_bytes ||
        total_output_bytes_ > SIZE_MAX - output_bytes) {
      return Error{Errc::BudgetExhausted, reason::kBudgetTokens,
                   "Turn total output byte limit exceeded"};
    }
    tool_calls_count_++;
    total_output_bytes_ += output_bytes;
    return Error::Ok();
  }

  std::size_t prompt_tokens_used() const noexcept { return prompt_tokens_used_; }
  std::size_t completion_tokens_used() const noexcept { return completion_tokens_used_; }
  std::size_t total_tokens_used() const noexcept { return total_tokens_used_; }
  std::size_t reserved_tokens() const noexcept { return reserved_tokens_; }
  std::size_t tool_calls_count() const noexcept { return tool_calls_count_; }
  std::size_t total_output_bytes() const noexcept { return total_output_bytes_; }
  const TurnBudget& budget() const noexcept { return budget_; }

 private:
  TurnBudget  budget_;
  bool        turn_started_ = false;
  std::int64_t start_monotonic_ns_ = 0;
  std::int64_t deadline_ns_ = 0;

  std::size_t prompt_tokens_used_ = 0;
  std::size_t completion_tokens_used_ = 0;
  std::size_t total_tokens_used_ = 0;
  std::size_t reserved_tokens_ = 0;
  bool        reservation_active_ = false;

  std::size_t tool_calls_count_ = 0;
  std::size_t total_output_bytes_ = 0;
};

}  // namespace cogito

#endif  // COGITO_BUDGET_HPP
