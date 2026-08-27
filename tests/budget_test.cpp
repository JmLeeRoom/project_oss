// SPDX-License-Identifier: Apache-2.0

#include "cogito/budget.hpp"

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("TurnBudget default values match specification", "[budget][defaults]") {
  cogito::TurnBudget budget;
  REQUIRE(budget.max_prompt_tokens == 8192);
  REQUIRE(budget.max_completion_tokens == 4096);
  REQUIRE(budget.max_total_tokens == 16384);
  REQUIRE(budget.max_tool_calls == 10);
  REQUIRE(budget.max_total_output_bytes == 1024 * 1024);
  REQUIRE(budget.timeout_ns == 30'000'000'000LL);
}

TEST_CASE("BudgetTracker tracks turn start and monotonic deadline", "[budget][deadline]") {
  cogito::TurnBudget budget;
  budget.timeout_ns = 5'000'000'000LL;  // 5 seconds

  cogito::BudgetTracker tracker(budget);
  REQUIRE(tracker.start_monotonic_ns() == 0);
  REQUIRE(tracker.deadline_ns() == 0);

  const std::int64_t start_time = 1'000'000'000LL;
  tracker.StartTurn(start_time);

  REQUIRE(tracker.start_monotonic_ns() == start_time);
  REQUIRE(tracker.deadline_ns() == start_time + 5'000'000'000LL);

  // Before deadline
  REQUIRE_FALSE(tracker.IsDeadlineExceeded(start_time + 4'999'999'999LL));
  REQUIRE(tracker.CheckDeadline(start_time + 4'999'999'999LL).ok());
  REQUIRE(tracker.remaining_ns(start_time + 4'000'000'000LL) == 1'000'000'000LL);

  // Exact deadline boundary (fail-closed: boundary is considered expired)
  REQUIRE(tracker.IsDeadlineExceeded(start_time + 5'000'000'000LL));
  const cogito::Error boundary_err = tracker.CheckDeadline(start_time + 5'000'000'000LL);
  REQUIRE(boundary_err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(boundary_err.reason_code == cogito::reason::kBudgetDeadline);
  REQUIRE(tracker.remaining_ns(start_time + 5'000'000'000LL) == 0);

  // After deadline
  REQUIRE(tracker.IsDeadlineExceeded(start_time + 6'000'000'000LL));
  const cogito::Error after_err = tracker.CheckDeadline(start_time + 6'000'000'000LL);
  REQUIRE(after_err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(after_err.reason_code == cogito::reason::kBudgetDeadline);
  REQUIRE(tracker.remaining_ns(start_time + 6'000'000'000LL) == 0);
}

TEST_CASE("BudgetTracker handles deadline overflow and zero timeout", "[budget][deadline][overflow]") {
  cogito::TurnBudget budget;
  budget.timeout_ns = 1000LL;

  cogito::BudgetTracker tracker(budget);
  tracker.StartTurn(INT64_MAX - 500LL);
  REQUIRE(tracker.deadline_ns() == INT64_MAX);
  REQUIRE_FALSE(tracker.IsDeadlineExceeded(INT64_MAX - 1LL));
  REQUIRE(tracker.IsDeadlineExceeded(INT64_MAX));
  REQUIRE(tracker.remaining_ns(INT64_MIN) == INT64_MAX);

  cogito::TurnBudget zero_budget;
  zero_budget.timeout_ns = 0;
  cogito::BudgetTracker zero_tracker(zero_budget);
  zero_tracker.StartTurn(0LL);
  REQUIRE(zero_tracker.deadline_ns() == 0LL);
  REQUIRE(zero_tracker.IsDeadlineExceeded(0LL));
  REQUIRE(zero_tracker.CheckDeadline(0LL).reason_code ==
          cogito::reason::kBudgetDeadline);

  cogito::TurnBudget negative_budget;
  negative_budget.timeout_ns = -1;
  cogito::BudgetTracker negative_tracker(negative_budget);
  negative_tracker.StartTurn(-100LL);
  REQUIRE(negative_tracker.deadline_ns() == -100LL);
  REQUIRE_FALSE(negative_tracker.IsDeadlineExceeded(-101LL));
  REQUIRE(negative_tracker.IsDeadlineExceeded(-100LL));
  REQUIRE(negative_tracker.remaining_ns(INT64_MIN) == INT64_MAX - 99LL);
}

TEST_CASE("BudgetTracker manages token reservation, settlement, and release", "[budget][tokens]") {
  cogito::TurnBudget budget;
  budget.max_prompt_tokens = 1000;
  budget.max_completion_tokens = 500;
  budget.max_total_tokens = 1200;

  cogito::BudgetTracker tracker(budget);

  // Exceed prompt limit
  cogito::Error err = tracker.ReserveTokens(1001, 100);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);

  // Exceed completion limit
  err = tracker.ReserveTokens(500, 501);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);

  // Exceed total limit
  err = tracker.ReserveTokens(800, 500);  // 1300 > 1200
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);

  // Valid reservation
  REQUIRE(tracker.ReserveTokens(500, 300).ok());
  REQUIRE(tracker.reserved_tokens() == 800);
  REQUIRE(tracker.total_tokens_used() == 0);

  // A second reservation is rejected without changing the active reservation.
  err = tracker.ReserveTokens(1, 1);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);
  REQUIRE(tracker.reserved_tokens() == 800);
  REQUIRE(tracker.total_tokens_used() == 0);

  // Release reservation without usage
  tracker.ReleaseReservation();
  REQUIRE(tracker.reserved_tokens() == 0);
  REQUIRE(tracker.total_tokens_used() == 0);

  // Reserve and settle
  REQUIRE(tracker.ReserveTokens(400, 200).ok());
  REQUIRE(tracker.reserved_tokens() == 600);

  // Failed settlement is atomic and leaves the reservation active.
  err = tracker.SettleTokens(std::numeric_limits<std::size_t>::max(), 1);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);
  REQUIRE(tracker.reserved_tokens() == 600);
  REQUIRE(tracker.prompt_tokens_used() == 0);
  REQUIRE(tracker.completion_tokens_used() == 0);
  REQUIRE(tracker.total_tokens_used() == 0);

  REQUIRE(tracker.SettleTokens(400, 150).ok());
  REQUIRE(tracker.reserved_tokens() == 0);
  REQUIRE(tracker.prompt_tokens_used() == 400);
  REQUIRE(tracker.completion_tokens_used() == 150);
  REQUIRE(tracker.total_tokens_used() == 550);

  // Settlement is single-use; a second settlement cannot alter accounting.
  err = tracker.SettleTokens(1, 1);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);
  REQUIRE(tracker.prompt_tokens_used() == 400);
  REQUIRE(tracker.completion_tokens_used() == 150);
  REQUIRE(tracker.total_tokens_used() == 550);

  // Next reservation check against remaining budget
  // Remaining total budget: 1200 - 550 = 650
  err = tracker.ReserveTokens(500, 200);  // 700 > 650
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);

  REQUIRE(tracker.ReserveTokens(400, 200).ok());  // 600 <= 650
  REQUIRE(tracker.SettleTokens(400, 200).ok());
  REQUIRE(tracker.total_tokens_used() == 1150);

  // A zero-token reservation is still active until settled or released.
  cogito::BudgetTracker zero_reservation_tracker(budget);
  REQUIRE(zero_reservation_tracker.ReserveTokens(0, 0).ok());
  REQUIRE(zero_reservation_tracker.reserved_tokens() == 0);
  err = zero_reservation_tracker.ReserveTokens(1, 0);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(zero_reservation_tracker.SettleTokens(0, 0).ok());
  err = zero_reservation_tracker.SettleTokens(0, 0);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);

  // Individual actual limits are checked atomically while preserving reservation.
  cogito::BudgetTracker actual_limit_tracker(budget);
  REQUIRE(actual_limit_tracker.ReserveTokens(0, 0).ok());
  err = actual_limit_tracker.SettleTokens(budget.max_prompt_tokens + 1U, 0);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(actual_limit_tracker.reserved_tokens() == 0);
  REQUIRE(actual_limit_tracker.total_tokens_used() == 0);
  err = actual_limit_tracker.SettleTokens(0, budget.max_completion_tokens + 1U);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(actual_limit_tracker.reserved_tokens() == 0);
  REQUIRE(actual_limit_tracker.total_tokens_used() == 0);
  REQUIRE(actual_limit_tracker.SettleTokens(0, 0).ok());

  // Actual usage may exceed the estimate, but it is fully accounted if in budget.
  cogito::BudgetTracker underestimated_tracker(budget);
  REQUIRE(underestimated_tracker.ReserveTokens(100, 100).ok());
  REQUIRE(underestimated_tracker.SettleTokens(200, 100).ok());
  REQUIRE(underestimated_tracker.reserved_tokens() == 0);
  REQUIRE(underestimated_tracker.prompt_tokens_used() == 200);
  REQUIRE(underestimated_tracker.completion_tokens_used() == 100);
  REQUIRE(underestimated_tracker.total_tokens_used() == 300);

  cogito::TurnBudget unbounded_budget;
  unbounded_budget.max_prompt_tokens = std::numeric_limits<std::size_t>::max();
  unbounded_budget.max_completion_tokens = std::numeric_limits<std::size_t>::max();
  unbounded_budget.max_total_tokens = std::numeric_limits<std::size_t>::max();
  cogito::BudgetTracker overflow_tracker(unbounded_budget);
  err = overflow_tracker.ReserveTokens(
      std::numeric_limits<std::size_t>::max(), 1U);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(overflow_tracker.reserved_tokens() == 0);
  REQUIRE(overflow_tracker.ReserveTokens(0, 0).ok());
  err = overflow_tracker.SettleTokens(
      std::numeric_limits<std::size_t>::max(), 1U);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(overflow_tracker.reserved_tokens() == 0);
  REQUIRE(overflow_tracker.total_tokens_used() == 0);
  REQUIRE(overflow_tracker.SettleTokens(0, 0).ok());
}

TEST_CASE("BudgetTracker tracks tool call count and total output bytes", "[budget][tool_calls]") {
  cogito::TurnBudget budget;
  budget.max_tool_calls = 3;
  budget.max_total_output_bytes = 1000;

  cogito::BudgetTracker tracker(budget);

  // First call
  REQUIRE(tracker.RecordToolCall(300).ok());
  REQUIRE(tracker.tool_calls_count() == 1);
  REQUIRE(tracker.total_output_bytes() == 300);

  // Second call
  REQUIRE(tracker.RecordToolCall(400).ok());
  REQUIRE(tracker.tool_calls_count() == 2);
  REQUIRE(tracker.total_output_bytes() == 700);

  // Output bytes exceed limit (700 + 400 = 1100 > 1000)
  cogito::Error err = tracker.RecordToolCall(400);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);
  REQUIRE(tracker.tool_calls_count() == 2);
  REQUIRE(tracker.total_output_bytes() == 700);

  // Third call within byte limit
  REQUIRE(tracker.RecordToolCall(200).ok());
  REQUIRE(tracker.tool_calls_count() == 3);
  REQUIRE(tracker.total_output_bytes() == 900);

  // Fourth call exceeds tool call count limit
  err = tracker.RecordToolCall(50);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetToolCalls);
  REQUIRE(tracker.tool_calls_count() == 3);

  cogito::TurnBudget byte_overflow_budget;
  byte_overflow_budget.max_tool_calls = 2;
  byte_overflow_budget.max_total_output_bytes =
      std::numeric_limits<std::size_t>::max();
  cogito::BudgetTracker byte_overflow_tracker(byte_overflow_budget);
  REQUIRE(byte_overflow_tracker.RecordToolCall(
      std::numeric_limits<std::size_t>::max()).ok());
  err = byte_overflow_tracker.RecordToolCall(1U);
  REQUIRE(err.code == cogito::Errc::BudgetExhausted);
  REQUIRE(err.reason_code == cogito::reason::kBudgetTokens);
  REQUIRE(byte_overflow_tracker.tool_calls_count() == 1);
  REQUIRE(byte_overflow_tracker.total_output_bytes() ==
          std::numeric_limits<std::size_t>::max());
}
