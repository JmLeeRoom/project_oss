// SPDX-License-Identifier: Apache-2.0

#include "cogito/fsm.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "cogito/canonical_json.hpp"
#include "cogito/clock.hpp"

namespace {

struct StateName {
  cogito::State state;
  const char* name;
};

struct EventName {
  cogito::Event event;
  const char* name;
};

constexpr std::array<StateName, 10> kStateNames{{
    {cogito::State::Idle, "Idle"},
    {cogito::State::Infer, "Infer"},
    {cogito::State::Propose, "Propose"},
    {cogito::State::Gate, "Gate"},
    {cogito::State::AwaitApproval, "AwaitApproval"},
    {cogito::State::Execute, "Execute"},
    {cogito::State::Observe, "Observe"},
    {cogito::State::Done, "Done"},
    {cogito::State::Failed, "Failed"},
    {cogito::State::Cancelled, "Cancelled"},
}};

constexpr std::array<EventName, 19> kEventNames{{
    {cogito::Event::UserInput, "UserInput"},
    {cogito::Event::InferOk, "InferOk"},
    {cogito::Event::ProviderError, "ProviderError"},
    {cogito::Event::BudgetExhausted, "BudgetExhausted"},
    {cogito::Event::Cancel, "Cancel"},
    {cogito::Event::NoAction, "NoAction"},
    {cogito::Event::OneAction, "OneAction"},
    {cogito::Event::MultipleActions, "MultipleActions"},
    {cogito::Event::Deny, "Deny"},
    {cogito::Event::Ask, "Ask"},
    {cogito::Event::Allow, "Allow"},
    {cogito::Event::AuditError, "AuditError"},
    {cogito::Event::Approved, "Approved"},
    {cogito::Event::RejectedOrExpired, "RejectedOrExpired"},
    {cogito::Event::ExecOk, "ExecOk"},
    {cogito::Event::ExecErrorOrIndeterminate, "ExecErrorOrIndeterminate"},
    {cogito::Event::Continue, "Continue"},
    {cogito::Event::CompleteOrLimit, "CompleteOrLimit"},
    {cogito::Event::StartNextTurn, "StartNextTurn"},
}};

constexpr std::array<cogito::State, 6> kR1States{{
    cogito::State::Infer,
    cogito::State::Propose,
    cogito::State::Gate,
    cogito::State::AwaitApproval,
    cogito::State::Execute,
    cogito::State::Observe,
}};

constexpr std::array<cogito::State, 5> kR2States{{
    cogito::State::Infer,
    cogito::State::Propose,
    cogito::State::Gate,
    cogito::State::AwaitApproval,
    cogito::State::Observe,
}};

constexpr std::array<cogito::State, 3> kTerminalStates{{
    cogito::State::Done,
    cogito::State::Failed,
    cogito::State::Cancelled,
}};

cogito::TransitionRecord SeededRecord() {
  cogito::TransitionRecord record;
  record.applied = true;
  record.from = cogito::State::Cancelled;
  record.to = cogito::State::Cancelled;
  record.ev = cogito::Event::StartNextTurn;
  record.cause = "stale-cause";
  record.action_id = "stale-action";
  record.turn_id = 999;
  record.wall_utc = "stale-wall";
  record.monotonic_ns = -1;
  record.process_epoch_id = "stale-epoch";
  return record;
}

void RequireRecord(const cogito::TransitionRecord& record,
                   bool applied,
                   cogito::State from,
                   cogito::State to,
                   cogito::Event event,
                   const std::string& cause,
                   const cogito::ActionId& action_id,
                   cogito::TurnId turn,
                   const std::string& wall_utc,
                   std::int64_t monotonic_ns) {
  REQUIRE(record.applied == applied);
  REQUIRE(record.from == from);
  REQUIRE(record.to == to);
  REQUIRE(record.ev == event);
  REQUIRE(record.cause == cause);
  REQUIRE(record.action_id == action_id);
  REQUIRE(record.turn_id == turn);
  REQUIRE(record.wall_utc == wall_utc);
  REQUIRE(record.monotonic_ns == monotonic_ns);
  REQUIRE(record.process_epoch_id.empty());
}

const std::string kDumpTableGolden =
    "["
    "{\"event\":\"UserInput\",\"from\":\"Idle\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Infer\"},"
    "{\"event\":\"InferOk\",\"from\":\"Infer\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Propose\"},"
    "{\"event\":\"ProviderError\",\"from\":\"Infer\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Failed\"},"
    "{\"event\":\"BudgetExhausted\",\"from\":\"Infer\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Done\"},"
    "{\"event\":\"NoAction\",\"from\":\"Propose\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Done\"},"
    "{\"event\":\"OneAction\",\"from\":\"Propose\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Gate\"},"
    "{\"event\":\"MultipleActions\",\"from\":\"Propose\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Failed\"},"
    "{\"event\":\"Deny\",\"from\":\"Gate\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Observe\"},"
    "{\"event\":\"Ask\",\"from\":\"Gate\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"AwaitApproval\"},"
    "{\"event\":\"Allow\",\"from\":\"Gate\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Execute\"},"
    "{\"event\":\"Approved\",\"from\":\"AwaitApproval\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Gate\"},"
    "{\"event\":\"RejectedOrExpired\",\"from\":\"AwaitApproval\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Observe\"},"
    "{\"event\":\"ExecOk\",\"from\":\"Execute\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Observe\"},"
    "{\"event\":\"ExecErrorOrIndeterminate\",\"from\":\"Execute\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Observe\"},"
    "{\"event\":\"Continue\",\"from\":\"Observe\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Infer\"},"
    "{\"event\":\"CompleteOrLimit\",\"from\":\"Observe\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Done\"},"
    "{\"event\":\"StartNextTurn\",\"from\":\"Done\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Idle\"},"
    "{\"event\":\"StartNextTurn\",\"from\":\"Failed\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Idle\"},"
    "{\"event\":\"StartNextTurn\",\"from\":\"Cancelled\",\"kind\":\"explicit\",\"rule\":\"explicit\",\"to\":\"Idle\"},"
    "{\"event\":\"Cancel\",\"from\":\"Idle\",\"kind\":\"universal\",\"rule\":\"R0\",\"to\":\"Idle\"},"
    "{\"event\":\"AuditError\",\"from\":\"Infer\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"Propose\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"Gate\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"AwaitApproval\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"Execute\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"Observe\",\"kind\":\"universal\",\"rule\":\"R1\",\"to\":\"Failed\"},"
    "{\"event\":\"Cancel\",\"from\":\"Infer\",\"kind\":\"universal\",\"rule\":\"R2\",\"to\":\"Cancelled\"},"
    "{\"event\":\"Cancel\",\"from\":\"Propose\",\"kind\":\"universal\",\"rule\":\"R2\",\"to\":\"Cancelled\"},"
    "{\"event\":\"Cancel\",\"from\":\"Gate\",\"kind\":\"universal\",\"rule\":\"R2\",\"to\":\"Cancelled\"},"
    "{\"event\":\"Cancel\",\"from\":\"AwaitApproval\",\"kind\":\"universal\",\"rule\":\"R2\",\"to\":\"Cancelled\"},"
    "{\"event\":\"Cancel\",\"from\":\"Observe\",\"kind\":\"universal\",\"rule\":\"R2\",\"to\":\"Cancelled\"},"
    "{\"event\":\"AuditError\",\"from\":\"Done\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Done\"},"
    "{\"event\":\"Cancel\",\"from\":\"Done\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Done\"},"
    "{\"event\":\"AuditError\",\"from\":\"Failed\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Failed\"},"
    "{\"event\":\"Cancel\",\"from\":\"Failed\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Failed\"},"
    "{\"event\":\"AuditError\",\"from\":\"Cancelled\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Cancelled\"},"
    "{\"event\":\"Cancel\",\"from\":\"Cancelled\",\"kind\":\"universal\",\"rule\":\"R4\",\"to\":\"Cancelled\"}"
    "]";

}  // namespace

TEST_CASE("core/fsm.string_mapping", "[fsm]") {
  for (const StateName& mapping : kStateNames) {
    CAPTURE(mapping.name);
    REQUIRE(std::string(cogito::ToString(mapping.state)) == mapping.name);
  }
  for (const EventName& mapping : kEventNames) {
    CAPTURE(mapping.name);
    REQUIRE(std::string(cogito::ToString(mapping.event)) == mapping.name);
  }

  REQUIRE(std::string(cogito::ToString(static_cast<cogito::State>(0xffU))) == "UnknownState");
  REQUIRE(std::string(cogito::ToString(static_cast<cogito::Event>(0xffU))) == "UnknownEvent");
}

TEST_CASE("core/fsm.terminal_classification", "[fsm]") {
  for (const StateName& mapping : kStateNames) {
    const bool expected = mapping.state == cogito::State::Done ||
                          mapping.state == cogito::State::Failed ||
                          mapping.state == cogito::State::Cancelled;
    CAPTURE(mapping.name);
    REQUIRE(cogito::IsTerminal(mapping.state) == expected);
  }
  REQUIRE_FALSE(cogito::IsTerminal(static_cast<cogito::State>(0xffU)));
}

TEST_CASE("core/fsm.explicit_transitions", "[fsm]") {
  STATIC_REQUIRE(cogito::kTransitions.size() == 19U);
  constexpr char kWall[] = "2026-08-27T12:34:56Z";
  constexpr std::int64_t kMonotonic = 42'000;

  for (const cogito::Transition& transition : cogito::kTransitions) {
    CAPTURE(cogito::ToString(transition.from), cogito::ToString(transition.ev));
    cogito::Fsm fsm;
    fsm.ResetForTestOnly(transition.from);
    cogito::FakeClock clock(kWall, kMonotonic);
    cogito::TransitionRecord record = SeededRecord();

    const cogito::Error error =
        fsm.Dispatch(transition.ev, "explicit-cause", "action-7", 7, clock, &record);

    REQUIRE(error.ok());
    REQUIRE(fsm.current() == transition.to);
    RequireRecord(record,
                  true,
                  transition.from,
                  transition.to,
                  transition.ev,
                  "explicit-cause",
                  "action-7",
                  7,
                  kWall,
                  kMonotonic);
  }
}

TEST_CASE("core/fsm.universal_r0_idle_cancel", "[fsm]") {
  cogito::Fsm fsm;
  cogito::FakeClock clock("2026-08-27T00:00:00Z", 100);
  cogito::TransitionRecord record = SeededRecord();

  const cogito::Error error = fsm.Dispatch(cogito::Event::Cancel, "idle-cancel", "action", 3, clock, &record);

  REQUIRE(error.ok());
  REQUIRE(fsm.current() == cogito::State::Idle);
  RequireRecord(record,
                false,
                cogito::State::Idle,
                cogito::State::Idle,
                cogito::Event::Cancel,
                "idle-cancel",
                "action",
                3,
                "2026-08-27T00:00:00Z",
                100);
}

TEST_CASE("core/fsm.universal_r1_audit_error", "[fsm]") {
  for (cogito::State state : kR1States) {
    CAPTURE(cogito::ToString(state));
    cogito::Fsm fsm;
    fsm.ResetForTestOnly(state);
    cogito::FakeClock clock("2026-08-27T01:00:00Z", 200);
    cogito::TransitionRecord record = SeededRecord();

    const cogito::Error error =
        fsm.Dispatch(cogito::Event::AuditError, "audit_failed", "action", 4, clock, &record);

    REQUIRE(error.ok());
    REQUIRE(fsm.current() == cogito::State::Failed);
    RequireRecord(record,
                  true,
                  state,
                  cogito::State::Failed,
                  cogito::Event::AuditError,
                  "audit_failed",
                  "action",
                  4,
                  "2026-08-27T01:00:00Z",
                  200);
  }
}

TEST_CASE("core/fsm.universal_r2_cancel", "[fsm]") {
  for (cogito::State state : kR2States) {
    CAPTURE(cogito::ToString(state));
    cogito::Fsm fsm;
    fsm.ResetForTestOnly(state);
    cogito::FakeClock clock("2026-08-27T02:00:00Z", 300);
    cogito::TransitionRecord record = SeededRecord();

    const cogito::Error error =
        fsm.Dispatch(cogito::Event::Cancel, "cancelled", "action", 5, clock, &record);

    REQUIRE(error.ok());
    REQUIRE(fsm.current() == cogito::State::Cancelled);
    RequireRecord(record,
                  true,
                  state,
                  cogito::State::Cancelled,
                  cogito::Event::Cancel,
                  "cancelled",
                  "action",
                  5,
                  "2026-08-27T02:00:00Z",
                  300);
  }
}

TEST_CASE("core/fsm.universal_r3_execute_cancel_reject", "[fsm]") {
  cogito::Fsm fsm;
  fsm.ResetForTestOnly(cogito::State::Execute);
  cogito::FakeClock clock("2026-08-27T03:00:00Z", 400);
  cogito::TransitionRecord record = SeededRecord();

  const cogito::Error error =
      fsm.Dispatch(cogito::Event::Cancel, "direct_execute_cancel", "action", 6, clock, &record);

  REQUIRE(error.code == cogito::Errc::Internal);
  REQUIRE(fsm.current() == cogito::State::Failed);
  RequireRecord(record,
                true,
                cogito::State::Execute,
                cogito::State::Failed,
                cogito::Event::Cancel,
                "direct_execute_cancel",
                "action",
                6,
                "2026-08-27T03:00:00Z",
                400);
}

TEST_CASE("core/fsm.universal_r4_terminal_noop", "[fsm]") {
  constexpr std::array<cogito::Event, 2> kEvents{{
      cogito::Event::AuditError,
      cogito::Event::Cancel,
  }};

  for (cogito::State state : kTerminalStates) {
    for (cogito::Event event : kEvents) {
      CAPTURE(cogito::ToString(state), cogito::ToString(event));
      cogito::Fsm fsm;
      fsm.ResetForTestOnly(state);
      cogito::FakeClock clock("2026-08-27T04:00:00Z", 500);
      cogito::TransitionRecord record = SeededRecord();

      const cogito::Error error = fsm.Dispatch(event, "terminal-noop", "action", 7, clock, &record);

      REQUIRE(error.ok());
      REQUIRE(fsm.current() == state);
      RequireRecord(record,
                    false,
                    state,
                    state,
                    event,
                    "terminal-noop",
                    "action",
                    7,
                    "2026-08-27T04:00:00Z",
                    500);
    }
  }
}

TEST_CASE("core/fsm.undefined_transitions", "[fsm]") {
  SECTION("empty cause receives stable fallback and every record field is overwritten") {
    cogito::Fsm fsm;
    cogito::FakeClock clock("2026-08-27T05:00:00Z", 600);
    cogito::TransitionRecord record = SeededRecord();

    const cogito::Error error =
        fsm.Dispatch(cogito::Event::AuditError, "", "action", 8, clock, &record);

    REQUIRE(error.code == cogito::Errc::Internal);
    REQUIRE(fsm.current() == cogito::State::Failed);
    RequireRecord(record,
                  true,
                  cogito::State::Idle,
                  cogito::State::Failed,
                  cogito::Event::AuditError,
                  "undefined_transition",
                  "action",
                  8,
                  "2026-08-27T05:00:00Z",
                  600);
  }

  SECTION("invalid enum values fail closed") {
    constexpr auto kInvalidState = static_cast<cogito::State>(0xffU);
    constexpr auto kInvalidEvent = static_cast<cogito::Event>(0xffU);
    cogito::FakeClock clock;

    cogito::Fsm invalid_event_fsm;
    const cogito::Error event_error =
        invalid_event_fsm.Dispatch(kInvalidEvent, "invalid-event", {}, 0, clock, nullptr);
    REQUIRE(event_error.code == cogito::Errc::Internal);
    REQUIRE(invalid_event_fsm.current() == cogito::State::Failed);

    cogito::Fsm invalid_state_fsm;
    invalid_state_fsm.ResetForTestOnly(kInvalidState);
    cogito::TransitionRecord record;
    const cogito::Error state_error = invalid_state_fsm.Dispatch(
        cogito::Event::UserInput, "invalid-state", {}, 0, clock, &record);
    REQUIRE(state_error.code == cogito::Errc::Internal);
    REQUIRE(invalid_state_fsm.current() == cogito::State::Failed);
    REQUIRE(record.from == kInvalidState);
    REQUIRE(record.to == cogito::State::Failed);
  }

  SECTION("null record pointers are accepted on every dispatch branch") {
    cogito::FakeClock clock;

    cogito::Fsm explicit_fsm;
    REQUIRE(explicit_fsm.Dispatch(cogito::Event::UserInput, {}, {}, 0, clock, nullptr).ok());
    REQUIRE(explicit_fsm.current() == cogito::State::Infer);

    cogito::Fsm noop_fsm;
    REQUIRE(noop_fsm.Dispatch(cogito::Event::Cancel, {}, {}, 0, clock, nullptr).ok());
    REQUIRE(noop_fsm.current() == cogito::State::Idle);

    cogito::Fsm undefined_fsm;
    REQUIRE(undefined_fsm.Dispatch(cogito::Event::NoAction, {}, {}, 0, clock, nullptr).code ==
            cogito::Errc::Internal);
    REQUIRE(undefined_fsm.current() == cogito::State::Failed);
  }
}

TEST_CASE("core/fsm.table_integrity", "[fsm]") {
  REQUIRE(cogito::Fsm::VerifyTableIntegrity().ok());
}

TEST_CASE("core/fsm.dump_table_golden", "[fsm]") {
  const cogito::ccj::Json table = cogito::Fsm::DumpTable();
  REQUIRE(table.is_array());
  REQUIRE(table.size() == 37U);

  for (const cogito::ccj::Json& transition : table) {
    REQUIRE(transition.is_object());
    REQUIRE(transition.size() == 5U);
    REQUIRE(transition.contains("event"));
    REQUIRE(transition.contains("from"));
    REQUIRE(transition.contains("kind"));
    REQUIRE(transition.contains("rule"));
    REQUIRE(transition.contains("to"));
  }

  const auto parsed = cogito::ccj::ParseStrict(kDumpTableGolden);
  REQUIRE(parsed.ok());
  REQUIRE(parsed.value() == table);

  const auto serialized = cogito::ccj::Serialize(table);
  REQUIRE(serialized.ok());
  REQUIRE(serialized.value() == kDumpTableGolden);

  for (const cogito::ccj::Json& transition : table) {
    REQUIRE_FALSE(transition == cogito::ccj::Json{{"event", "Cancel"},
                                                  {"from", "Execute"},
                                                  {"kind", "universal"},
                                                  {"rule", "R3"},
                                                  {"to", "Failed"}});
  }
}

TEST_CASE("core/fsm.consecutive_turns", "[fsm]") {
  cogito::FakeClock clock("2026-08-27T06:00:00Z", 700);

  SECTION("normal completion starts an isolated next turn") {
    cogito::Fsm fsm;
    cogito::TransitionRecord record;
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, "turn-1", {}, 1, clock, &record).ok());
    REQUIRE(fsm.Dispatch(cogito::Event::InferOk, "turn-1", {}, 1, clock, &record).ok());
    REQUIRE(fsm.Dispatch(cogito::Event::NoAction, "turn-1", {}, 1, clock, &record).ok());
    REQUIRE(fsm.current() == cogito::State::Done);
    REQUIRE(fsm.Dispatch(cogito::Event::StartNextTurn, "next", {}, 2, clock, &record).ok());
    REQUIRE(fsm.current() == cogito::State::Idle);
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, "turn-2", "new-action", 2, clock, &record).ok());
    REQUIRE(fsm.current() == cogito::State::Infer);
    REQUIRE(record.cause == "turn-2");
    REQUIRE(record.action_id == "new-action");
    REQUIRE(record.turn_id == 2);
  }

  SECTION("failure starts an isolated next turn") {
    cogito::Fsm fsm;
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, {}, {}, 1, clock, nullptr).ok());
    REQUIRE(fsm.Dispatch(cogito::Event::ProviderError, {}, {}, 1, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Failed);
    REQUIRE(fsm.Dispatch(cogito::Event::StartNextTurn, {}, {}, 2, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Idle);
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, {}, {}, 2, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Infer);
  }

  SECTION("cancellation starts an isolated next turn") {
    cogito::Fsm fsm;
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, {}, {}, 1, clock, nullptr).ok());
    REQUIRE(fsm.Dispatch(cogito::Event::Cancel, {}, {}, 1, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Cancelled);
    REQUIRE(fsm.Dispatch(cogito::Event::StartNextTurn, {}, {}, 2, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Idle);
    REQUIRE(fsm.Dispatch(cogito::Event::UserInput, {}, {}, 2, clock, nullptr).ok());
    REQUIRE(fsm.current() == cogito::State::Infer);
  }
}
