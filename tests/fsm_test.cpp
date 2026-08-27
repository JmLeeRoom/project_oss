// SPDX-License-Identifier: Apache-2.0

#include "cogito/fsm.hpp"

#include <array>
#include <cstddef>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "cogito/clock.hpp"
#include "cogito/tool.hpp"

namespace {

using cogito::Event;
using cogito::Fsm;
using cogito::State;
using cogito::TransitionRecord;

constexpr std::size_t kStateCount =
    static_cast<std::size_t>(State::Cancelled) + 1U;
constexpr std::size_t kEventCount =
    static_cast<std::size_t>(Event::StartNextTurn) + 1U;

enum class ExpectedKind {
  Explicit,
  Universal,
  Noop,
  Undefined,
};

struct ExpectedTransition {
  ExpectedKind kind;
  State to;
};

struct ExpectedDumpRow {
  State from;
  Event event;
  State to;
  const char* type;
};

constexpr std::array<std::pair<State, const char*>, 10> kStateNames{{
    {State::Idle, "Idle"},
    {State::Infer, "Infer"},
    {State::Propose, "Propose"},
    {State::Gate, "Gate"},
    {State::AwaitApproval, "AwaitApproval"},
    {State::Execute, "Execute"},
    {State::Observe, "Observe"},
    {State::Done, "Done"},
    {State::Failed, "Failed"},
    {State::Cancelled, "Cancelled"},
}};

constexpr std::array<std::pair<Event, const char*>, 19> kEventNames{{
    {Event::UserInput, "UserInput"},
    {Event::InferOk, "InferOk"},
    {Event::ProviderError, "ProviderError"},
    {Event::BudgetExhausted, "BudgetExhausted"},
    {Event::Cancel, "Cancel"},
    {Event::NoAction, "NoAction"},
    {Event::OneAction, "OneAction"},
    {Event::MultipleActions, "MultipleActions"},
    {Event::Deny, "Deny"},
    {Event::Ask, "Ask"},
    {Event::Allow, "Allow"},
    {Event::AuditError, "AuditError"},
    {Event::Approved, "Approved"},
    {Event::RejectedOrExpired, "RejectedOrExpired"},
    {Event::ExecOk, "ExecOk"},
    {Event::ExecErrorOrIndeterminate, "ExecErrorOrIndeterminate"},
    {Event::Continue, "Continue"},
    {Event::CompleteOrLimit, "CompleteOrLimit"},
    {Event::StartNextTurn, "StartNextTurn"},
}};

constexpr std::array<ExpectedDumpRow, 30> kExpectedDumpRows{{
    {State::AwaitApproval, Event::Approved, State::Gate, "explicit"},
    {State::AwaitApproval, Event::AuditError, State::Failed, "universal"},
    {State::AwaitApproval, Event::Cancel, State::Cancelled, "universal"},
    {State::AwaitApproval, Event::RejectedOrExpired, State::Observe, "explicit"},
    {State::Cancelled, Event::StartNextTurn, State::Idle, "explicit"},
    {State::Done, Event::StartNextTurn, State::Idle, "explicit"},
    {State::Execute, Event::AuditError, State::Failed, "universal"},
    {State::Execute, Event::ExecErrorOrIndeterminate, State::Observe, "explicit"},
    {State::Execute, Event::ExecOk, State::Observe, "explicit"},
    {State::Failed, Event::StartNextTurn, State::Idle, "explicit"},
    {State::Gate, Event::Allow, State::Execute, "explicit"},
    {State::Gate, Event::Ask, State::AwaitApproval, "explicit"},
    {State::Gate, Event::AuditError, State::Failed, "universal"},
    {State::Gate, Event::Cancel, State::Cancelled, "universal"},
    {State::Gate, Event::Deny, State::Observe, "explicit"},
    {State::Idle, Event::UserInput, State::Infer, "explicit"},
    {State::Infer, Event::AuditError, State::Failed, "universal"},
    {State::Infer, Event::BudgetExhausted, State::Done, "explicit"},
    {State::Infer, Event::Cancel, State::Cancelled, "universal"},
    {State::Infer, Event::InferOk, State::Propose, "explicit"},
    {State::Infer, Event::ProviderError, State::Failed, "explicit"},
    {State::Observe, Event::AuditError, State::Failed, "universal"},
    {State::Observe, Event::Cancel, State::Cancelled, "universal"},
    {State::Observe, Event::CompleteOrLimit, State::Done, "explicit"},
    {State::Observe, Event::Continue, State::Infer, "explicit"},
    {State::Propose, Event::AuditError, State::Failed, "universal"},
    {State::Propose, Event::Cancel, State::Cancelled, "universal"},
    {State::Propose, Event::MultipleActions, State::Failed, "explicit"},
    {State::Propose, Event::NoAction, State::Done, "explicit"},
    {State::Propose, Event::OneAction, State::Gate, "explicit"},
}};

constexpr const char* kDumpGolden =
    R"json([{"ev":"Approved","from":"AwaitApproval","to":"Gate","type":"explicit"},{"ev":"AuditError","from":"AwaitApproval","to":"Failed","type":"universal"},{"ev":"Cancel","from":"AwaitApproval","to":"Cancelled","type":"universal"},{"ev":"RejectedOrExpired","from":"AwaitApproval","to":"Observe","type":"explicit"},{"ev":"StartNextTurn","from":"Cancelled","to":"Idle","type":"explicit"},{"ev":"StartNextTurn","from":"Done","to":"Idle","type":"explicit"},{"ev":"AuditError","from":"Execute","to":"Failed","type":"universal"},{"ev":"ExecErrorOrIndeterminate","from":"Execute","to":"Observe","type":"explicit"},{"ev":"ExecOk","from":"Execute","to":"Observe","type":"explicit"},{"ev":"StartNextTurn","from":"Failed","to":"Idle","type":"explicit"},{"ev":"Allow","from":"Gate","to":"Execute","type":"explicit"},{"ev":"Ask","from":"Gate","to":"AwaitApproval","type":"explicit"},{"ev":"AuditError","from":"Gate","to":"Failed","type":"universal"},{"ev":"Cancel","from":"Gate","to":"Cancelled","type":"universal"},{"ev":"Deny","from":"Gate","to":"Observe","type":"explicit"},{"ev":"UserInput","from":"Idle","to":"Infer","type":"explicit"},{"ev":"AuditError","from":"Infer","to":"Failed","type":"universal"},{"ev":"BudgetExhausted","from":"Infer","to":"Done","type":"explicit"},{"ev":"Cancel","from":"Infer","to":"Cancelled","type":"universal"},{"ev":"InferOk","from":"Infer","to":"Propose","type":"explicit"},{"ev":"ProviderError","from":"Infer","to":"Failed","type":"explicit"},{"ev":"AuditError","from":"Observe","to":"Failed","type":"universal"},{"ev":"Cancel","from":"Observe","to":"Cancelled","type":"universal"},{"ev":"CompleteOrLimit","from":"Observe","to":"Done","type":"explicit"},{"ev":"Continue","from":"Observe","to":"Infer","type":"explicit"},{"ev":"AuditError","from":"Propose","to":"Failed","type":"universal"},{"ev":"Cancel","from":"Propose","to":"Cancelled","type":"universal"},{"ev":"MultipleActions","from":"Propose","to":"Failed","type":"explicit"},{"ev":"NoAction","from":"Propose","to":"Done","type":"explicit"},{"ev":"OneAction","from":"Propose","to":"Gate","type":"explicit"}])json";

bool ExpectedIsTerminal(State state) noexcept {
  return state == State::Done || state == State::Failed || state == State::Cancelled;
}

bool ExpectedAuditSource(State state) noexcept {
  return state == State::Infer || state == State::Propose || state == State::Gate ||
         state == State::AwaitApproval || state == State::Execute ||
         state == State::Observe;
}

bool ExpectedCancelSource(State state) noexcept {
  return state == State::Infer || state == State::Propose || state == State::Gate ||
         state == State::AwaitApproval || state == State::Observe;
}

ExpectedTransition ExpectedFor(State from, Event event) {
  for (const cogito::Transition& transition : cogito::kTransitions) {
    if (transition.from == from && transition.ev == event) {
      return ExpectedTransition{ExpectedKind::Explicit, transition.to};
    }
  }

  if ((ExpectedIsTerminal(from) &&
       (event == Event::AuditError || event == Event::Cancel)) ||
      (event == Event::Cancel &&
       (from == State::Idle || from == State::Execute))) {
    return ExpectedTransition{ExpectedKind::Noop, from};
  }
  if (event == Event::AuditError && ExpectedAuditSource(from)) {
    return ExpectedTransition{ExpectedKind::Universal, State::Failed};
  }
  if (event == Event::Cancel && ExpectedCancelSource(from)) {
    return ExpectedTransition{ExpectedKind::Universal, State::Cancelled};
  }
  return ExpectedTransition{ExpectedKind::Undefined, State::Failed};
}

void EnsureProcessEpochInitialized() {
  if (cogito::ProcessEpochId().empty()) {
    const cogito::Error initialized = cogito::InitProcessEpoch();
    REQUIRE(initialized.ok());
  }
  REQUIRE_FALSE(cogito::ProcessEpochId().empty());
}

TransitionRecord PoisonedRecord() {
  TransitionRecord record;
  record.applied = false;
  record.from = State::Cancelled;
  record.to = State::Done;
  record.ev = Event::StartNextTurn;
  record.cause = "poison-cause";
  record.action_id = "poison-action";
  record.turn_id = 999U;
  record.wall_utc = "poison-time";
  record.monotonic_ns = -1;
  record.process_epoch_id = "poison-epoch";
  return record;
}

}  // namespace

TEST_CASE("FSM state and event strings are stable", "[fsm][strings]") {
  for (const auto& entry : kStateNames) {
    REQUIRE(std::string_view(cogito::ToString(entry.first)) == entry.second);
  }
  for (const auto& entry : kEventNames) {
    REQUIRE(std::string_view(cogito::ToString(entry.first)) == entry.second);
  }
  REQUIRE(std::string_view(cogito::ToString(static_cast<State>(255U))) == "unknown");
  REQUIRE(std::string_view(cogito::ToString(static_cast<Event>(255U))) == "unknown");
}

TEST_CASE("FSM terminal classification contains exactly three states", "[fsm]") {
  std::size_t terminal_count = 0;
  for (std::size_t index = 0; index < kStateCount; ++index) {
    const State state = static_cast<State>(index);
    const bool expected = ExpectedIsTerminal(state);
    REQUIRE(cogito::IsTerminal(state) == expected);
    if (expected) {
      ++terminal_count;
    }
  }
  REQUIRE(terminal_count == 3U);
  REQUIRE_FALSE(cogito::IsTerminal(static_cast<State>(255U)));
}

TEST_CASE("FSM dispatch exhaustively covers all state and event pairs",
          "[fsm][dispatch]") {
  EnsureProcessEpochInitialized();
  const std::string cause = "test-cause";
  const cogito::ActionId action_id = "action-123";
  constexpr cogito::TurnId kTurn = 42U;
  const cogito::FakeClock clock("2026-08-26T01:02:03Z", 123456789);

  std::size_t explicit_count = 0;
  std::size_t universal_count = 0;
  std::size_t noop_count = 0;
  std::size_t undefined_count = 0;

  for (std::size_t state_index = 0; state_index < kStateCount; ++state_index) {
    const State from = static_cast<State>(state_index);
    for (std::size_t event_index = 0; event_index < kEventCount; ++event_index) {
      const Event event = static_cast<Event>(event_index);
      const ExpectedTransition expected = ExpectedFor(from, event);
      CAPTURE(cogito::ToString(from), cogito::ToString(event));

      Fsm fsm;
      fsm.ResetForTestOnly(from);
      TransitionRecord record = PoisonedRecord();
      const cogito::Error error =
          fsm.Dispatch(event, cause, action_id, kTurn, clock, &record);

      REQUIRE(record.from == from);
      REQUIRE(record.to == expected.to);
      REQUIRE(record.ev == event);
      REQUIRE(record.action_id == action_id);
      REQUIRE(record.turn_id == kTurn);
      REQUIRE(record.wall_utc == "2026-08-26T01:02:03Z");
      REQUIRE(record.monotonic_ns == 123456789);
      REQUIRE(record.process_epoch_id == cogito::ProcessEpochId());
      REQUIRE(fsm.current() == expected.to);

      switch (expected.kind) {
        case ExpectedKind::Explicit:
          ++explicit_count;
          REQUIRE(error.ok());
          REQUIRE(record.applied);
          REQUIRE(record.cause == cause);
          break;
        case ExpectedKind::Universal:
          ++universal_count;
          REQUIRE(error.ok());
          REQUIRE(record.applied);
          REQUIRE(record.cause == cause);
          break;
        case ExpectedKind::Noop:
          ++noop_count;
          REQUIRE(error.ok());
          REQUIRE_FALSE(record.applied);
          REQUIRE(record.cause == cause);
          REQUIRE(fsm.current() == from);
          break;
        case ExpectedKind::Undefined: {
          ++undefined_count;
          REQUIRE(error.code == cogito::Errc::Internal);
          REQUIRE(error.reason_code == cogito::reason::kInvalidFsmState);
          REQUIRE(error.detail ==
                  std::string(cogito::ToString(from)) + "/" + cogito::ToString(event));
          REQUIRE(record.applied);
          REQUIRE(record.cause ==
                  "undefined_transition:" + error.detail);
          break;
        }
      }
    }
  }

  REQUIRE(explicit_count == 19U);
  REQUIRE(universal_count == 11U);
  REQUIRE(noop_count == 8U);
  REQUIRE(undefined_count == 152U);
}

TEST_CASE("FSM dispatch accepts a null transition record", "[fsm][dispatch]") {
  const cogito::FakeClock clock;
  Fsm fsm;

  cogito::Error error =
      fsm.Dispatch(Event::UserInput, "cause", "action", 1U, clock, nullptr);
  REQUIRE(error.ok());
  REQUIRE(fsm.current() == State::Infer);

  fsm.ResetForTestOnly(State::Execute);
  error = fsm.Dispatch(Event::Cancel, "cause", "action", 1U, clock, nullptr);
  REQUIRE(error.ok());
  REQUIRE(fsm.current() == State::Execute);

  fsm.ResetForTestOnly(State::Idle);
  error = fsm.Dispatch(Event::InferOk, "cause", "action", 1U, clock, nullptr);
  REQUIRE(error.code == cogito::Errc::Internal);
  REQUIRE(fsm.current() == State::Failed);
}

TEST_CASE("FSM maps every tool result status to an execution event",
          "[fsm][tool-result]") {
  struct Mapping {
    cogito::ToolResultStatus status;
    Event event;
  };
  constexpr std::array<Mapping, 5> mappings{{
      {cogito::ToolResultStatus::Ok, Event::ExecOk},
      {cogito::ToolResultStatus::Error, Event::ExecErrorOrIndeterminate},
      {cogito::ToolResultStatus::Timeout, Event::ExecErrorOrIndeterminate},
      {cogito::ToolResultStatus::Cancelled, Event::ExecErrorOrIndeterminate},
      {cogito::ToolResultStatus::Indeterminate, Event::ExecErrorOrIndeterminate},
  }};

  const cogito::FakeClock clock;
  for (const Mapping& mapping : mappings) {
    const Event mapped = Fsm::MapToolResultStatus(mapping.status);
    REQUIRE(mapped == mapping.event);

    Fsm fsm;
    fsm.ResetForTestOnly(State::Execute);
    TransitionRecord record;
    const cogito::Error error =
        fsm.Dispatch(mapped, "tool-result", "action", 9U, clock, &record);
    REQUIRE(error.ok());
    REQUIRE(record.applied);
    REQUIRE(fsm.current() == State::Observe);
  }

  REQUIRE(Fsm::MapToolResultStatus(
              static_cast<cogito::ToolResultStatus>(255U)) ==
          Event::ExecErrorOrIndeterminate);
}

TEST_CASE("FSM transition table integrity and reachability hold",
          "[fsm][integrity]") {
  const cogito::Error integrity = Fsm::VerifyTableIntegrity();
  REQUIRE(integrity.ok());

  std::set<std::pair<std::size_t, std::size_t>> unique_pairs;
  for (const cogito::Transition& transition : cogito::kTransitions) {
    const auto key = std::make_pair(static_cast<std::size_t>(transition.from),
                                    static_cast<std::size_t>(transition.ev));
    REQUIRE(unique_pairs.insert(key).second);
    REQUIRE(transition.ev != Event::AuditError);
    REQUIRE(transition.ev != Event::Cancel);
  }
  REQUIRE(unique_pairs.size() == cogito::kTransitions.size());

  std::array<bool, kStateCount> reached{};
  std::queue<State> pending;
  reached[static_cast<std::size_t>(State::Idle)] = true;
  pending.push(State::Idle);
  while (!pending.empty()) {
    const State from = pending.front();
    pending.pop();
    for (const cogito::Transition& transition : cogito::kTransitions) {
      const std::size_t to_index = static_cast<std::size_t>(transition.to);
      if (transition.from == from && !reached[to_index]) {
        reached[to_index] = true;
        pending.push(transition.to);
      }
    }
  }
  for (std::size_t index = 0; index < kStateCount; ++index) {
    const State state = static_cast<State>(index);
    if (!ExpectedIsTerminal(state)) {
      REQUIRE(reached[index]);
    }
  }

  for (const State terminal :
       {State::Done, State::Failed, State::Cancelled}) {
    std::size_t reset_count = 0;
    for (const cogito::Transition& transition : cogito::kTransitions) {
      if (transition.from == terminal &&
          transition.ev == Event::StartNextTurn &&
          transition.to == State::Idle) {
        ++reset_count;
      }
    }
    REQUIRE(reset_count == 1U);
  }
}

TEST_CASE("FSM dump is the exact canonical 30-edge graph", "[fsm][dump][golden]") {
  const cogito::ccj::Json dump = Fsm::DumpTable();
  REQUIRE(dump.is_array());
  REQUIRE(dump.size() == kExpectedDumpRows.size());

  std::size_t explicit_count = 0;
  std::size_t universal_count = 0;
  std::set<std::pair<std::string, std::string>> unique_pairs;
  const cogito::FakeClock clock;

  for (std::size_t index = 0; index < kExpectedDumpRows.size(); ++index) {
    const cogito::ccj::Json& row = dump.at(index);
    const ExpectedDumpRow& expected = kExpectedDumpRows[index];
    CAPTURE(index);

    REQUIRE(row.is_object());
    REQUIRE(row.size() == 4U);
    REQUIRE(row.at("from").get<std::string>() == cogito::ToString(expected.from));
    REQUIRE(row.at("ev").get<std::string>() == cogito::ToString(expected.event));
    REQUIRE(row.at("to").get<std::string>() == cogito::ToString(expected.to));
    REQUIRE(row.at("type").get<std::string>() == expected.type);

    const auto key = std::make_pair(row.at("from").get<std::string>(),
                                    row.at("ev").get<std::string>());
    REQUIRE(unique_pairs.insert(key).second);

    if (std::string_view(expected.type) == "explicit") {
      ++explicit_count;
    } else {
      REQUIRE(std::string_view(expected.type) == "universal");
      ++universal_count;
    }

    Fsm fsm;
    fsm.ResetForTestOnly(expected.from);
    TransitionRecord record;
    const cogito::Error error = fsm.Dispatch(
        expected.event, "dump-cross-check", "action", 1U, clock, &record);
    REQUIRE(error.ok());
    REQUIRE(record.applied);
    REQUIRE(fsm.current() == expected.to);
  }

  REQUIRE(explicit_count == 19U);
  REQUIRE(universal_count == 11U);
  REQUIRE(unique_pairs.size() == kExpectedDumpRows.size());

  const auto serialized = cogito::ccj::Serialize(dump);
  REQUIRE(serialized.ok());
  REQUIRE(serialized.value() == kDumpGolden);

  const auto reparsed = cogito::ccj::ParseStrict(kDumpGolden);
  REQUIRE(reparsed.ok());
  REQUIRE(reparsed.value() == dump);
}
