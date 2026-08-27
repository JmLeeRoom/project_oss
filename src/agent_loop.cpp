// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 에이전트 핵심 루프 (AgentLoop & FakeProvider) 구현

#include "cogito/agent_loop.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cogito/action.hpp"
#include "cogito/audit.hpp"
#include "cogito/budget.hpp"
#include "cogito/canonical_json.hpp"
#include "cogito/clock.hpp"
#include "cogito/context_compactor.hpp"
#include "cogito/conversation.hpp"
#include "cogito/digest.hpp"
#include "cogito/fsm.hpp"
#include "cogito/identity.hpp"
#include "cogito/ids.hpp"
#include "cogito/inference.hpp"
#include "cogito/invoker.hpp"
#include "cogito/permission_gate.hpp"
#include "cogito/permit.hpp"
#include "cogito/policy.hpp"
#include "cogito/registry.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {
namespace {

std::string DumpJson(const ccj::Json& j) {
  auto res = ccj::Serialize(j);
  if (res.ok()) {
    return res.value();
  }
  return j.dump();
}

std::string GenerateUuid() {
  auto res = NewUuidV4();
  return res.ok() ? res.value() : "";
}

thread_local TurnOutcome* t_active_outcome = nullptr;

struct OutcomeScope {
  TurnOutcome* prev;
  explicit OutcomeScope(TurnOutcome* out) : prev(t_active_outcome) {
    t_active_outcome = out;
  }
  ~OutcomeScope() {
    t_active_outcome = prev;
  }
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FakeProvider Implementation
// ─────────────────────────────────────────────────────────────────────────────
Result<InferenceResponse> FakeProvider::Complete(const InferenceRequest& req) {
  if (req.cancel.IsCancelled()) {
    InferenceResponse resp;
    resp.finish = FinishReason::Cancelled;
    resp.identity = identity_;
    return resp;
  }

  if (!errors_.empty()) {
    Error err = errors_.front();
    errors_.erase(errors_.begin());
    return err;
  }

  if (!responses_.empty()) {
    InferenceResponse resp = std::move(responses_.front());
    responses_.erase(responses_.begin());
    ++call_count_;
    if (resp.identity.provider_id.empty()) {
      resp.identity = identity_;
    }
    if (resp.raw_digest.is_zero()) {
      auto dres = Sha256(resp.text);
      resp.raw_digest = dres.ok() ? dres.value() : Digest::Zero();
    }
    return resp;
  }

  InferenceResponse resp;
  resp.text = "OK";
  resp.finish = FinishReason::Stop;
  resp.identity = identity_;
  auto dres = Sha256(resp.text);
  resp.raw_digest = dres.ok() ? dres.value() : Digest::Zero();
  ++call_count_;
  return resp;
}

// ─────────────────────────────────────────────────────────────────────────────
// AgentLoop Implementation
// ─────────────────────────────────────────────────────────────────────────────
AgentLoop::AgentLoop(AgentDeps deps,
                     Subject subject,
                     ExecutionMode mode,
                     SessionId session_id,
                     AgentLoopConfig config)
    : d_(std::move(deps)),
      subject_(std::move(subject)),
      mode_(mode),
      session_(std::move(session_id)),
      config_(config) {}

void AgentLoop::RequestCancel() noexcept {
  cancel_flag_.store(true, std::memory_order_release);
}

Error AgentLoop::AcknowledgeIndeterminate(const Subject& operator_subject,
                                          const std::string& note) {
  bool expected = false;
  if (!in_call_.compare_exchange_strong(expected, true)) {
    return Error{Errc::WrongThread, "single_thread_violation",
                 "AgentLoop concurrent call detected"};
  }
  struct Guard {
    std::atomic<bool>& flag;
    ~Guard() { flag.store(false, std::memory_order_release); }
  } call_guard{in_call_};

  AuditEvent e;
  e.event_id = GenerateUuid();
  e.session_id = session_;
  e.turn_id = (turn_ == 0) ? 1 : turn_;
  e.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
  e.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
  e.process_epoch_id = ProcessEpochId();
  e.kind = event_kind::kAuditRecovery;
  e.actor_type = ActorType::Operator;
  e.actor_id = operator_subject.subject_id;
  e.payload = ccj::Json{
      {"action", "operator_ack"},
      {"note", note},
      {"operator", operator_subject.subject_id},
      {"unlocked_count", static_cast<int>(indeterminate_locks_.size())}
  };

  if (d_.audit) {
    auto res = d_.audit->Commit(e);
    if (!res.ok()) {
      return res.error();
    }
  }

  indeterminate_locks_.clear();
  line_write_lockdown_ = false;
  return Error::Ok();
}

Error AgentLoop::SealSession() {
  bool expected = false;
  if (!in_call_.compare_exchange_strong(expected, true)) {
    return Error{Errc::WrongThread, "single_thread_violation",
                 "AgentLoop concurrent call detected"};
  }
  struct Guard {
    std::atomic<bool>& flag;
    ~Guard() { flag.store(false, std::memory_order_release); }
  } call_guard{in_call_};

  if (sealed_) {
    return Error::Ok();
  }

  AuditEvent e;
  e.event_id = GenerateUuid();
  e.session_id = session_;
  e.turn_id = (turn_ == 0) ? 1 : turn_;
  e.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
  e.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
  e.process_epoch_id = ProcessEpochId();
  e.kind = event_kind::kAuditRecovery;
  e.actor_type = ActorType::System;
  e.actor_id = "core:agent_loop";
  e.payload = ccj::Json{
      {"action", "seal_session"},
      {"reason", "session_sealed"},
      {"last_state", ToString(fsm_.current())}
  };

  if (d_.audit) {
    auto res = d_.audit->Commit(e);
    if (!res.ok()) {
      return res.error();
    }
  }

  sealed_ = true;
  finalize_pending_ = false;
  turn_active_ = false;
  return Error::Ok();
}

Error AgentLoop::RetryFinalize() {
  bool expected = false;
  if (!in_call_.compare_exchange_strong(expected, true)) {
    return Error{Errc::WrongThread, "single_thread_violation",
                 "AgentLoop concurrent call detected"};
  }
  struct Guard {
    std::atomic<bool>& flag;
    ~Guard() { flag.store(false, std::memory_order_release); }
  } call_guard{in_call_};

  if (!finalize_pending_) {
    return Error::Ok();
  }

  TurnOutcome tmp;
  tmp.status = TurnStatus::Failed;
  tmp.final_state = fsm_.current();
  return Finalize(&tmp);
}

Error AgentLoop::Fire(Event ev, const std::string& cause, const ActionId& aid) {
  TransitionRecord rec;
  if (!d_.clock) {
    return Error{Errc::Internal, reason::kInvalidFsmState, "Clock is null"};
  }
  Error err = fsm_.Dispatch(ev, cause, aid, turn_, *d_.clock, &rec);
  if (err.ok() && rec.applied && t_active_outcome != nullptr) {
    t_active_outcome->transitions.push_back(rec);
  }
  return err;
}

Error AgentLoop::Finalize(TurnOutcome* out) {
  if (!turn_active_) {
    return Error::Ok();
  }

  out->final_state = fsm_.current();

  AuditEvent e;
  e.event_id = GenerateUuid();
  e.session_id = session_;
  e.turn_id = turn_;
  e.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
  e.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
  e.process_epoch_id = ProcessEpochId();
  e.kind = event_kind::kTurnEnd;
  e.actor_type = ActorType::System;
  e.actor_id = "core:agent_loop";
  e.payload = ccj::Json{
      {"final_state", ToString(fsm_.current())},
      {"status", ToString(out->status)},
      {"had_indeterminate", out->had_indeterminate},
      {"prompt_tokens", out->usage.prompt_tokens},
      {"completion_tokens", out->usage.completion_tokens},
      {"transition_count", static_cast<int>(out->transitions.size())},
      {"verdict_count", static_cast<int>(out->verdicts.size())}
  };

  if (d_.audit) {
    auto res = d_.audit->Commit(e);
    if (!res.ok()) {
      finalize_pending_ = true;
      return Error{Errc::AuditWriteFailed, reason::kAuditCommitFailed,
                   "Failed to commit turn_end audit event", res.error().detail};
    }
  }

  cancel_flag_.store(false, std::memory_order_release);
  finalize_pending_ = false;
  turn_active_ = false;
  return Error::Ok();
}

Result<TurnOutcome> AgentLoop::RunTurn(const std::string& user_input) {
  bool expected = false;
  if (!in_call_.compare_exchange_strong(expected, true)) {
    return Error{Errc::WrongThread, "single_thread_violation",
                 "AgentLoop concurrent call detected"};
  }
  struct Guard {
    std::atomic<bool>& flag;
    ~Guard() { flag.store(false, std::memory_order_release); }
  } call_guard{in_call_};

  if (sealed_) {
    return Error{Errc::TurnSealed, "session_sealed", "Session is sealed"};
  }
  if (finalize_pending_) {
    return Error{Errc::AuditWriteFailed, reason::kAuditCommitFailed,
                 "Previous turn finalize pending. Must RetryFinalize or SealSession."};
  }
  if (turn_active_) {
    return Error{Errc::Internal, "turn_already_active", "Turn is already active"};
  }
  if (config_.max_turns > 0 && turn_ >= config_.max_turns) {
    return Error{Errc::TurnSealed, "turn_limit_reached",
                 "Session reached max_turns limit"};
  }

  TurnOutcome outcome;
  OutcomeScope outcome_scope(&outcome);

  if (IsTerminal(fsm_.current())) {
    if (!d_.clock) {
      return Error{Errc::Internal, reason::kInvalidFsmState, "Clock is null"};
    }
    TransitionRecord rec;
    Error reset_err =
        fsm_.Dispatch(Event::StartNextTurn, "start_turn", ActionId{}, turn_, *d_.clock, &rec);
    if (!reset_err.ok()) {
      return reset_err;
    }
    if (rec.applied) {
      outcome.transitions.push_back(rec);
    }
  }

  if (fsm_.current() != State::Idle) {
    return Error{Errc::Internal, reason::kInvalidFsmState,
                 "FSM must be in Idle state to start turn"};
  }

  pending_action_ = ActionRequest{};
  pending_approval_id_.clear();
  gate_reentry_.clear();

  ++turn_;
  turn_active_ = true;

  // Commit turn_begin BEFORE FSM transition
  AuditEvent begin_ev;
  begin_ev.event_id = GenerateUuid();
  begin_ev.session_id = session_;
  begin_ev.turn_id = turn_;
  begin_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
  begin_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
  begin_ev.process_epoch_id = ProcessEpochId();
  begin_ev.kind = event_kind::kTurnBegin;
  begin_ev.actor_type = ActorType::User;
  begin_ev.actor_id = subject_.subject_id;
  begin_ev.payload = ccj::Json{
      {"user_input_bytes", user_input.size()},
      {"mode", ToString(mode_)},
      {"turn_id", turn_}
  };

  if (d_.audit) {
    auto res = d_.audit->Commit(begin_ev);
    if (!res.ok()) {
      turn_active_ = false;
      --turn_;
      return res.error();
    }
  }

  Error fire_err = Fire(Event::UserInput, "user_input");
  if (!fire_err.ok()) {
    outcome.status = TurnStatus::Failed;
    Finalize(&outcome);
    return outcome;
  }

  if (d_.conv) {
    Message user_msg;
    user_msg.role = Role::User;
    user_msg.content = user_input;
    user_msg.untrusted = false;
    user_msg.provenance = "user:" + subject_.subject_id;
    d_.conv->AddMessage(std::move(user_msg));
  }

  if (d_.budget && d_.clock) {
    d_.budget->StartTurn(d_.clock->MonotonicNs());
  }

  return Drive();
}

Result<TurnOutcome> AgentLoop::ResumeTurn() {
  bool expected = false;
  if (!in_call_.compare_exchange_strong(expected, true)) {
    return Error{Errc::WrongThread, "single_thread_violation",
                 "AgentLoop concurrent call detected"};
  }
  struct Guard {
    std::atomic<bool>& flag;
    ~Guard() { flag.store(false, std::memory_order_release); }
  } call_guard{in_call_};

  if (sealed_) {
    return Error{Errc::TurnSealed, "session_sealed", "Session is sealed"};
  }
  if (finalize_pending_) {
    return Error{Errc::AuditWriteFailed, reason::kAuditCommitFailed,
                 "Previous turn finalize pending."};
  }
  if (!turn_active_ || fsm_.current() != State::AwaitApproval) {
    return Error{Errc::Internal, reason::kInvalidFsmState,
                 "Cannot resume turn: not in AwaitApproval state"};
  }

  TurnOutcome outcome;
  OutcomeScope outcome_scope(&outcome);

  auto act_dig_res = ComputeActionDigest(
      pending_action_.session_id, pending_action_.turn_id,
      pending_action_.action_id, pending_action_.tool_name,
      pending_action_.arguments);
  Digest action_digest = act_dig_res.ok() ? act_dig_res.value() : Digest::Zero();
  Digest policy_digest = d_.gate ? d_.gate->policy().policy_digest() : Digest::Zero();
  Digest registry_digest = d_.registry ? d_.registry->registry_digest() : Digest::Zero();
  auto scope_dig_res = ComputePermitDigest(
      action_digest, subject_.subject_id,
      static_cast<std::uint64_t>(mode_), policy_digest, registry_digest);
  Digest scope_digest = scope_dig_res.ok() ? scope_dig_res.value() : Digest::Zero();
  std::int64_t now_ns = d_.clock ? d_.clock->MonotonicNs() : 0;

  const ApprovalRecord* rec =
      d_.approvals
          ? d_.approvals->FindUsable(action_digest, scope_digest, session_,
                                     turn_, now_ns)
          : nullptr;

  if (rec != nullptr) {
    AuditEvent grant_ev;
    grant_ev.event_id = GenerateUuid();
    grant_ev.session_id = session_;
    grant_ev.turn_id = turn_;
    grant_ev.action_id = pending_action_.action_id;
    grant_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
    grant_ev.monotonic_ns = now_ns;
    grant_ev.process_epoch_id = ProcessEpochId();
    grant_ev.kind = event_kind::kApprovalGranted;
    grant_ev.actor_type = ActorType::Operator;
    grant_ev.actor_id = rec->approver.subject_id;
    grant_ev.payload = ccj::Json{
        {"approval_id", rec->approval_id},
        {"action_id", pending_action_.action_id},
        {"approver", rec->approver.subject_id}
    };

    if (d_.audit) {
      auto res = d_.audit->Commit(grant_ev);
      if (!res.ok()) {
        (void)Fire(Event::AuditError, "approval_granted_audit_failed",
                   pending_action_.action_id);
        outcome.status = TurnStatus::Failed;
        Error fin_err = Finalize(&outcome);
        if (!fin_err.ok()) {
          return fin_err;
        }
        return outcome;
      }
    }

    gate_reentry_[pending_action_.action_id]++;
    (void)Fire(Event::Approved, "operator_approved", pending_action_.action_id);
  } else {
    AuditEvent rej_ev;
    rej_ev.event_id = GenerateUuid();
    rej_ev.session_id = session_;
    rej_ev.turn_id = turn_;
    rej_ev.action_id = pending_action_.action_id;
    rej_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
    rej_ev.monotonic_ns = now_ns;
    rej_ev.process_epoch_id = ProcessEpochId();
    rej_ev.kind = event_kind::kApprovalRejected;
    rej_ev.actor_type = ActorType::Operator;
    rej_ev.actor_id = "operator";
    rej_ev.payload = ccj::Json{
        {"action_id", pending_action_.action_id},
        {"reason_code", reason::kApprovalRejected}
    };

    if (d_.audit) {
      auto res = d_.audit->Commit(rej_ev);
      if (!res.ok()) {
        (void)Fire(Event::AuditError, "approval_rejected_audit_failed",
                   pending_action_.action_id);
        outcome.status = TurnStatus::Failed;
        Error fin_err = Finalize(&outcome);
        if (!fin_err.ok()) {
          return fin_err;
        }
        return outcome;
      }
    }

    (void)Fire(Event::RejectedOrExpired, "approval_rejected_or_expired",
               pending_action_.action_id);

    if (d_.conv) {
      Message den_msg;
      den_msg.role = Role::Tool;
      den_msg.tool_call_id = pending_action_.action_id;
      den_msg.content = DumpJson(ccj::Json{
          {"status", "denied"},
          {"reason_code", reason::kApprovalRejected},
          {"reason", "Operator approval was not granted or expired"}
      });
      den_msg.untrusted = true;
      den_msg.provenance = "gate:denial";
      d_.conv->AddMessage(std::move(den_msg));
    }
  }

  return Drive();
}

Result<TurnOutcome> AgentLoop::Drive() {
  TurnOutcome fallback;
  TurnOutcome& outcome = (t_active_outcome != nullptr) ? *t_active_outcome : fallback;
  outcome.status = TurnStatus::Completed;

  auto finish_turn = [&](TurnStatus st, std::string text = "") -> Result<TurnOutcome> {
    outcome.status = st;
    if (!text.empty()) {
      outcome.text = std::move(text);
    }
    Error fin_err = Finalize(&outcome);
    if (!fin_err.ok()) {
      return fin_err;
    }
    return outcome;
  };

  std::size_t iteration_count = 0;
  const std::size_t kMaxIterations = config_.max_turns * 4 + 16;

  while (turn_active_ && ++iteration_count <= kMaxIterations) {
    State current_state = fsm_.current();

    if (current_state == State::Infer) {
      // 1. Cancel check
      if (cancel_flag_.load(std::memory_order_relaxed)) {
        (void)Fire(Event::Cancel, "user_cancelled");
        return finish_turn(TurnStatus::Cancelled);
      }

      // 2. Budget check
      if (d_.budget && d_.clock && d_.budget->IsDeadlineExceeded(d_.clock->MonotonicNs())) {
        (void)Fire(Event::BudgetExhausted, "deadline_exceeded");
        return finish_turn(TurnStatus::Completed, "Turn budget exhausted.");
      }

      // 3. Compact context if compactor provided
      if (d_.compactor && d_.conv) {
        (void)d_.conv->Compact(*d_.compactor, 65536);
      }

      // 4. Export tools for model
      std::vector<ModelToolDeclaration> model_tools;
      if (d_.registry) {
        model_tools = d_.registry->ExportForModel(mode_);
      }

      // 5. Commit infer_begin
      AuditEvent infer_beg_ev;
      infer_beg_ev.event_id = GenerateUuid();
      infer_beg_ev.session_id = session_;
      infer_beg_ev.turn_id = turn_;
      infer_beg_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
      infer_beg_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
      infer_beg_ev.process_epoch_id = ProcessEpochId();
      infer_beg_ev.kind = event_kind::kInferBegin;
      infer_beg_ev.actor_type = ActorType::Core;
      infer_beg_ev.actor_id = "core:agent_loop";
      infer_beg_ev.payload = ccj::Json{
          {"messages_count", d_.conv ? d_.conv->size() : 0},
          {"tools_count", model_tools.size()}
      };

      if (d_.audit) {
        auto res = d_.audit->Commit(infer_beg_ev);
        if (!res.ok()) {
          (void)Fire(Event::AuditError, "infer_begin_audit_failed");
          return finish_turn(TurnStatus::Failed);
        }
      }

      // 6. Complete inference
      if (!d_.provider) {
        (void)Fire(Event::ProviderError, "Inference adapter is null");
        return finish_turn(TurnStatus::Failed);
      }

      InferenceRequest req;
      req.messages = d_.conv ? &d_.conv->messages() : nullptr;
      req.deadline_ns = d_.budget ? d_.budget->deadline_ns() : 0;
      CancelToken ctok;
      ctok.flag = &cancel_flag_;
      req.cancel = ctok;

      auto infer_res = d_.provider->Complete(req);

      // 7. Commit infer_end
      AuditEvent infer_end_ev;
      infer_end_ev.event_id = GenerateUuid();
      infer_end_ev.session_id = session_;
      infer_end_ev.turn_id = turn_;
      infer_end_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
      infer_end_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
      infer_end_ev.process_epoch_id = ProcessEpochId();
      infer_end_ev.kind = event_kind::kInferEnd;
      infer_end_ev.actor_type = ActorType::Core;
      infer_end_ev.actor_id = "core:agent_loop";
      infer_end_ev.payload = ccj::Json{
          {"finish_reason",
           infer_res.ok() ? ToString(infer_res.value().finish) : "error"},
          {"prompt_tokens",
           infer_res.ok() ? infer_res.value().usage.prompt_tokens : 0},
          {"completion_tokens",
           infer_res.ok() ? infer_res.value().usage.completion_tokens : 0},
          {"raw_digest",
           infer_res.ok() ? infer_res.value().raw_digest.hex() : ""}
      };

      if (d_.audit) {
        auto res = d_.audit->Commit(infer_end_ev);
        if (!res.ok()) {
          (void)Fire(Event::AuditError, "infer_end_audit_failed");
          return finish_turn(TurnStatus::Failed);
        }
      }

      if (!infer_res.ok()) {
        (void)Fire(Event::ProviderError, infer_res.error().message);
        return finish_turn(TurnStatus::Failed);
      }

      const auto& resp = infer_res.value();
      outcome.usage.prompt_tokens += resp.usage.prompt_tokens;
      outcome.usage.completion_tokens += resp.usage.completion_tokens;

      if (resp.finish == FinishReason::Cancelled || cancel_flag_.load(std::memory_order_relaxed)) {
        (void)Fire(Event::Cancel, "cancelled");
        return finish_turn(TurnStatus::Cancelled);
      }

      if (resp.finish == FinishReason::Error) {
        (void)Fire(Event::ProviderError, "provider_finish_reason_error");
        return finish_turn(TurnStatus::Failed);
      }

      if (resp.finish == FinishReason::Length && !resp.actions.empty()) {
        (void)Fire(Event::ProviderError, "truncated_action_rejected");
        return finish_turn(TurnStatus::Failed);
      }

      (void)Fire(Event::InferOk, "infer_ok");

      // State is now Propose
      if (resp.actions.empty()) {
        outcome.text = resp.text;
        if (d_.conv && !resp.text.empty()) {
          Message asst_msg;
          asst_msg.role = Role::Assistant;
          asst_msg.content = resp.text;
          asst_msg.untrusted = false;
          asst_msg.provenance = "assistant:" + resp.identity.model_id;
          d_.conv->AddMessage(std::move(asst_msg));
        }
        (void)Fire(Event::NoAction, "no_action");
        return finish_turn(TurnStatus::Completed);
      } else if (resp.actions.size() > 1) {
        (void)Fire(Event::MultipleActions, "multiple_actions_not_allowed");
        return finish_turn(TurnStatus::Failed);
      } else {
        ActionRequest action = resp.actions[0];
        action.session_id = session_;
        action.turn_id = turn_;
        if (action.action_id.empty()) {
          action.action_id = GenerateUuid();
        }

        if (d_.conv) {
          Message asst_msg;
          asst_msg.role = Role::Assistant;
          asst_msg.content = resp.text;
          asst_msg.actions = {action};
          asst_msg.untrusted = false;
          asst_msg.provenance = "assistant:" + resp.identity.model_id;
          d_.conv->AddMessage(std::move(asst_msg));
        }

        pending_action_ = action;
        (void)Fire(Event::OneAction, "one_action", action.action_id);
      }
    } else if (current_state == State::Gate) {
      if (cancel_flag_.load(std::memory_order_relaxed)) {
        (void)Fire(Event::Cancel, "cancelled_at_gate", pending_action_.action_id);
        return finish_turn(TurnStatus::Cancelled);
      }

      if (!d_.gate) {
        (void)Fire(Event::AuditError, "PermissionGate is null", pending_action_.action_id);
        return finish_turn(TurnStatus::Failed);
      }

      auto act_dig_res = ComputeActionDigest(
          pending_action_.session_id, pending_action_.turn_id,
          pending_action_.action_id, pending_action_.tool_name,
          pending_action_.arguments);
      Digest action_digest = act_dig_res.ok() ? act_dig_res.value() : Digest::Zero();
      Digest policy_digest = d_.gate ? d_.gate->policy().policy_digest() : Digest::Zero();
      Digest registry_digest = d_.registry ? d_.registry->registry_digest() : Digest::Zero();
      auto scope_dig_res = ComputePermitDigest(
          action_digest, subject_.subject_id,
          static_cast<std::uint64_t>(mode_), policy_digest, registry_digest);
      Digest scope_digest = scope_dig_res.ok() ? scope_dig_res.value() : Digest::Zero();
      auto op_dig_res = ComputeOperationDigest(pending_action_.tool_name,
                                                pending_action_.arguments);
      Digest op_digest = op_dig_res.ok() ? op_dig_res.value() : Digest::Zero();

      bool is_locked =
          (indeterminate_locks_.find(op_digest.hex()) !=
           indeterminate_locks_.end()) ||
          (line_write_lockdown_ && d_.registry &&
           d_.registry->Lookup(pending_action_.tool_name).desc &&
           d_.registry->Lookup(pending_action_.tool_name).desc->effect !=
               Effect::None);

      GateInput gin;
      gin.action_digest = action_digest;
      gin.permit_scope_digest = scope_digest;
      gin.operation_digest = op_digest;
      gin.subject = subject_;
      gin.mode = mode_;
      gin.fsm_state = State::Gate;
      gin.now_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
      gin.now_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
      gin.gate_reentry_count = gate_reentry_[pending_action_.action_id];
      gin.indeterminate_locked = is_locked;

      Verdict v = d_.gate->Evaluate(pending_action_, gin);
      outcome.verdicts.push_back(v);

      // Commit gate_verdict BEFORE state transition
      AuditEvent v_ev;
      v_ev.event_id = GenerateUuid();
      v_ev.session_id = session_;
      v_ev.turn_id = turn_;
      v_ev.action_id = pending_action_.action_id;
      v_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
      v_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
      v_ev.process_epoch_id = ProcessEpochId();
      v_ev.kind = event_kind::kGateVerdict;
      v_ev.actor_type = ActorType::Core;
      v_ev.actor_id = "core:permission_gate";
      v_ev.payload = ccj::Json{
          {"decision", ToString(v.decision)},
          {"gate_stage", v.gate_stage},
          {"reason_code", v.reason_code},
          {"reason", v.reason},
          {"rule_id", v.rule_id},
          {"action_digest", action_digest.hex()},
          {"tool_name", pending_action_.tool_name}
      };

      if (d_.audit) {
        auto res = d_.audit->Commit(v_ev);
        if (!res.ok()) {
          (void)Fire(Event::AuditError, "verdict_audit_failed",
                     pending_action_.action_id);
          return finish_turn(TurnStatus::Failed);
        }
      }

      if (v.decision == Decision::Deny) {
        (void)Fire(Event::Deny, v.reason_code, pending_action_.action_id);
        if (d_.conv) {
          Message den_msg;
          den_msg.role = Role::Tool;
          den_msg.tool_call_id = pending_action_.action_id;
          den_msg.content = DumpJson(ccj::Json{
              {"status", "denied"},
              {"reason_code", v.reason_code},
              {"reason", v.reason},
              {"rule_id", v.rule_id}
          });
          den_msg.untrusted = true;
          den_msg.provenance = "gate:denial";
          d_.conv->AddMessage(std::move(den_msg));
        }
        // Transitions to Observe
      } else if (v.decision == Decision::Ask) {
        std::string app_id = GenerateUuid();
        pending_approval_id_ = app_id;

        AuditEvent app_ev;
        app_ev.event_id = GenerateUuid();
        app_ev.session_id = session_;
        app_ev.turn_id = turn_;
        app_ev.action_id = pending_action_.action_id;
        app_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
        app_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
        app_ev.process_epoch_id = ProcessEpochId();
        app_ev.kind = event_kind::kApprovalRequested;
        app_ev.actor_type = ActorType::Core;
        app_ev.actor_id = "core:permission_gate";
        app_ev.payload = ccj::Json{
            {"approval_id", app_id},
            {"tool_name", pending_action_.tool_name},
            {"action_digest", action_digest.hex()},
            {"scope_digest", scope_digest.hex()},
            {"reason_code", v.reason_code},
            {"reason", v.reason}
        };

        if (d_.audit) {
          auto res = d_.audit->Commit(app_ev);
          if (!res.ok()) {
            (void)Fire(Event::AuditError, "approval_request_audit_failed",
                       pending_action_.action_id);
            return finish_turn(TurnStatus::Failed);
          }
        }

        (void)Fire(Event::Ask, v.reason_code, pending_action_.action_id);

        outcome.status = TurnStatus::PendingApproval;
        outcome.pending_action_id = pending_action_.action_id;
        outcome.pending_approval_id = app_id;
        outcome.final_state = fsm_.current();
        return outcome;
      } else {
        // Allow
        auto lookup = d_.registry ? d_.registry->Lookup(pending_action_.tool_name)
                                  : LookupResult{};
        if (lookup.kind != LookupKind::Enabled || lookup.desc == nullptr) {
          (void)Fire(Event::Deny, reason::kToolNotRegistered,
                     pending_action_.action_id);
          continue;
        }
        const ToolDescriptor& td = *lookup.desc;

        std::string idem_key = action_digest.hex();
        AuditEvent start_ev;
        start_ev.event_id = GenerateUuid();
        start_ev.session_id = session_;
        start_ev.turn_id = turn_;
        start_ev.action_id = pending_action_.action_id;
        start_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
        start_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
        start_ev.process_epoch_id = ProcessEpochId();
        start_ev.kind = event_kind::kToolCallStarted;
        start_ev.actor_type = ActorType::Core;
        start_ev.actor_id = "core:agent_loop";
        start_ev.payload = ccj::Json{
            {"tool_name", pending_action_.tool_name},
            {"idempotency_key", idem_key},
            {"action_digest", action_digest.hex()},
            {"scope_digest", scope_digest.hex()},
            {"timeout_ms", td.timeout_ms},
            {"effect", ToString(td.effect)}
        };

        if (d_.audit) {
          auto res = d_.audit->Commit(start_ev);
          if (!res.ok()) {
            (void)Fire(Event::AuditError, "tool_call_started_audit_failed",
                       pending_action_.action_id);
            return finish_turn(TurnStatus::Failed);
          }
        }

        if (d_.approvals) {
          const ApprovalRecord* rec = d_.approvals->FindUsable(
              action_digest, scope_digest, session_, turn_,
              d_.clock ? d_.clock->MonotonicNs() : 0);
          if (rec != nullptr) {
            d_.approvals->Consume(rec->approval_id,
                                  d_.clock ? d_.clock->MonotonicNs() : 0);
          }
        }

        ExecutionPermit permit =
            d_.gate->IssuePermit(td, action_digest, scope_digest,
                                 d_.clock ? d_.clock->MonotonicNs() : 0);

        (void)Fire(Event::Allow, "gate_allowed", pending_action_.action_id);

        ToolCallContext tctx;
        tctx.idempotency_key = idem_key;
        tctx.deadline_ns = d_.budget ? d_.budget->deadline_ns() : 0;
        CancelToken ctok;
        ctok.flag = &cancel_flag_;
        tctx.cancel = &ctok;
        tctx.subject = &subject_;

        ToolResult tool_result;
        if (d_.invoker) {
          tool_result = d_.invoker->Invoke(std::move(permit),
                                           pending_action_.arguments, tctx);
        } else {
          tool_result.status = ToolResultStatus::Error;
          tool_result.error_code = "missing_invoker";
          tool_result.error_message = "ToolInvoker dependency is null";
        }

        if (d_.budget) {
          (void)d_.budget->RecordToolCall(tool_result.output_bytes);
        }

        AuditEvent res_ev;
        res_ev.event_id = GenerateUuid();
        res_ev.session_id = session_;
        res_ev.turn_id = turn_;
        res_ev.action_id = pending_action_.action_id;
        res_ev.wall_time_utc = d_.clock ? d_.clock->NowUtcRfc3339() : "";
        res_ev.monotonic_ns = d_.clock ? d_.clock->MonotonicNs() : 0;
        res_ev.process_epoch_id = ProcessEpochId();
        res_ev.kind = event_kind::kToolResult;
        res_ev.actor_type = ActorType::Tool;
        res_ev.actor_id = td.name;
        res_ev.payload = ccj::Json{
            {"status", ToString(tool_result.status)},
            {"tool_name", td.name},
            {"output_bytes", tool_result.output_bytes},
            {"elapsed_us", tool_result.elapsed_us},
            {"attempt_count", tool_result.attempt_count},
            {"truncated", tool_result.truncated},
            {"masked", tool_result.masked}
        };

        if (d_.audit) {
          auto res = d_.audit->Commit(res_ev);
          if (!res.ok()) {
            (void)Fire(Event::AuditError, "tool_result_audit_failed",
                       pending_action_.action_id);
            return finish_turn(TurnStatus::Failed);
          }
        }

        if (tool_result.status == ToolResultStatus::Indeterminate) {
          outcome.had_indeterminate = true;
          indeterminate_locks_.insert(op_digest.hex());
          line_write_lockdown_ = true;
        }

        if (d_.conv) {
          Message tmsg;
          tmsg.role = Role::Tool;
          tmsg.tool_call_id = pending_action_.action_id;
          if (tool_result.status == ToolResultStatus::Ok) {
            tmsg.content = DumpJson(tool_result.content);
          } else if (tool_result.status == ToolResultStatus::Indeterminate) {
            tmsg.content = DumpJson(ccj::Json{
                {"status", "indeterminate"},
                {"tool", pending_action_.tool_name},
                {"before", tool_result.before},
                {"requested", tool_result.requested},
                {"after", nullptr},
                {"note", "Execution outcome indeterminate. Do not retry automatically."}
            });
          } else {
            tmsg.content = DumpJson(MakeInvalidToolResult(
                pending_action_.tool_name, tool_result.error_code,
                tool_result.error_message));
          }
          tmsg.untrusted = true;
          tmsg.provenance = "tool:" + pending_action_.tool_name;
          d_.conv->AddMessage(std::move(tmsg));
        }

        Event exec_ev = Fsm::MapToolResultStatus(tool_result.status);
        (void)Fire(exec_ev, ToString(tool_result.status), pending_action_.action_id);
      }
    } else if (current_state == State::Observe) {
      if (cancel_flag_.load(std::memory_order_relaxed)) {
        (void)Fire(Event::Cancel, "observe_cancelled");
        return finish_turn(TurnStatus::Cancelled);
      }

      if ((d_.budget && d_.clock && d_.budget->IsDeadlineExceeded(d_.clock->MonotonicNs())) ||
          (d_.budget && d_.budget->tool_calls_count() >= d_.budget->budget().max_tool_calls)) {
        (void)Fire(Event::CompleteOrLimit, "turn_limit_reached");
        return finish_turn(TurnStatus::Completed);
      }

      (void)Fire(Event::Continue, "observe_continue");
    } else if (IsTerminal(current_state)) {
      if (current_state == State::Done) {
        return finish_turn(TurnStatus::Completed);
      } else if (current_state == State::Cancelled) {
        return finish_turn(TurnStatus::Cancelled);
      } else {
        return finish_turn(TurnStatus::Failed);
      }
    } else {
      return finish_turn(TurnStatus::Failed);
    }
  }

  if (turn_active_) {
    (void)Fire(Event::AuditError, "max_iterations_exceeded");
    return finish_turn(TurnStatus::Failed);
  }
  return outcome;
}

}  // namespace cogito
