// SPDX-License-Identifier: Apache-2.0

#include "cogito/fsm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/clock.hpp"
#include "cogito/tool.hpp"

namespace cogito {
namespace {

constexpr std::size_t kStateCount =
    static_cast<std::size_t>(State::Cancelled) + 1U;
constexpr std::size_t kEventCount =
    static_cast<std::size_t>(Event::StartNextTurn) + 1U;

constexpr bool IsKnown(State state) noexcept {
  return static_cast<std::size_t>(state) < kStateCount;
}

constexpr bool IsKnown(Event event) noexcept {
  return static_cast<std::size_t>(event) < kEventCount;
}

bool IsAuditFailureSource(State state) noexcept {
  return state == State::Infer || state == State::Propose || state == State::Gate ||
         state == State::AwaitApproval || state == State::Execute ||
         state == State::Observe;
}

bool IsCancellationSource(State state) noexcept {
  return state == State::Infer || state == State::Propose || state == State::Gate ||
         state == State::AwaitApproval || state == State::Observe;
}

void FillTransitionRecord(TransitionRecord* out,
                          bool applied,
                          State from,
                          State to,
                          Event event,
                          const std::string& cause,
                          const ActionId& action_id,
                          TurnId turn,
                          const Clock& clock) {
  if (out == nullptr) {
    return;
  }

  TransitionRecord record;
  record.applied = applied;
  record.from = from;
  record.to = to;
  record.ev = event;
  record.cause = cause;
  record.action_id = action_id;
  record.turn_id = turn;
  record.wall_utc = clock.NowUtcRfc3339();
  record.monotonic_ns = clock.MonotonicNs();
  record.process_epoch_id = ProcessEpochId();
  *out = std::move(record);
}

Error IntegrityError(std::string message, std::string detail = {}) {
  return Error{Errc::Internal, reason::kInvalidFsmState, std::move(message),
               std::move(detail)};
}

struct DumpRow {
  State from;
  Event event;
  State to;
  const char* type;
};

}  // namespace

const char* ToString(State state) noexcept {
  switch (state) {
    case State::Idle:
      return "Idle";
    case State::Infer:
      return "Infer";
    case State::Propose:
      return "Propose";
    case State::Gate:
      return "Gate";
    case State::AwaitApproval:
      return "AwaitApproval";
    case State::Execute:
      return "Execute";
    case State::Observe:
      return "Observe";
    case State::Done:
      return "Done";
    case State::Failed:
      return "Failed";
    case State::Cancelled:
      return "Cancelled";
  }
  return "unknown";
}

const char* ToString(Event event) noexcept {
  switch (event) {
    case Event::UserInput:
      return "UserInput";
    case Event::InferOk:
      return "InferOk";
    case Event::ProviderError:
      return "ProviderError";
    case Event::BudgetExhausted:
      return "BudgetExhausted";
    case Event::Cancel:
      return "Cancel";
    case Event::NoAction:
      return "NoAction";
    case Event::OneAction:
      return "OneAction";
    case Event::MultipleActions:
      return "MultipleActions";
    case Event::Deny:
      return "Deny";
    case Event::Ask:
      return "Ask";
    case Event::Allow:
      return "Allow";
    case Event::AuditError:
      return "AuditError";
    case Event::Approved:
      return "Approved";
    case Event::RejectedOrExpired:
      return "RejectedOrExpired";
    case Event::ExecOk:
      return "ExecOk";
    case Event::ExecErrorOrIndeterminate:
      return "ExecErrorOrIndeterminate";
    case Event::Continue:
      return "Continue";
    case Event::CompleteOrLimit:
      return "CompleteOrLimit";
    case Event::StartNextTurn:
      return "StartNextTurn";
  }
  return "unknown";
}

bool IsTerminal(State state) noexcept {
  return state == State::Done || state == State::Failed || state == State::Cancelled;
}

bool Fsm::ResolveUniversal(State from,
                           Event event,
                           State* to,
                           bool* noop) noexcept {
  if (to == nullptr || noop == nullptr) {
    return false;
  }

  *to = from;
  *noop = false;

  if (IsTerminal(from) &&
      (event == Event::AuditError || event == Event::Cancel)) {
    *noop = true;
    return true;
  }

  if (event == Event::Cancel &&
      (from == State::Idle || from == State::Execute)) {
    *noop = true;
    return true;
  }

  if (event == Event::AuditError && IsAuditFailureSource(from)) {
    *to = State::Failed;
    return true;
  }

  if (event == Event::Cancel && IsCancellationSource(from)) {
    *to = State::Cancelled;
    return true;
  }

  return false;
}

Error Fsm::Dispatch(Event event,
                    const std::string& cause,
                    const ActionId& action_id,
                    TurnId turn,
                    const Clock& clock,
                    TransitionRecord* out) {
  const State from = s_;
  State to = from;
  bool found = false;
  bool noop = false;

  for (const Transition& transition : kTransitions) {
    if (transition.from == from && transition.ev == event) {
      to = transition.to;
      found = true;
      break;
    }
  }

  if (!found) {
    found = ResolveUniversal(from, event, &to, &noop);
  }

  if (!found) {
    const std::string detail =
        std::string(ToString(from)) + "/" + ToString(event);
    const std::string undefined_cause = "undefined_transition:" + detail;
    s_ = State::Failed;
    FillTransitionRecord(out, true, from, State::Failed, event, undefined_cause,
                         action_id, turn, clock);
    return Error{Errc::Internal, reason::kInvalidFsmState,
                 "undefined FSM transition", detail};
  }

  if (noop) {
    FillTransitionRecord(out, false, from, from, event, cause, action_id, turn,
                         clock);
    return Error::Ok();
  }

  s_ = to;
  FillTransitionRecord(out, true, from, to, event, cause, action_id, turn, clock);
  return Error::Ok();
}

ccj::Json Fsm::DumpTable() {
  std::vector<DumpRow> rows;
  rows.reserve(kTransitions.size() + 11U);

  for (const Transition& transition : kTransitions) {
    rows.push_back(
        DumpRow{transition.from, transition.ev, transition.to, "explicit"});
  }

  for (std::size_t state_index = 0; state_index < kStateCount; ++state_index) {
    const State from = static_cast<State>(state_index);
    for (std::size_t event_index = 0; event_index < kEventCount; ++event_index) {
      const Event event = static_cast<Event>(event_index);
      State to = from;
      bool noop = false;
      if (ResolveUniversal(from, event, &to, &noop) && !noop) {
        rows.push_back(DumpRow{from, event, to, "universal"});
      }
    }
  }

  std::sort(rows.begin(), rows.end(), [](const DumpRow& lhs, const DumpRow& rhs) {
    const std::string_view lhs_from(ToString(lhs.from));
    const std::string_view rhs_from(ToString(rhs.from));
    if (lhs_from != rhs_from) {
      return lhs_from < rhs_from;
    }

    const std::string_view lhs_event(ToString(lhs.event));
    const std::string_view rhs_event(ToString(rhs.event));
    if (lhs_event != rhs_event) {
      return lhs_event < rhs_event;
    }

    const std::string_view lhs_to(ToString(lhs.to));
    const std::string_view rhs_to(ToString(rhs.to));
    if (lhs_to != rhs_to) {
      return lhs_to < rhs_to;
    }
    return std::string_view(lhs.type) < std::string_view(rhs.type);
  });

  ccj::Json output = ccj::Json::array();
  for (const DumpRow& row : rows) {
    ccj::Json item = ccj::Json::object();
    item["from"] = ToString(row.from);
    item["ev"] = ToString(row.event);
    item["to"] = ToString(row.to);
    item["type"] = row.type;
    output.push_back(std::move(item));
  }
  return output;
}

Event Fsm::MapToolResultStatus(ToolResultStatus status) noexcept {
  switch (status) {
    case ToolResultStatus::Ok:
      return Event::ExecOk;
    case ToolResultStatus::Error:
    case ToolResultStatus::Timeout:
    case ToolResultStatus::Cancelled:
    case ToolResultStatus::Indeterminate:
      return Event::ExecErrorOrIndeterminate;
  }
  return Event::ExecErrorOrIndeterminate;
}

Error Fsm::VerifyTableIntegrity() {
  for (const Transition& transition : kTransitions) {
    if (!IsKnown(transition.from) || !IsKnown(transition.ev) ||
        !IsKnown(transition.to)) {
      return IntegrityError("FSM table contains an unknown enum value");
    }
    if (transition.ev == Event::AuditError || transition.ev == Event::Cancel) {
      return IntegrityError("explicit transition overlaps a universal event",
                            std::string(ToString(transition.from)) + "/" +
                                ToString(transition.ev));
    }

    State universal_to = transition.from;
    bool noop = false;
    if (ResolveUniversal(transition.from, transition.ev, &universal_to, &noop)) {
      return IntegrityError("explicit transition overlaps a universal rule",
                            std::string(ToString(transition.from)) + "/" +
                                ToString(transition.ev));
    }
  }

  for (std::size_t i = 0; i < kTransitions.size(); ++i) {
    for (std::size_t j = i + 1U; j < kTransitions.size(); ++j) {
      if (kTransitions[i].from == kTransitions[j].from &&
          kTransitions[i].ev == kTransitions[j].ev) {
        return IntegrityError("duplicate explicit FSM transition",
                              std::string(ToString(kTransitions[i].from)) + "/" +
                                  ToString(kTransitions[i].ev));
      }
    }
  }

  std::array<bool, kStateCount> reached{};
  std::queue<State> pending;
  reached[static_cast<std::size_t>(State::Idle)] = true;
  pending.push(State::Idle);

  const auto enqueue = [&reached, &pending](State state) {
    const std::size_t index = static_cast<std::size_t>(state);
    if (!reached[index]) {
      reached[index] = true;
      pending.push(state);
    }
  };

  while (!pending.empty()) {
    const State from = pending.front();
    pending.pop();

    for (const Transition& transition : kTransitions) {
      if (transition.from == from) {
        enqueue(transition.to);
      }
    }

    for (std::size_t event_index = 0; event_index < kEventCount; ++event_index) {
      State to = from;
      bool noop = false;
      if (ResolveUniversal(from, static_cast<Event>(event_index), &to, &noop) &&
          !noop) {
        enqueue(to);
      }
    }
  }

  for (std::size_t state_index = 0; state_index < kStateCount; ++state_index) {
    const State state = static_cast<State>(state_index);
    if (!IsTerminal(state) && !reached[state_index]) {
      return IntegrityError("non-terminal FSM state is unreachable from Idle",
                            ToString(state));
    }
  }

  for (std::size_t state_index = 0; state_index < kStateCount; ++state_index) {
    const State terminal = static_cast<State>(state_index);
    if (!IsTerminal(terminal)) {
      continue;
    }

    std::size_t reset_count = 0;
    for (const Transition& transition : kTransitions) {
      if (transition.from == terminal && transition.ev == Event::StartNextTurn &&
          transition.to == State::Idle) {
        ++reset_count;
      }
    }
    if (reset_count != 1U) {
      return IntegrityError("terminal FSM state lacks one StartNextTurn reset",
                            ToString(terminal));
    }
  }

  return Error::Ok();
}

}  // namespace cogito
