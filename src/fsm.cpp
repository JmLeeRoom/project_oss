// SPDX-License-Identifier: Apache-2.0

#include "cogito/fsm.hpp"

#include <array>
#include <cstddef>
#include <queue>
#include <string>
#include <utility>

#include "cogito/clock.hpp"

namespace cogito {
namespace {

constexpr std::array<State, 10> kAllStates{{
    State::Idle,
    State::Infer,
    State::Propose,
    State::Gate,
    State::AwaitApproval,
    State::Execute,
    State::Observe,
    State::Done,
    State::Failed,
    State::Cancelled,
}};

constexpr std::array<Event, 19> kAllEvents{{
    Event::UserInput,
    Event::InferOk,
    Event::ProviderError,
    Event::BudgetExhausted,
    Event::Cancel,
    Event::NoAction,
    Event::OneAction,
    Event::MultipleActions,
    Event::Deny,
    Event::Ask,
    Event::Allow,
    Event::AuditError,
    Event::Approved,
    Event::RejectedOrExpired,
    Event::ExecOk,
    Event::ExecErrorOrIndeterminate,
    Event::Continue,
    Event::CompleteOrLimit,
    Event::StartNextTurn,
}};

constexpr std::array<State, 6> kR1States{{
    State::Infer,
    State::Propose,
    State::Gate,
    State::AwaitApproval,
    State::Execute,
    State::Observe,
}};

constexpr std::array<State, 5> kR2States{{
    State::Infer,
    State::Propose,
    State::Gate,
    State::AwaitApproval,
    State::Observe,
}};

constexpr std::array<State, 3> kTerminalStates{{
    State::Done,
    State::Failed,
    State::Cancelled,
}};

constexpr std::array<Event, 2> kR4Events{{
    Event::AuditError,
    Event::Cancel,
}};

constexpr std::size_t StateIndex(State state) noexcept {
  return static_cast<std::size_t>(state);
}

constexpr bool IsKnownState(State state) noexcept {
  return StateIndex(state) < kAllStates.size();
}

bool Contains(const std::array<State, 6>& states, State value) noexcept {
  for (State state : states) {
    if (state == value) {
      return true;
    }
  }
  return false;
}

bool Contains(const std::array<State, 5>& states, State value) noexcept {
  for (State state : states) {
    if (state == value) {
      return true;
    }
  }
  return false;
}

void FillRecord(TransitionRecord* out,
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
  out->applied = applied;
  out->from = from;
  out->to = to;
  out->ev = event;
  out->cause = cause;
  out->action_id = action_id;
  out->turn_id = turn;
  out->wall_utc = clock.NowUtcRfc3339();
  out->monotonic_ns = clock.MonotonicNs();
  out->process_epoch_id.clear();
}

ccj::Json TransitionJson(State from,
                         Event event,
                         State to,
                         const char* kind,
                         const char* rule) {
  return ccj::Json{{"event", ToString(event)},
                   {"from", ToString(from)},
                   {"kind", kind},
                   {"rule", rule},
                   {"to", ToString(to)}};
}

Error IntegrityError(std::string message, std::string detail) {
  return Error{Errc::Internal, {}, std::move(message), std::move(detail)};
}

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
  return "UnknownState";
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
  return "UnknownEvent";
}

bool IsTerminal(State state) noexcept {
  return state == State::Done || state == State::Failed || state == State::Cancelled;
}

bool Fsm::ResolveUniversal(State from, Event event, State* to, bool* noop) noexcept {
  *noop = false;

  if (from == State::Idle && event == Event::Cancel) {
    *to = State::Idle;
    *noop = true;
    return true;
  }

  if (IsTerminal(from) && (event == Event::AuditError || event == Event::Cancel)) {
    *to = from;
    *noop = true;
    return true;
  }

  if (event == Event::AuditError && Contains(kR1States, from)) {
    *to = State::Failed;
    return true;
  }

  if (event == Event::Cancel && Contains(kR2States, from)) {
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
  const State previous = s_;
  State target = previous;
  bool found = false;
  bool noop = false;

  for (const Transition& transition : kTransitions) {
    if (transition.from == previous && transition.ev == event) {
      target = transition.to;
      found = true;
      break;
    }
  }

  if (!found) {
    found = ResolveUniversal(previous, event, &target, &noop);
  }

  if (!found) {
    s_ = State::Failed;
    const std::string recorded_cause = cause.empty() ? "undefined_transition" : cause;
    FillRecord(out, true, previous, State::Failed, event, recorded_cause, action_id, turn, clock);
    return Error{Errc::Internal,
                 {},
                 "정의되지 않은 상태 전이입니다.",
                 std::string(ToString(previous)) + "/" + ToString(event)};
  }

  if (noop) {
    FillRecord(out, false, previous, previous, event, cause, action_id, turn, clock);
    return Error::Ok();
  }

  s_ = target;
  FillRecord(out, true, previous, target, event, cause, action_id, turn, clock);
  return Error::Ok();
}

ccj::Json Fsm::DumpTable() {
  ccj::Json result = ccj::Json::array();

  for (const Transition& transition : kTransitions) {
    result.push_back(
        TransitionJson(transition.from, transition.ev, transition.to, "explicit", "explicit"));
  }

  result.push_back(TransitionJson(State::Idle, Event::Cancel, State::Idle, "universal", "R0"));

  for (State state : kR1States) {
    result.push_back(TransitionJson(state, Event::AuditError, State::Failed, "universal", "R1"));
  }

  for (State state : kR2States) {
    result.push_back(TransitionJson(state, Event::Cancel, State::Cancelled, "universal", "R2"));
  }

  for (State state : kTerminalStates) {
    for (Event event : kR4Events) {
      result.push_back(TransitionJson(state, event, state, "universal", "R4"));
    }
  }

  return result;
}

Error Fsm::VerifyTableIntegrity() {
  for (std::size_t i = 0; i < kTransitions.size(); ++i) {
    const Transition& current = kTransitions[i];
    if (!IsKnownState(current.from) || !IsKnownState(current.to)) {
      return IntegrityError("전이표에 알 수 없는 상태가 있습니다.", ToString(current.from));
    }
    if (current.ev == Event::AuditError || current.ev == Event::Cancel) {
      return IntegrityError("명시 전이가 보편 규칙과 충돌합니다.", ToString(current.ev));
    }
    for (std::size_t j = i + 1; j < kTransitions.size(); ++j) {
      if (current.from == kTransitions[j].from && current.ev == kTransitions[j].ev) {
        return IntegrityError("중복된 명시 전이가 있습니다.",
                              std::string(ToString(current.from)) + "/" + ToString(current.ev));
      }
    }
  }

  std::array<bool, kAllStates.size()> reachable{};
  std::queue<State> pending;
  reachable[StateIndex(State::Idle)] = true;
  pending.push(State::Idle);

  const auto enqueue = [&reachable, &pending](State state) {
    const std::size_t index = StateIndex(state);
    if (index < reachable.size() && !reachable[index]) {
      reachable[index] = true;
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

    for (Event event : kAllEvents) {
      State target = from;
      bool noop = false;
      if (ResolveUniversal(from, event, &target, &noop) && !noop) {
        enqueue(target);
      }
    }
  }

  for (State state : kR1States) {
    if (!reachable[StateIndex(state)]) {
      return IntegrityError("Idle에서 비종료 상태에 도달할 수 없습니다.", ToString(state));
    }
  }

  for (State terminal : kTerminalStates) {
    bool returns_to_idle = false;
    for (const Transition& transition : kTransitions) {
      if (transition.from == terminal && transition.ev == Event::StartNextTurn &&
          transition.to == State::Idle) {
        returns_to_idle = true;
        break;
      }
    }
    if (!returns_to_idle) {
      return IntegrityError("종료 상태가 다음 턴의 Idle로 복귀하지 못합니다.", ToString(terminal));
    }
  }

  return Error::Ok();
}

}  // namespace cogito
