// SPDX-License-Identifier: Apache-2.0

#include "cogito/audit.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cogito {
namespace {

Error MissingField(const char* message) {
  return Error{Errc::InvalidArgument, reason::kInputMissingField, message};
}

Error AuditWriteError(const char* message) {
  return Error{Errc::AuditWriteFailed, reason::kAuditCommitFailed, message};
}

Error AuditChainError(const char* message) {
  return Error{Errc::AuditChainBroken, reason::kAuditCommitFailed, message};
}

bool IsKnownActorType(ActorType type) noexcept {
  switch (type) {
    case ActorType::Core:
    case ActorType::User:
    case ActorType::Operator:
    case ActorType::System:
    case ActorType::Tool:
      return true;
  }
  return false;
}

bool AllowsEmptyActionId(const std::string& kind) noexcept {
  return kind == event_kind::kTurnBegin || kind == event_kind::kTurnEnd ||
         kind == event_kind::kInferBegin || kind == event_kind::kInferEnd ||
         kind == event_kind::kAuditRecovery || kind == "operator_ack";
}

Error ValidateEventShape(const AuditEvent& event) {
  if (event.event_id.empty()) {
    return MissingField("event_id must not be empty");
  }
  if (event.session_id.empty()) {
    return MissingField("session_id must not be empty");
  }
  if (event.turn_id == 0U) {
    return MissingField("turn_id must be at least 1");
  }
  if (event.wall_time_utc.empty()) {
    return MissingField("wall_time_utc must not be empty");
  }
  if (event.monotonic_ns < 0) {
    return Error{Errc::InvalidArgument, reason::kInputMissingField,
                 "monotonic_ns must not be negative"};
  }
  if (event.process_epoch_id.empty()) {
    return MissingField("process_epoch_id must not be empty");
  }
  if (event.kind.empty()) {
    return MissingField("kind must not be empty");
  }
  if (!AllowsEmptyActionId(event.kind) && event.action_id.empty()) {
    return MissingField("action_id is required for this event kind");
  }
  if (!IsKnownActorType(event.actor_type)) {
    return Error{Errc::InvalidArgument, reason::kInputMissingField,
                 "actor_type is invalid"};
  }
  if (event.schema_version != 1U) {
    return Error{Errc::InvalidArgument, reason::kInputMissingField,
                 "schema_version must be 1"};
  }
  return Error::Ok();
}

Result<Digest> ComputeEventDigest(const AuditEvent& event) {
  return ComputeAuditDigest(
      event.prev_hash, event.event_id, event.session_id, event.turn_id,
      event.action_id, event.wall_time_utc,
      static_cast<std::uint64_t>(event.monotonic_ns), event.process_epoch_id,
      event.kind, static_cast<std::uint64_t>(event.actor_type), event.actor_id,
      event.payload, event.schema_version);
}

Error EffectiveInjectedError(const Error& injected) {
  if (injected.ok()) {
    return AuditWriteError("Injected audit commit failure");
  }
  return injected;
}

using RecoveryKey = std::tuple<SessionId, TurnId, ActionId>;

struct PendingStart {
  AuditEvent event;
  bool open = true;
};

}  // namespace

Result<std::uint64_t> RecordingAuditJournal::Commit(AuditEvent event) {
  try {
    if (failure_index_ > 0U &&
        next_seq_ == static_cast<std::uint64_t>(failure_index_)) {
      return EffectiveInjectedError(injected_error_);
    }
    if (next_seq_ == std::numeric_limits<std::uint64_t>::max()) {
      return AuditWriteError("Audit sequence space is exhausted");
    }

    if (Error validation_error = ValidateEventShape(event)) {
      return validation_error;
    }
    for (const auto& existing : events_) {
      if (existing.event_id == event.event_id) {
        return Error{Errc::DuplicateKey, reason::kInputDuplicateKey,
                     "event_id must be unique"};
      }
      if (existing.process_epoch_id == event.process_epoch_id &&
          existing.monotonic_ns > event.monotonic_ns) {
        return Error{Errc::InvalidArgument, reason::kInputMissingField,
                     "monotonic_ns must not regress within a process epoch"};
      }
    }

    event.seq = next_seq_;
    event.prev_hash = head_hash_;
    auto digest = ComputeEventDigest(event);
    if (!digest.ok()) {
      return digest.error();
    }
    event.hash = digest.value();

    const std::uint64_t assigned_seq = event.seq;
    events_.push_back(std::move(event));

    // Publish the new head only after the append succeeds. This ordering keeps
    // all externally observable state unchanged if allocation throws.
    head_hash_ = events_.back().hash;
    ++next_seq_;
    return assigned_seq;
  } catch (const std::bad_alloc&) {
    return AuditWriteError("Out of memory while committing an audit event");
  } catch (...) {
    return AuditWriteError("Unexpected audit commit failure");
  }
}

Result<Digest> RecordingAuditJournal::GetHeadHash() const {
  return head_hash_;
}

Result<std::uint64_t> RecordingAuditJournal::GetHeadSeq() const {
  return events_.empty() ? 0U : events_.back().seq;
}

Result<std::vector<AuditEvent>> RecordingAuditJournal::QuerySession(
    const SessionId& session_id,
    std::uint64_t from_seq,
    std::size_t limit) const {
  try {
    std::vector<AuditEvent> result;
    if (limit == 0U) {
      return result;
    }
    result.reserve(limit < events_.size() ? limit : events_.size());
    for (const auto& event : events_) {
      if (event.session_id == session_id && event.seq >= from_seq) {
        result.push_back(event);
        if (result.size() == limit) {
          break;
        }
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return AuditWriteError("Out of memory while querying audit events");
  } catch (...) {
    return AuditWriteError("Unexpected audit query failure");
  }
}

Result<bool> RecordingAuditJournal::VerifyChain() const {
  try {
    if (events_.empty()) {
      if (head_hash_ != Digest::Zero() || next_seq_ != 1U) {
        return AuditChainError("Empty audit chain has inconsistent state");
      }
      return true;
    }

    Digest expected_prev = Digest::Zero();
    std::uint64_t expected_seq = 1U;
    std::set<std::string> event_ids;
    std::map<std::string, std::int64_t> epoch_last_monotonic;

    for (const auto& event : events_) {
      if (event.seq != expected_seq) {
        return AuditChainError("Audit sequence is not contiguous");
      }
      if (Error validation_error = ValidateEventShape(event)) {
        (void)validation_error;
        return AuditChainError("Audit event shape is invalid");
      }
      if (!event_ids.insert(event.event_id).second) {
        return AuditChainError("Audit event_id is duplicated");
      }
      if (event.prev_hash != expected_prev) {
        return AuditChainError("Audit prev_hash does not match the prior event");
      }

      const auto found_epoch = epoch_last_monotonic.find(event.process_epoch_id);
      if (found_epoch != epoch_last_monotonic.end() &&
          event.monotonic_ns < found_epoch->second) {
        return AuditChainError("Audit monotonic time regressed within an epoch");
      }
      epoch_last_monotonic[event.process_epoch_id] = event.monotonic_ns;

      auto digest = ComputeEventDigest(event);
      if (!digest.ok() || digest.value() != event.hash) {
        return AuditChainError("Audit event hash does not match its canonical fields");
      }

      expected_prev = event.hash;
      ++expected_seq;
    }

    if (expected_prev != head_hash_) {
      return AuditChainError("Audit head hash does not match the final event");
    }
    if (next_seq_ != expected_seq) {
      return AuditChainError("Audit next sequence is inconsistent");
    }
    return true;
  } catch (const std::bad_alloc&) {
    return AuditChainError("Out of memory while verifying the audit chain");
  } catch (...) {
    return AuditChainError("Unexpected audit chain verification failure");
  }
}

Result<std::size_t> RecordingAuditJournal::RecoverDangling() {
  try {
    auto verified = VerifyChain();
    if (!verified.ok()) {
      return verified.error();
    }
    if (!verified.value()) {
      return AuditChainError("Audit chain verification failed before recovery");
    }

    std::vector<PendingStart> starts;
    std::map<RecoveryKey, std::vector<std::size_t>> pending_by_key;
    std::map<std::string, std::int64_t> epoch_last_monotonic;

    for (const auto& event : events_) {
      const auto last = epoch_last_monotonic.find(event.process_epoch_id);
      if (last == epoch_last_monotonic.end() || event.monotonic_ns > last->second) {
        epoch_last_monotonic[event.process_epoch_id] = event.monotonic_ns;
      }

      if (event.kind == event_kind::kToolCallStarted) {
        const std::size_t index = starts.size();
        starts.push_back(PendingStart{event, true});
        pending_by_key[RecoveryKey{event.session_id, event.turn_id,
                                   event.action_id}]
            .push_back(index);
      } else if (event.kind == event_kind::kToolResult) {
        auto pending = pending_by_key.find(
            RecoveryKey{event.session_id, event.turn_id, event.action_id});
        if (pending != pending_by_key.end() && !pending->second.empty()) {
          const std::size_t index = pending->second.back();
          pending->second.pop_back();
          starts[index].open = false;
        }
      }
    }

    std::size_t dangling_count = 0U;
    for (const auto& start : starts) {
      if (start.open) {
        ++dangling_count;
      }
    }
    if (dangling_count == 0U) {
      return 0U;
    }

    // Stage every generated event in a private copy. Recovery becomes visible
    // only after all UUID, digest, allocation, and failure-injection checks pass.
    std::vector<AuditEvent> staged_events = events_;
    Digest staged_head = head_hash_;
    std::uint64_t staged_next_seq = next_seq_;
    std::set<std::string> staged_event_ids;
    for (const auto& event : staged_events) {
      staged_event_ids.insert(event.event_id);
    }

    for (const auto& start : starts) {
      if (!start.open) {
        continue;
      }
      if (failure_index_ > 0U &&
          staged_next_seq == static_cast<std::uint64_t>(failure_index_)) {
        return EffectiveInjectedError(injected_error_);
      }
      if (staged_next_seq == std::numeric_limits<std::uint64_t>::max()) {
        return AuditWriteError("Audit sequence space is exhausted during recovery");
      }

      auto event_id = NewUuidV4();
      if (!event_id.ok()) {
        return event_id.error();
      }

      AuditEvent recovered;
      recovered.seq = staged_next_seq;
      recovered.event_id = std::move(event_id).take();
      recovered.session_id = start.event.session_id;
      recovered.turn_id = start.event.turn_id;
      recovered.action_id = start.event.action_id;
      recovered.wall_time_utc = start.event.wall_time_utc;
      recovered.monotonic_ns =
          epoch_last_monotonic[start.event.process_epoch_id];
      recovered.process_epoch_id = start.event.process_epoch_id;
      recovered.kind = event_kind::kToolResult;
      recovered.actor_type = ActorType::Core;
      recovered.actor_id = "audit-recovery";
      recovered.payload = ccj::Json{{"status", "indeterminate"}};
      recovered.schema_version = 1U;
      recovered.prev_hash = staged_head;

      if (Error validation_error = ValidateEventShape(recovered)) {
        return validation_error;
      }
      if (!staged_event_ids.insert(recovered.event_id).second) {
        return Error{Errc::DuplicateKey, reason::kInputDuplicateKey,
                     "Generated recovery event_id is duplicated"};
      }
      auto digest = ComputeEventDigest(recovered);
      if (!digest.ok()) {
        return digest.error();
      }
      recovered.hash = digest.value();
      staged_events.push_back(std::move(recovered));
      staged_head = staged_events.back().hash;
      ++staged_next_seq;
    }

    events_.swap(staged_events);
    head_hash_ = staged_head;
    next_seq_ = staged_next_seq;
    return dangling_count;
  } catch (const std::bad_alloc&) {
    return AuditWriteError("Out of memory while recovering the audit journal");
  } catch (...) {
    return AuditWriteError("Unexpected audit recovery failure");
  }
}

void RecordingAuditJournal::InjectFailureAt(std::size_t commit_index,
                                             Error error) noexcept {
  failure_index_ = commit_index;
  injected_error_ = std::move(error);
}

void RecordingAuditJournal::ClearFailureInjection() noexcept {
  failure_index_ = 0U;
  injected_error_ = Error::Ok();
}

std::vector<AuditEvent> RecordingAuditJournal::GetEvents() const {
  return events_;
}

std::size_t RecordingAuditJournal::event_count() const noexcept {
  return events_.size();
}

}  // namespace cogito
