// SPDX-License-Identifier: Apache-2.0

#include "cogito/audit.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/ids.hpp"
#include "cogito/result.hpp"

namespace ccj = cogito::ccj;

namespace {

cogito::AuditEvent MakeTestEvent(
    const std::string& kind,
    const cogito::SessionId& session_id = "sess-001",
    cogito::TurnId turn_id = 1,
    const cogito::ActionId& action_id = "act-001",
    ccj::Json payload = ccj::Json::object(),
    std::int64_t monotonic_ns = 1'000'000'000LL,
    std::string process_epoch_id = "epoch-test-01") {
  cogito::AuditEvent event;
  event.event_id = "evt:" + session_id + ":" + std::to_string(turn_id) +
                   ":" + kind + ":" + action_id;
  event.session_id = session_id;
  event.turn_id = turn_id;
  event.action_id = action_id;
  event.wall_time_utc = "2026-08-26T12:00:00Z";
  event.monotonic_ns = monotonic_ns;
  event.process_epoch_id = std::move(process_epoch_id);
  event.kind = kind;
  event.actor_type = cogito::ActorType::Core;
  event.actor_id = "core-engine";
  event.payload = std::move(payload);
  event.schema_version = 1;
  return event;
}

void RequireUnchanged(const cogito::RecordingAuditJournal& journal,
                      std::size_t expected_count,
                      const cogito::Digest& expected_head,
                      std::uint64_t expected_seq) {
  REQUIRE(journal.event_count() == expected_count);
  REQUIRE(journal.GetHeadHash().value() == expected_head);
  REQUIRE(journal.GetHeadSeq().value() == expected_seq);
}

}  // namespace

TEST_CASE("ActorType string conversion and parsing", "[audit][actor]") {
  REQUIRE(cogito::ToString(cogito::ActorType::Core) == "core");
  REQUIRE(cogito::ToString(cogito::ActorType::User) == "user");
  REQUIRE(cogito::ToString(cogito::ActorType::Operator) == "operator");
  REQUIRE(cogito::ToString(cogito::ActorType::System) == "system");
  REQUIRE(cogito::ToString(cogito::ActorType::Tool) == "tool");

  REQUIRE(cogito::ParseActorType("core").value() == cogito::ActorType::Core);
  REQUIRE(cogito::ParseActorType("user").value() == cogito::ActorType::User);
  REQUIRE(cogito::ParseActorType("operator").value() ==
          cogito::ActorType::Operator);
  REQUIRE(cogito::ParseActorType("system").value() ==
          cogito::ActorType::System);
  REQUIRE(cogito::ParseActorType("tool").value() == cogito::ActorType::Tool);

  const auto invalid = cogito::ParseActorType("invalid_actor");
  REQUIRE_FALSE(invalid.ok());
  REQUIRE(invalid.error().code == cogito::Errc::InvalidArgument);
}

TEST_CASE("RecordingAuditJournal commits the canonical hash projection",
          "[audit][commit][golden]") {
  cogito::RecordingAuditJournal journal;
  REQUIRE(journal.GetHeadHash().value() == cogito::Digest::Zero());
  REQUIRE(journal.GetHeadSeq().value() == 0U);

  auto event = MakeTestEvent(cogito::event_kind::kToolResult, "sess-001", 1,
                             "act-001", ccj::Json{{"status", "ok"}},
                             1'000'000LL, "epoch-001");
  event.event_id = "evt-001";
  event.wall_time_utc = "2026-08-24T00:00:00Z";
  event.actor_id = "agent";
  event.seq = 91U;
  event.prev_hash.bytes.fill(0xAAU);
  event.hash.bytes.fill(0xBBU);

  const auto committed = journal.Commit(event);
  REQUIRE(committed.ok());
  REQUIRE(committed.value() == 1U);

  const auto golden = cogito::Digest::FromHex(
      "a20a0bc1a0fe979ad6d988cc92af9a91294a80b2efb2f289180d472b6e065085");
  REQUIRE(golden.ok());

  const auto snapshot = journal.GetEvents();
  REQUIRE(snapshot.size() == 1U);
  REQUIRE(snapshot[0].seq == 1U);
  REQUIRE(snapshot[0].prev_hash == cogito::Digest::Zero());
  REQUIRE(snapshot[0].hash == golden.value());
  REQUIRE(journal.GetHeadHash().value() == golden.value());
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal preserves null payload semantics and snapshots",
          "[audit][commit][null][snapshot]") {
  cogito::RecordingAuditJournal journal;
  auto event = MakeTestEvent(cogito::event_kind::kTurnBegin, "null-session", 1,
                             "", ccj::Json(nullptr));
  REQUIRE(journal.Commit(event).value() == 1U);

  auto snapshot = journal.GetEvents();
  REQUIRE(snapshot[0].payload.is_null());

  const auto null_digest = cogito::ComputeAuditDigest(
      snapshot[0].prev_hash, snapshot[0].event_id, snapshot[0].session_id,
      snapshot[0].turn_id, snapshot[0].action_id, snapshot[0].wall_time_utc,
      static_cast<std::uint64_t>(snapshot[0].monotonic_ns),
      snapshot[0].process_epoch_id, snapshot[0].kind,
      static_cast<std::uint64_t>(snapshot[0].actor_type), snapshot[0].actor_id,
      ccj::Json(nullptr), snapshot[0].schema_version);
  const auto object_digest = cogito::ComputeAuditDigest(
      snapshot[0].prev_hash, snapshot[0].event_id, snapshot[0].session_id,
      snapshot[0].turn_id, snapshot[0].action_id, snapshot[0].wall_time_utc,
      static_cast<std::uint64_t>(snapshot[0].monotonic_ns),
      snapshot[0].process_epoch_id, snapshot[0].kind,
      static_cast<std::uint64_t>(snapshot[0].actor_type), snapshot[0].actor_id,
      ccj::Json::object(), snapshot[0].schema_version);
  REQUIRE(null_digest.ok());
  REQUIRE(object_digest.ok());
  REQUIRE(null_digest.value() == snapshot[0].hash);
  REQUIRE(null_digest.value() != object_digest.value());

  snapshot[0].payload = ccj::Json{{"tampered", true}};
  REQUIRE(journal.GetEvents()[0].payload.is_null());
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal rejects malformed or duplicate events",
          "[audit][validation]") {
  cogito::RecordingAuditJournal journal;

  SECTION("required envelope fields") {
    const auto require_invalid = [&](cogito::AuditEvent event) {
      const auto result = journal.Commit(std::move(event));
      REQUIRE_FALSE(result.ok());
      REQUIRE(result.error().code == cogito::Errc::InvalidArgument);
      REQUIRE(result.error().reason_code == cogito::reason::kInputMissingField);
      REQUIRE(journal.event_count() == 0U);
    };

    auto event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.event_id.clear();
    require_invalid(std::move(event));

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "", 1, "");
    require_invalid(std::move(event));

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 0, "");
    require_invalid(std::move(event));

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.wall_time_utc.clear();
    require_invalid(std::move(event));

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.process_epoch_id.clear();
    require_invalid(std::move(event));

    event = MakeTestEvent("", "s1", 1, "");
    require_invalid(std::move(event));
  }

  SECTION("typed fields") {
    auto event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.monotonic_ns = -1;
    REQUIRE_FALSE(journal.Commit(std::move(event)).ok());

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.actor_type = static_cast<cogito::ActorType>(99U);
    REQUIRE_FALSE(journal.Commit(std::move(event)).ok());

    event = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    event.schema_version = 2U;
    REQUIRE_FALSE(journal.Commit(std::move(event)).ok());
    REQUIRE(journal.event_count() == 0U);
  }

  SECTION("action-bound kinds require an action id") {
    const std::vector<std::string> kinds = {
        cogito::event_kind::kGateVerdict,
        cogito::event_kind::kApprovalRequested,
        cogito::event_kind::kApprovalGranted,
        cogito::event_kind::kApprovalRejected,
        cogito::event_kind::kToolCallStarted,
        cogito::event_kind::kToolResult,
        "extension_event"};
    for (const auto& kind : kinds) {
      auto event = MakeTestEvent(kind, "s1", 1, "");
      REQUIRE_FALSE(journal.Commit(std::move(event)).ok());
    }
  }

  SECTION("event ids are unique") {
    auto first = MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, "");
    auto duplicate = first;
    REQUIRE(journal.Commit(std::move(first)).ok());
    const auto before_head = journal.GetHeadHash().value();
    const auto result = journal.Commit(std::move(duplicate));
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == cogito::Errc::DuplicateKey);
    RequireUnchanged(journal, 1U, before_head, 1U);
  }
}

TEST_CASE("RecordingAuditJournal preserves process-epoch monotonic ordering",
          "[audit][epoch]") {
  cogito::RecordingAuditJournal journal;
  REQUIRE(journal.Commit(MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1,
                                       "", ccj::Json::object(), 100,
                                       "epoch-a"))
              .ok());

  const auto head = journal.GetHeadHash().value();
  auto regressed = MakeTestEvent(cogito::event_kind::kInferBegin, "s1", 1, "",
                                 ccj::Json::object(), 99, "epoch-a");
  const auto rejected = journal.Commit(std::move(regressed));
  REQUIRE_FALSE(rejected.ok());
  RequireUnchanged(journal, 1U, head, 1U);

  REQUIRE(journal.Commit(MakeTestEvent(cogito::event_kind::kInferBegin, "s1", 1,
                                       "", ccj::Json::object(), 1,
                                       "epoch-b"))
              .ok());
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal fault injection is atomic and repeatable",
          "[audit][fault]") {
  cogito::RecordingAuditJournal journal;
  REQUIRE(journal.Commit(
                     MakeTestEvent(cogito::event_kind::kTurnBegin, "s1", 1, ""))
              .ok());
  REQUIRE(journal.Commit(
                     MakeTestEvent(cogito::event_kind::kInferBegin, "s1", 1, ""))
              .ok());

  const auto count = journal.event_count();
  const auto head = journal.GetHeadHash().value();
  const auto seq = journal.GetHeadSeq().value();
  journal.InjectFailureAt(
      3U, cogito::Error{cogito::Errc::AuditWriteFailed,
                        cogito::reason::kAuditCommitFailed, "Disk I/O failure"});

  for (int attempt = 0; attempt < 2; ++attempt) {
    const auto failed = journal.Commit(MakeTestEvent(
        cogito::event_kind::kGateVerdict, "s1", 1, "action-failed"));
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.error().code == cogito::Errc::AuditWriteFailed);
    RequireUnchanged(journal, count, head, seq);
  }

  journal.ClearFailureInjection();
  REQUIRE(journal.Commit(MakeTestEvent(cogito::event_kind::kGateVerdict,
                                       "s1", 1, "action-success"))
              .value() == 3U);

  journal.InjectFailureAt(4U, cogito::Error::Ok());
  const auto default_failure = journal.Commit(MakeTestEvent(
      cogito::event_kind::kToolCallStarted, "s1", 1, "action-success"));
  REQUIRE_FALSE(default_failure.ok());
  REQUIRE(default_failure.error().code == cogito::Errc::AuditWriteFailed);
  journal.ClearFailureInjection();
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal recovery matches session turn and action in order",
          "[audit][recovery]") {
  cogito::RecordingAuditJournal journal;
  std::int64_t monotonic = 10;
  const auto commit = [&](const std::string& kind, const std::string& session,
                          cogito::TurnId turn, const std::string& action,
                          std::string epoch = "epoch-recovery") {
    auto event = MakeTestEvent(kind, session, turn, action, ccj::Json::object(),
                               monotonic, std::move(epoch));
    monotonic += 10;
    return journal.Commit(std::move(event));
  };

  // A result before a start cannot close that later start.
  REQUIRE(commit(cogito::event_kind::kToolResult, "session-a", 1, "shared").ok());
  REQUIRE(commit(cogito::event_kind::kToolCallStarted, "session-a", 1,
                 "shared")
              .ok());

  // Same action id in another session or turn is a different operation.
  REQUIRE(commit(cogito::event_kind::kToolCallStarted, "session-b", 1,
                 "shared")
              .ok());
  REQUIRE(commit(cogito::event_kind::kToolResult, "session-c", 1, "shared")
              .ok());
  REQUIRE(commit(cogito::event_kind::kToolCallStarted, "session-a", 2,
                 "shared")
              .ok());

  // A normal matched pair must not be recovered.
  REQUIRE(commit(cogito::event_kind::kToolCallStarted, "session-a", 1,
                 "complete")
              .ok());
  REQUIRE(commit(cogito::event_kind::kToolResult, "session-a", 1, "complete")
              .ok());

  auto max_time = MakeTestEvent(
      cogito::event_kind::kToolCallStarted, "session-max", 1, "max-time",
      ccj::Json::object(), std::numeric_limits<std::int64_t>::max(),
      "epoch-max");
  REQUIRE(journal.Commit(std::move(max_time)).ok());

  const std::size_t before = journal.event_count();
  const auto recovered = journal.RecoverDangling();
  REQUIRE(recovered.ok());
  REQUIRE(recovered.value() == 4U);
  REQUIRE(journal.event_count() == before + 4U);

  const auto events = journal.GetEvents();
  const std::vector<std::tuple<std::string, cogito::TurnId, std::string>> expected = {
      {"session-a", 1U, "shared"},
      {"session-b", 1U, "shared"},
      {"session-a", 2U, "shared"},
      {"session-max", 1U, "max-time"}};
  const ccj::Json indeterminate_payload{{"status", "indeterminate"}};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto& event = events[before + i];
    REQUIRE(event.kind == cogito::event_kind::kToolResult);
    REQUIRE(event.session_id == std::get<0>(expected[i]));
    REQUIRE(event.turn_id == std::get<1>(expected[i]));
    REQUIRE(event.action_id == std::get<2>(expected[i]));
    REQUIRE(event.actor_type == cogito::ActorType::Core);
    REQUIRE(event.payload == indeterminate_payload);
  }
  REQUIRE(events.back().monotonic_ns ==
          std::numeric_limits<std::int64_t>::max());
  for (const auto& event : events) {
    REQUIRE(event.kind != cogito::event_kind::kAuditRecovery);
  }
  REQUIRE(journal.VerifyChain().value());

  const auto second_recovery = journal.RecoverDangling();
  REQUIRE(second_recovery.ok());
  REQUIRE(second_recovery.value() == 0U);
  REQUIRE(journal.event_count() == before + 4U);
}

TEST_CASE("RecordingAuditJournal recovery rolls back the whole batch",
          "[audit][recovery][fault]") {
  cogito::RecordingAuditJournal journal;
  REQUIRE(journal.Commit(MakeTestEvent(cogito::event_kind::kToolCallStarted,
                                       "s1", 1, "a1"))
              .ok());
  REQUIRE(journal.Commit(MakeTestEvent(cogito::event_kind::kToolCallStarted,
                                       "s1", 1, "a2"))
              .ok());
  const auto head = journal.GetHeadHash().value();

  journal.InjectFailureAt(
      4U, cogito::Error{cogito::Errc::AuditWriteFailed,
                        cogito::reason::kAuditCommitFailed, "second recovery fails"});
  const auto failed = journal.RecoverDangling();
  REQUIRE_FALSE(failed.ok());
  RequireUnchanged(journal, 2U, head, 2U);
  REQUIRE(journal.VerifyChain().value());

  journal.ClearFailureInjection();
  REQUIRE(journal.RecoverDangling().value() == 2U);
  REQUIRE(journal.event_count() == 4U);
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal queries inclusive deep-copy pages",
          "[audit][query]") {
  cogito::RecordingAuditJournal journal;
  for (int i = 1; i <= 5; ++i) {
    REQUIRE(journal.Commit(MakeTestEvent(
                               cogito::event_kind::kGateVerdict, "sess-A",
                               static_cast<cogito::TurnId>(i),
                               "act-A-" + std::to_string(i),
                               ccj::Json{{"index", i}}))
                .ok());
  }
  for (int i = 1; i <= 3; ++i) {
    REQUIRE(journal.Commit(MakeTestEvent(
                               cogito::event_kind::kGateVerdict, "sess-B",
                               static_cast<cogito::TurnId>(i),
                               "act-B-" + std::to_string(i)))
                .ok());
  }

  REQUIRE(journal.QuerySession("sess-A", 0U, 0U).value().empty());

  auto first_page = journal.QuerySession("sess-A", 0U, 3U);
  REQUIRE(first_page.ok());
  REQUIRE(first_page.value().size() == 3U);
  REQUIRE(first_page.value().front().seq == 1U);

  const auto inclusive = journal.QuerySession("sess-A", 3U, 10U);
  REQUIRE(inclusive.ok());
  REQUIRE(inclusive.value().size() == 3U);
  REQUIRE(inclusive.value()[0].seq == 3U);
  REQUIRE(inclusive.value()[1].seq == 4U);
  REQUIRE(inclusive.value()[2].seq == 5U);

  first_page.value()[0].payload["index"] = 999;
  const auto fresh = journal.QuerySession("sess-A", 1U, 1U);
  REQUIRE(fresh.ok());
  REQUIRE(fresh.value()[0].payload["index"] == 1);
  REQUIRE(journal.QuerySession("missing", 0U, 100U).value().empty());
  REQUIRE(journal.VerifyChain().value());
}

TEST_CASE("RecordingAuditJournal stress and multi-turn recovery edge cases",
          "[audit][stress][recovery]") {
  cogito::RecordingAuditJournal journal;

  SECTION("Empty journal operations") {
    REQUIRE(journal.event_count() == 0U);
    REQUIRE(journal.GetHeadSeq().value() == 0U);
    REQUIRE(journal.GetHeadHash().value() == cogito::Digest::Zero());
    REQUIRE(journal.VerifyChain().value());
    REQUIRE(journal.RecoverDangling().value() == 0U);
    REQUIRE(journal.QuerySession("any-session", 0U, 50U).value().empty());
  }

  SECTION("Sequential commit stress test: 200 events") {
    const std::string session_id = "stress-session";
    std::int64_t monotonic = 1000;

    for (std::uint64_t i = 1; i <= 200; ++i) {
      auto event = MakeTestEvent(
          (i % 2 == 1) ? cogito::event_kind::kToolCallStarted
                       : cogito::event_kind::kToolResult,
          session_id, (i + 1) / 2, "act-" + std::to_string((i + 1) / 2),
          ccj::Json{{"step", i}, {"nested", {{"val", i * 10}}}},
          monotonic, "epoch-stress");
      event.event_id = "evt-stress-" + std::to_string(i);
      monotonic += 50;

      const auto res = journal.Commit(std::move(event));
      REQUIRE(res.ok());
      REQUIRE(res.value() == i);
      REQUIRE(journal.GetHeadSeq().value() == i);
    }

    REQUIRE(journal.event_count() == 200U);
    REQUIRE(journal.VerifyChain().value());

    // All pairs matched -> 0 dangling actions
    REQUIRE(journal.RecoverDangling().value() == 0U);
    REQUIRE(journal.event_count() == 200U);

    // Query pagination across the 200 events
    const auto page1 = journal.QuerySession(session_id, 1U, 50U);
    REQUIRE(page1.ok());
    REQUIRE(page1.value().size() == 50U);
    REQUIRE(page1.value().front().seq == 1U);
    REQUIRE(page1.value().back().seq == 50U);

    const auto page4 = journal.QuerySession(session_id, 151U, 100U);
    REQUIRE(page4.ok());
    REQUIRE(page4.value().size() == 50U);
    REQUIRE(page4.value().front().seq == 151U);
    REQUIRE(page4.value().back().seq == 200U);
  }

  SECTION("Interleaved multi-session dangling recovery") {
    std::int64_t monotonic = 100;

    // session 1, turn 1: started, not completed
    auto e1 = MakeTestEvent(cogito::event_kind::kToolCallStarted, "s1", 1, "a1",
                            ccj::Json::object(), monotonic, "ep-1");
    e1.event_id = "e1";
    monotonic += 10;
    REQUIRE(journal.Commit(std::move(e1)).ok());

    // session 2, turn 1: started, completed
    auto e2 = MakeTestEvent(cogito::event_kind::kToolCallStarted, "s2", 1, "a2",
                            ccj::Json::object(), monotonic, "ep-1");
    e2.event_id = "e2";
    monotonic += 10;
    REQUIRE(journal.Commit(std::move(e2)).ok());

    auto e3 = MakeTestEvent(cogito::event_kind::kToolResult, "s2", 1, "a2",
                            ccj::Json{{"status", "ok"}}, monotonic, "ep-1");
    e3.event_id = "e3";
    monotonic += 10;
    REQUIRE(journal.Commit(std::move(e3)).ok());

    // session 1, turn 2: started, not completed
    auto e4 = MakeTestEvent(cogito::event_kind::kToolCallStarted, "s1", 2, "a4",
                            ccj::Json::object(), monotonic, "ep-1");
    e4.event_id = "e4";
    monotonic += 10;
    REQUIRE(journal.Commit(std::move(e4)).ok());

    // Total events = 4, dangling = 2 (e1, e4)
    REQUIRE(journal.event_count() == 4U);
    const auto rec = journal.RecoverDangling();
    REQUIRE(rec.ok());
    REQUIRE(rec.value() == 2U);
    REQUIRE(journal.event_count() == 6U);
    REQUIRE(journal.VerifyChain().value());

    // Verify recovery events have correct session, turn, and action IDs
    const auto all = journal.GetEvents();
    REQUIRE(all[4].session_id == "s1");
    REQUIRE(all[4].turn_id == 1U);
    REQUIRE(all[4].action_id == "a1");
    REQUIRE(all[4].kind == cogito::event_kind::kToolResult);
    REQUIRE(all[4].payload["status"] == "indeterminate");

    REQUIRE(all[5].session_id == "s1");
    REQUIRE(all[5].turn_id == 2U);
    REQUIRE(all[5].action_id == "a4");
    REQUIRE(all[5].kind == cogito::event_kind::kToolResult);
    REQUIRE(all[5].payload["status"] == "indeterminate");
  }

  SECTION("All ActorType values commit and verify cleanly") {
    const std::vector<cogito::ActorType> types = {
        cogito::ActorType::Core,     cogito::ActorType::User,
        cogito::ActorType::Operator, cogito::ActorType::System,
        cogito::ActorType::Tool};

    std::int64_t monotonic = 100;
    for (std::size_t i = 0; i < types.size(); ++i) {
      auto event = MakeTestEvent(
          cogito::event_kind::kTurnBegin, "actor-sess", 1, "",
          ccj::Json::object(), monotonic, "ep-actors");
      event.event_id = "actor-evt-" + std::to_string(i);
      event.actor_type = types[i];
      event.actor_id = std::string(cogito::ToString(types[i])) + "-id";
      monotonic += 10;
      REQUIRE(journal.Commit(std::move(event)).ok());
    }

    REQUIRE(journal.event_count() == types.size());
    REQUIRE(journal.VerifyChain().value());
  }
}

