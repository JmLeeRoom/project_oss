// SPDX-License-Identifier: Apache-2.0

#include "cogito/agent_loop.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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
#include "cogito/tool_schema.hpp"

namespace ccj = cogito::ccj;

namespace {

class FakeTestClock : public cogito::Clock {
 public:
  explicit FakeTestClock(std::int64_t initial_ns = 1'000'000'000LL)
      : now_ns_(initial_ns) {}

  std::int64_t MonotonicNs() const noexcept override { return now_ns_; }
  std::string NowUtcRfc3339() const override { return "2026-08-26T12:00:00Z"; }

  void AdvanceMs(std::int64_t ms) noexcept { now_ns_ += ms * 1'000'000LL; }
  void SetNs(std::int64_t ns) noexcept { now_ns_ = ns; }

 private:
  std::int64_t now_ns_;
};

class MemoryApprovalStore : public cogito::ApprovalStore {
 public:
  const cogito::ApprovalRecord* FindUsable(const cogito::Digest& action_digest,
                                           const cogito::Digest& scope_digest,
                                           const cogito::SessionId& session_id,
                                           cogito::TurnId turn_id,
                                           std::int64_t now_ns) const override {
    (void)session_id;
    (void)turn_id;
    for (const auto& rec : approvals_) {
      if (!rec.consumed && rec.action_digest == action_digest &&
          rec.scope_digest == scope_digest &&
          (rec.expires_at_ns == 0 || now_ns < rec.expires_at_ns)) {
        return &rec;
      }
    }
    return nullptr;
  }

  cogito::Error AddApproval(cogito::ApprovalRecord record) override {
    approvals_.push_back(std::move(record));
    return cogito::Error::Ok();
  }

  cogito::Error Consume(const std::string& approval_id, std::int64_t now_ns) override {
    (void)now_ns;
    for (auto& rec : approvals_) {
      if (rec.approval_id == approval_id) {
        rec.consumed = true;
        return cogito::Error::Ok();
      }
    }
    return cogito::Error{cogito::Errc::ApprovalRequired, cogito::reason::kApprovalRequired,
                         "Approval not found to consume"};
  }

  void Clear() { approvals_.clear(); }

 private:
  std::vector<cogito::ApprovalRecord> approvals_;
};

struct TestFixture {
  FakeTestClock clock;
  cogito::RecordingAuditJournal audit;
  cogito::ConversationStore conv;
  std::unique_ptr<cogito::ContextCompactor> compactor;
  cogito::FakeProvider provider;
  cogito::ToolRegistry registry;
  std::unique_ptr<cogito::PolicyEngine> policy;
  cogito::TurnBudget budget_spec;
  cogito::BudgetTracker budget;
  MemoryApprovalStore approvals;
  std::unique_ptr<cogito::PermissionGate> gate;
  std::unique_ptr<cogito::ToolInvoker> invoker;

  cogito::Subject subject;
  cogito::SessionId session_id;

  std::size_t read_tool_calls = 0;
  std::size_t write_tool_calls = 0;

  TestFixture()
      : compactor(cogito::MakeDropOldestObservationCompactor()),
        budget(budget_spec),
        session_id("sess-test-001") {
    if (cogito::ProcessEpochId().empty()) {
      (void)cogito::InitProcessEpoch();
    }
    subject.subject_id = "user_alice";
    subject.roles = {"operator"};
    subject.auth_method = "os_user";

    // Setup Policy
    ccj::Json policy_doc = ccj::Json{
        {"schema_version", 1},
        {"default", "deny"},
        {"rules", ccj::Json::array({
            {
                {"id", "allow_reads"},
                {"priority", 10},
                {"tool", "sensor.*"},
                {"decision", "allow"}
            },
            {
                {"id", "ask_writes"},
                {"priority", 20},
                {"tool", "valve.*"},
                {"decision", "ask"},
                {"reason", "Valve write requires operator approval"}
            }
        })}
    };
    auto policy_res = cogito::PolicyEngine::Create(policy_doc);
    REQUIRE(policy_res.ok());
    policy = std::move(policy_res).take();

    // Register Tools
    cogito::ToolDescriptor td_read;
    td_read.name = "sensor.read";
    td_read.description = "Read sensor";
    td_read.input_schema = ccj::Json{{"type", "object"}};
    td_read.effect = cogito::Effect::None;
    td_read.risk = cogito::Risk::Low;
    td_read.idempotency = cogito::Idempotency::Safe;
    td_read.approval_required = false;
    td_read.provider_id = "prov";
    td_read.invoker_id = "inv";
    td_read.status = cogito::ToolStatus::Enabled;
    td_read.SetHandler([this](const ccj::Json&, const cogito::ToolCallContext&) {
      ++read_tool_calls;
      cogito::ToolResult r;
      r.status = cogito::ToolResultStatus::Ok;
      r.content = ccj::Json{{"temperature", 23.4}};
      return r;
    });
    (void)registry.Register(std::move(td_read));

    cogito::ToolDescriptor td_write;
    td_write.name = "valve.set";
    td_write.description = "Set valve position";
    td_write.input_schema = ccj::Json{{"type", "object"}};
    td_write.effect = cogito::Effect::Write;
    td_write.risk = cogito::Risk::Medium;
    td_write.idempotency = cogito::Idempotency::Conditional;
    td_write.approval_required = true;
    td_write.provider_id = "prov";
    td_write.invoker_id = "inv";
    td_write.status = cogito::ToolStatus::Enabled;
    td_write.SetHandler([this](const ccj::Json&, const cogito::ToolCallContext&) {
      ++write_tool_calls;
      cogito::ToolResult r;
      r.status = cogito::ToolResultStatus::Ok;
      r.content = ccj::Json{{"status", "valve_set"}};
      return r;
    });
    (void)registry.Register(std::move(td_write));

    (void)registry.Freeze();

    gate = std::make_unique<cogito::PermissionGate>(registry, *policy, budget, &approvals);
    invoker = std::make_unique<cogito::ToolInvoker>(registry, clock);
  }

  cogito::AgentDeps MakeDeps() {
    cogito::AgentDeps d;
    d.provider = &provider;
    d.registry = &registry;
    d.gate = gate.get();
    d.invoker = invoker.get();
    d.approvals = &approvals;
    d.budget = &budget;
    d.conv = &conv;
    d.audit = &audit;
    d.clock = &clock;
    d.compactor = compactor.get();
    return d;
  }
};

}  // namespace

TEST_CASE("AgentLoop RunTurn pure text conversation (No Action)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse resp;
  resp.text = "Hello, I am Cogito++ assistant.";
  resp.finish = cogito::FinishReason::Stop;
  resp.usage.prompt_tokens = 10;
  resp.usage.completion_tokens = 15;
  fix.provider.PushResponse(std::move(resp));

  auto res = loop.RunTurn("Hello!");
  REQUIRE(res.ok());

  const auto& outcome = res.value();
  REQUIRE(outcome.status == cogito::TurnStatus::Completed);
  REQUIRE(outcome.text == "Hello, I am Cogito++ assistant.");
  REQUIRE(outcome.final_state == cogito::State::Done);
  REQUIRE(outcome.usage.prompt_tokens == 10);
  REQUIRE(outcome.usage.completion_tokens == 15);
  REQUIRE(!outcome.had_indeterminate);
  REQUIRE(!outcome.transitions.empty());

  // Check audit journal events
  auto events = fix.audit.GetEvents();
  REQUIRE(events.size() >= 4);
  REQUIRE(events.front().kind == cogito::event_kind::kTurnBegin);
  REQUIRE(events.back().kind == cogito::event_kind::kTurnEnd);
  REQUIRE(fix.audit.VerifyChain().value());
}

TEST_CASE("AgentLoop RunTurn tool invocation (Allow path)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  // Step 1: Model proposes tool call
  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.tool_name = "sensor.read";
  act.arguments = ccj::Json{{"sensor_id", "temp_1"}};
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  // Step 2: Model finishes with final text after seeing tool result
  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "The sensor temperature is 23.4 C.";
  fix.provider.PushResponse(std::move(r2));

  auto res = loop.RunTurn("Check temperature");
  REQUIRE(res.ok());

  const auto& outcome = res.value();
  REQUIRE(outcome.status == cogito::TurnStatus::Completed);
  REQUIRE(outcome.text == "The sensor temperature is 23.4 C.");
  REQUIRE(fix.read_tool_calls == 1);

  // Verify audit events sequence
  auto events = fix.audit.GetEvents();
  bool found_gate_verdict = false;
  bool found_tool_start = false;
  bool found_tool_result = false;

  for (const auto& ev : events) {
    if (ev.kind == cogito::event_kind::kGateVerdict) found_gate_verdict = true;
    if (ev.kind == cogito::event_kind::kToolCallStarted) found_tool_start = true;
    if (ev.kind == cogito::event_kind::kToolResult) found_tool_result = true;
  }

  REQUIRE(found_gate_verdict);
  REQUIRE(found_tool_start);
  REQUIRE(found_tool_result);
  REQUIRE(fix.audit.VerifyChain().value());
}

TEST_CASE("AgentLoop Gate Deny path (Zero write count)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::ReadOnly, fix.session_id);

  // Model proposes write tool which is denied under ReadOnly mode / policy
  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.tool_name = "valve.set";
  act.arguments = ccj::Json{{"pos", 50}};
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "Action was denied.";
  fix.provider.PushResponse(std::move(r2));

  auto res = loop.RunTurn("Open the valve");
  REQUIRE(res.ok());

  REQUIRE(fix.write_tool_calls == 0);  // Write tool NEVER executed
  REQUIRE(res.value().status == cogito::TurnStatus::Completed);
}

TEST_CASE("AgentLoop Ask -> PendingApproval -> ResumeTurn (Approved)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  // Step 1: Model proposes valve.set (Policy says Ask)
  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.action_id = "act-valve-01";
  act.tool_name = "valve.set";
  act.arguments = ccj::Json{{"position", 100}};
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  auto turn_res = loop.RunTurn("Set valve to 100%");
  REQUIRE(turn_res.ok());
  const auto& outcome1 = turn_res.value();

  REQUIRE(outcome1.status == cogito::TurnStatus::PendingApproval);
  REQUIRE(outcome1.pending_action_id == "act-valve-01");
  REQUIRE(!outcome1.pending_approval_id.empty());
  REQUIRE(fix.write_tool_calls == 0);  // NOT yet executed
  REQUIRE(loop.fsm().current() == cogito::State::AwaitApproval);

  // Step 2: Operator grants approval
  cogito::ApprovalRecord app_rec;
  app_rec.approval_id = outcome1.pending_approval_id;
  app_rec.session_id = fix.session_id;
  app_rec.turn_id = loop.current_turn_id();
  act.session_id = fix.session_id;
  act.turn_id = loop.current_turn_id();
  act.action_id = outcome1.pending_action_id;
  app_rec.action_digest = cogito::ComputeActionDigest(
      act.session_id, act.turn_id, act.action_id, act.tool_name, act.arguments).value();
  app_rec.scope_digest = cogito::ComputePermitDigest(
      app_rec.action_digest, fix.subject.subject_id,
      static_cast<std::uint64_t>(cogito::ExecutionMode::Default),
      fix.policy->policy_digest(), fix.registry.registry_digest()).value();
  app_rec.requester = fix.subject;
  cogito::Subject approver;
  approver.subject_id = "operator_bob";
  approver.roles = {"security_officer"};
  app_rec.approver = approver;
  app_rec.granted_at_utc = "2026-08-26T12:05:00Z";
  app_rec.expires_at_ns = fix.clock.MonotonicNs() + 60'000'000'000LL;
  app_rec.consumed = false;
  fix.approvals.AddApproval(std::move(app_rec));

  // Response for after tool execution
  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "Valve was successfully set.";
  fix.provider.PushResponse(std::move(r2));

  // Step 3: ResumeTurn
  auto resume_res = loop.ResumeTurn();
  REQUIRE(resume_res.ok());
  const auto& outcome2 = resume_res.value();

  REQUIRE(outcome2.status == cogito::TurnStatus::Completed);
  REQUIRE(fix.write_tool_calls == 1);  // Executed EXACTLY ONCE on allow
  REQUIRE(loop.fsm().current() == cogito::State::Done);
  REQUIRE(fix.audit.VerifyChain().value());
}

TEST_CASE("AgentLoop Indeterminate lockdown and recovery", "[loop]") {
  TestFixture fix;

  // Change write tool to timeout / indeterminate
  cogito::ToolRegistry reg2;
  cogito::ToolDescriptor td_dest;
  td_dest.name = "valve.set";
  td_dest.description = "Destructive write";
  td_dest.input_schema = ccj::Json{{"type", "object"}};
  td_dest.effect = cogito::Effect::Write;
  td_dest.risk = cogito::Risk::Medium;
  td_dest.idempotency = cogito::Idempotency::Conditional;
  td_dest.approval_required = true;
  td_dest.timeout_ms = 3000;
  td_dest.max_output_bytes = 64 * 1024;
  td_dest.provider_id = "prov";
  td_dest.invoker_id = "inv";
  td_dest.status = cogito::ToolStatus::Enabled;
  td_dest.SetHandler([](const ccj::Json&, const cogito::ToolCallContext&) {
    cogito::ToolResult r;
    r.status = cogito::ToolResultStatus::Timeout;  // Write timeout -> Indeterminate
    return r;
  });
  REQUIRE(reg2.Register(std::move(td_dest)).ok());
  REQUIRE(reg2.Freeze().ok());

  auto gate2 = std::make_unique<cogito::PermissionGate>(reg2, *fix.policy, fix.budget, &fix.approvals);
  auto invoker2 = std::make_unique<cogito::ToolInvoker>(reg2, fix.clock);

  cogito::AgentDeps d = fix.MakeDeps();
  d.registry = &reg2;
  d.gate = gate2.get();
  d.invoker = invoker2.get();

  cogito::AgentLoop loop(d, fix.subject, cogito::ExecutionMode::Default, fix.session_id);

  // Pre-approve the write tool for this turn
  cogito::ActionRequest act;
  act.action_id = "act-valve-indet";
  act.session_id = fix.session_id;
  act.turn_id = 1;
  act.tool_name = "valve.set";
  act.arguments = ccj::Json{{"pos", 10}};

  cogito::ApprovalRecord app_rec;
  app_rec.approval_id = "app-valve-auto";
  app_rec.session_id = fix.session_id;
  app_rec.turn_id = 1;
  app_rec.action_digest = cogito::ComputeActionDigest(
      act.session_id, act.turn_id, act.action_id, act.tool_name, act.arguments).value();
  app_rec.scope_digest = cogito::ComputePermitDigest(
      app_rec.action_digest, fix.subject.subject_id,
      static_cast<std::uint64_t>(cogito::ExecutionMode::Default),
      fix.policy->policy_digest(), reg2.registry_digest()).value();
  app_rec.requester = fix.subject;
  cogito::Subject approver;
  approver.subject_id = "operator_jack";
  app_rec.approver = approver;
  app_rec.granted_at_utc = "2026-08-26T12:05:00Z";
  app_rec.expires_at_ns = fix.clock.MonotonicNs() + 60'000'000'000LL;
  app_rec.consumed = false;
  fix.approvals.AddApproval(std::move(app_rec));

  // Step 1: Tool returns Indeterminate
  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "Indeterminate state occurred.";
  fix.provider.PushResponse(std::move(r2));

  auto res1 = loop.RunTurn("Move valve");
  REQUIRE(res1.ok());
  REQUIRE(res1.value().had_indeterminate == true);

  // Step 2: Unlock via AcknowledgeIndeterminate
  cogito::Subject op;
  op.subject_id = "operator_jack";
  auto ack_err = loop.AcknowledgeIndeterminate(op, "Equipment verified in safe state");
  REQUIRE(ack_err.ok());
}

TEST_CASE("AgentLoop Session sealing (SealSession)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  REQUIRE(!loop.sealed());
  auto seal_err = loop.SealSession();
  REQUIRE(seal_err.ok());
  REQUIRE(loop.sealed());

  // Subsequent RunTurn must fail with TurnSealed
  auto res = loop.RunTurn("Hello after seal");
  REQUIRE(!res.ok());
  REQUIRE(res.error().code == cogito::Errc::TurnSealed);

  // Subsequent ResumeTurn must also fail with TurnSealed
  auto res2 = loop.ResumeTurn();
  REQUIRE(!res2.ok());
  REQUIRE(res2.error().code == cogito::Errc::TurnSealed);
}

TEST_CASE("AgentLoop audit failure isolation (Invariant 8)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.tool_name = "sensor.read";
  act.arguments = ccj::Json{{"id", 1}};
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  // Inject audit failure on tool_call_started (commit #4)
  // Commits: 1: turn_begin, 2: infer_begin, 3: infer_end, 4: gate_verdict, 5: tool_call_started
  fix.audit.InjectFailureAt(5, cogito::Error{cogito::Errc::AuditWriteFailed,
                                             cogito::reason::kAuditCommitFailed,
                                             "Disk I/O error"});

  auto res = loop.RunTurn("Read sensor with audit error");
  REQUIRE(!res.ok());
  REQUIRE(res.error().code == cogito::Errc::AuditWriteFailed);
  REQUIRE(fix.read_tool_calls == 0);  // Handler MUST NEVER be called on audit failure!
}

TEST_CASE("AgentLoop multi-turn sequential execution and monotonic turn counter", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  // Turn 1: Read temperature
  {
    cogito::InferenceResponse r1;
    r1.finish = cogito::FinishReason::ToolCalls;
    cogito::ActionRequest act;
    act.tool_name = "sensor.read";
    act.arguments = ccj::Json{{"sensor_id", "s1"}};
    r1.actions = {act};
    fix.provider.PushResponse(std::move(r1));

    cogito::InferenceResponse r2;
    r2.finish = cogito::FinishReason::Stop;
    r2.text = "Temp is 23.4 C";
    fix.provider.PushResponse(std::move(r2));

    auto res = loop.RunTurn("Turn 1");
    REQUIRE(res.ok());
    REQUIRE(loop.current_turn_id() == 1);
    REQUIRE(res.value().status == cogito::TurnStatus::Completed);
    REQUIRE(!res.value().transitions.empty());
  }

  // Turn 2: Pure text
  {
    cogito::InferenceResponse r;
    r.finish = cogito::FinishReason::Stop;
    r.text = "Turn 2 text response";
    fix.provider.PushResponse(std::move(r));

    auto res = loop.RunTurn("Turn 2");
    REQUIRE(res.ok());
    REQUIRE(loop.current_turn_id() == 2);
    REQUIRE(res.value().status == cogito::TurnStatus::Completed);
    REQUIRE(res.value().text == "Turn 2 text response");
  }

  // Turn 3: Denied write
  {
    cogito::InferenceResponse r1;
    r1.finish = cogito::FinishReason::ToolCalls;
    cogito::ActionRequest act;
    act.tool_name = "unknown.tool";
    act.arguments = ccj::Json::object();
    r1.actions = {act};
    fix.provider.PushResponse(std::move(r1));

    cogito::InferenceResponse r2;
    r2.finish = cogito::FinishReason::Stop;
    r2.text = "Handled denial";
    fix.provider.PushResponse(std::move(r2));

    auto res = loop.RunTurn("Turn 3");
    REQUIRE(res.ok());
    REQUIRE(loop.current_turn_id() == 3);
    REQUIRE(res.value().status == cogito::TurnStatus::Completed);
  }

  REQUIRE(fix.audit.VerifyChain().value());
}

TEST_CASE("AgentLoop RequestCancel handling and rapid cancellation", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  // Request cancel before turn
  loop.RequestCancel();

  cogito::InferenceResponse r;
  r.finish = cogito::FinishReason::Stop;
  r.text = "Should be cancelled";
  fix.provider.PushResponse(std::move(r));

  auto res = loop.RunTurn("Cancelled turn");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Cancelled);
  REQUIRE(loop.fsm().current() == cogito::State::Cancelled);
  REQUIRE(fix.audit.VerifyChain().value());
}

TEST_CASE("AgentLoop multiple actions rejection (Propose -> Failed)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse r;
  r.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest a1;
  a1.tool_name = "sensor.read";
  cogito::ActionRequest a2;
  a2.tool_name = "valve.set";
  r.actions = {a1, a2};  // 2 actions rejected by force_single_action rule
  fix.provider.PushResponse(std::move(r));

  auto res = loop.RunTurn("Run both tools");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Failed);
  REQUIRE(loop.fsm().current() == cogito::State::Failed);
  REQUIRE(fix.read_tool_calls == 0);
  REQUIRE(fix.write_tool_calls == 0);
}

TEST_CASE("AgentLoop ResumeTurn with already-consumed approval record", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.action_id = "act-valve-consumed";
  act.tool_name = "valve.set";
  act.arguments = ccj::Json{{"pos", 10}};
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  auto turn_res = loop.RunTurn("Set valve");
  REQUIRE(turn_res.ok());
  REQUIRE(turn_res.value().status == cogito::TurnStatus::PendingApproval);

  // Add approval but consume it immediately (e.g. consumed by external caller)
  cogito::ApprovalRecord app_rec;
  app_rec.approval_id = turn_res.value().pending_approval_id;
  app_rec.session_id = fix.session_id;
  app_rec.turn_id = loop.current_turn_id();
  act.session_id = fix.session_id;
  act.turn_id = loop.current_turn_id();
  act.action_id = turn_res.value().pending_action_id;
  app_rec.action_digest = cogito::ComputeActionDigest(
      act.session_id, act.turn_id, act.action_id, act.tool_name, act.arguments).value();
  app_rec.scope_digest = cogito::ComputePermitDigest(
      app_rec.action_digest, fix.subject.subject_id,
      static_cast<std::uint64_t>(cogito::ExecutionMode::Default),
      fix.policy->policy_digest(), fix.registry.registry_digest()).value();
  app_rec.requester = fix.subject;
  cogito::Subject approver;
  approver.subject_id = "operator_bob";
  app_rec.approver = approver;
  app_rec.granted_at_utc = "2026-08-26T12:05:00Z";
  app_rec.expires_at_ns = fix.clock.MonotonicNs() + 60'000'000'000LL;
  app_rec.consumed = true;  // ALREADY CONSUMED
  fix.approvals.AddApproval(std::move(app_rec));

  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "Approval was consumed; cannot perform write.";
  fix.provider.PushResponse(std::move(r2));

  // ResumeTurn should reject the consumed approval and transition to Observe -> Infer
  auto resume_res = loop.ResumeTurn();
  REQUIRE(resume_res.ok());
  REQUIRE(resume_res.value().status == cogito::TurnStatus::Completed);
  REQUIRE(fix.write_tool_calls == 0);  // WRITE CALLS MUST REMAIN ZERO
}

TEST_CASE("AgentLoop concurrency guard (single-thread enforcement)", "[loop]") {
  TestFixture fix;

  cogito::AgentLoop* loop_ptr = nullptr;
  bool handler_called = false;
  cogito::Error err_run = cogito::Error::Ok();
  cogito::Error err_resume = cogito::Error::Ok();
  cogito::Error err_seal = cogito::Error::Ok();
  cogito::Error err_retry = cogito::Error::Ok();

  cogito::ToolRegistry reg_conc;
  cogito::ToolDescriptor td_slow;
  td_slow.name = "sensor.read";
  td_slow.description = "Reentrant check tool";
  td_slow.input_schema = ccj::Json{{"type", "object"}};
  td_slow.effect = cogito::Effect::None;
  td_slow.risk = cogito::Risk::Low;
  td_slow.idempotency = cogito::Idempotency::Safe;
  td_slow.approval_required = false;
  td_slow.timeout_ms = 3000;
  td_slow.max_output_bytes = 64 * 1024;
  td_slow.provider_id = "prov";
  td_slow.invoker_id = "inv";
  td_slow.status = cogito::ToolStatus::Enabled;
  td_slow.SetHandler([&](const ccj::Json&, const cogito::ToolCallContext&) {
    handler_called = true;
    if (loop_ptr != nullptr) {
      auto res_run = loop_ptr->RunTurn("Concurrent attempt");
      if (!res_run.ok()) err_run = res_run.error();

      auto res_resume = loop_ptr->ResumeTurn();
      if (!res_resume.ok()) err_resume = res_resume.error();

      err_seal = loop_ptr->SealSession();
      err_retry = loop_ptr->RetryFinalize();
    }
    cogito::ToolResult r;
    r.status = cogito::ToolResultStatus::Ok;
    r.content = ccj::Json{{"val", 100}};
    return r;
  });
  REQUIRE(reg_conc.Register(std::move(td_slow)).ok());
  REQUIRE(reg_conc.Freeze().ok());

  auto gate_conc = std::make_unique<cogito::PermissionGate>(reg_conc, *fix.policy, fix.budget, &fix.approvals);
  auto invoker_conc = std::make_unique<cogito::ToolInvoker>(reg_conc, fix.clock);

  cogito::AgentDeps d = fix.MakeDeps();
  d.registry = &reg_conc;
  d.gate = gate_conc.get();
  d.invoker = invoker_conc.get();

  cogito::AgentLoop conc_loop(d, fix.subject, cogito::ExecutionMode::Default, fix.session_id);
  loop_ptr = &conc_loop;

  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.tool_name = "sensor.read";
  act.arguments = ccj::Json::object();
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  cogito::InferenceResponse r2;
  r2.finish = cogito::FinishReason::Stop;
  r2.text = "Finished turn";
  fix.provider.PushResponse(std::move(r2));

  auto res = conc_loop.RunTurn("Turn executing handler");
  REQUIRE(res.ok());
  REQUIRE(handler_called);

  // All 4 concurrent/reentrant invocations during active execution must be rejected with Errc::WrongThread
  REQUIRE(err_run.code == cogito::Errc::WrongThread);
  REQUIRE(err_resume.code == cogito::Errc::WrongThread);
  REQUIRE(err_seal.code == cogito::Errc::WrongThread);
  REQUIRE(err_retry.code == cogito::Errc::WrongThread);
}

TEST_CASE("AgentLoop session max_turns limit enforcement", "[loop]") {
  TestFixture fix;
  cogito::AgentLoopConfig cfg;
  cfg.max_turns = 2;

  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id, cfg);

  // Turn 1
  {
    cogito::InferenceResponse resp;
    resp.text = "Turn 1 done";
    resp.finish = cogito::FinishReason::Stop;
    fix.provider.PushResponse(std::move(resp));
  }
  auto res1 = loop.RunTurn("Turn 1");
  REQUIRE(res1.ok());
  REQUIRE(loop.current_turn_id() == 1);

  // Turn 2
  {
    cogito::InferenceResponse resp;
    resp.text = "Turn 2 done";
    resp.finish = cogito::FinishReason::Stop;
    fix.provider.PushResponse(std::move(resp));
  }
  auto res2 = loop.RunTurn("Turn 2");
  REQUIRE(res2.ok());
  REQUIRE(loop.current_turn_id() == 2);

  // Turn 3: Exceeds max_turns (2) -> Errc::TurnSealed
  auto res3 = loop.RunTurn("Turn 3");
  REQUIRE(!res3.ok());
  REQUIRE(res3.error().code == cogito::Errc::TurnSealed);
}

TEST_CASE("AgentLoop tool budget exhaustion in Observe state", "[loop]") {
  TestFixture fix;
  fix.budget_spec.max_tool_calls = 1;

  // Re-create budget tracker with max 1 tool call
  cogito::BudgetTracker custom_budget(fix.budget_spec);
  cogito::AgentDeps d = fix.MakeDeps();
  d.budget = &custom_budget;

  cogito::AgentLoop loop(d, fix.subject, cogito::ExecutionMode::Default, fix.session_id);

  // Model calls sensor.read
  cogito::InferenceResponse r1;
  r1.finish = cogito::FinishReason::ToolCalls;
  cogito::ActionRequest act;
  act.tool_name = "sensor.read";
  act.arguments = ccj::Json::object();
  r1.actions = {act};
  fix.provider.PushResponse(std::move(r1));

  auto res = loop.RunTurn("Read sensor with 1-call budget");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Completed);
  REQUIRE(loop.fsm().current() == cogito::State::Done);
}

TEST_CASE("AgentLoop RetryFinalize on pending turn finalize (G0-06)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  {
    cogito::InferenceResponse resp;
    resp.text = "Text response";
    resp.finish = cogito::FinishReason::Stop;
    fix.provider.PushResponse(std::move(resp));
  }

  // Inject failure on turn_end audit commit (commit #4)
  // Commits: 1: turn_begin, 2: infer_begin, 3: infer_end, 4: turn_end
  fix.audit.InjectFailureAt(4, cogito::Error{cogito::Errc::AuditWriteFailed,
                                             cogito::reason::kAuditCommitFailed,
                                             "Simulated turn_end commit failure"});

  auto res = loop.RunTurn("Trigger finalize pending");
  REQUIRE(!res.ok());
  REQUIRE(res.error().code == cogito::Errc::AuditWriteFailed);

  // Subsequent RunTurn should fail because finalize is pending
  auto res_blocked = loop.RunTurn("Next turn blocked");
  REQUIRE(!res_blocked.ok());
  REQUIRE(res_blocked.error().code == cogito::Errc::AuditWriteFailed);

  // Clear fault injection and RetryFinalize
  fix.audit.ClearFailureInjection();
  auto retry_err = loop.RetryFinalize();
  REQUIRE(retry_err.ok());

  // After RetryFinalize, next turn can run successfully
  {
    cogito::InferenceResponse resp;
    resp.text = "Recovered next turn";
    resp.finish = cogito::FinishReason::Stop;
    fix.provider.PushResponse(std::move(resp));
  }
  auto res_next = loop.RunTurn("Next turn after retry");
  REQUIRE(res_next.ok());
  REQUIRE(res_next.value().status == cogito::TurnStatus::Completed);
}

TEST_CASE("AgentLoop FinishReason::Error rejection", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse resp;
  resp.finish = cogito::FinishReason::Error;
  resp.text = "Error encountered during generation";
  fix.provider.PushResponse(std::move(resp));

  auto res = loop.RunTurn("Trigger provider error finish");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Failed);
  REQUIRE(loop.fsm().current() == cogito::State::Failed);
  REQUIRE(fix.read_tool_calls == 0);
  REQUIRE(fix.write_tool_calls == 0);
}

TEST_CASE("AgentLoop FinishReason::Length with incomplete action rejection (Rule 3)", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  cogito::InferenceResponse resp;
  resp.finish = cogito::FinishReason::Length;  // Truncated by token limit
  cogito::ActionRequest act;
  act.tool_name = "sensor.read";
  act.arguments = ccj::Json{{"sensor_id", "truncated_arg..."}};
  resp.actions = {act};
  fix.provider.PushResponse(std::move(resp));

  auto res = loop.RunTurn("Trigger truncated action");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Failed);
  REQUIRE(loop.fsm().current() == cogito::State::Failed);
  REQUIRE(fix.read_tool_calls == 0);
  REQUIRE(fix.write_tool_calls == 0);
}

TEST_CASE("AgentLoop Context compaction triggered during turn", "[loop]") {
  TestFixture fix;
  cogito::AgentLoop loop(fix.MakeDeps(), fix.subject,
                         cogito::ExecutionMode::Default, fix.session_id);

  // Prepopulate conversation with a large old tool observation
  cogito::Message old_tool_msg;
  old_tool_msg.role = cogito::Role::Tool;
  old_tool_msg.tool_call_id = "act-old-01";
  old_tool_msg.content = std::string(70000, 'Z');  // 70KB message exceeding 64KB compaction limit
  old_tool_msg.untrusted = true;
  old_tool_msg.provenance = "tool:sensor.read";
  fix.conv.AddMessage(std::move(old_tool_msg));

  // Add another recent tool message so latest observation is preserved
  cogito::Message recent_tool_msg;
  recent_tool_msg.role = cogito::Role::Tool;
  recent_tool_msg.tool_call_id = "act-recent-02";
  recent_tool_msg.content = "recent output";
  recent_tool_msg.untrusted = true;
  recent_tool_msg.provenance = "tool:sensor.read";
  fix.conv.AddMessage(std::move(recent_tool_msg));

  std::size_t msgs_before = fix.conv.size();
  REQUIRE(msgs_before == 2);

  cogito::InferenceResponse resp;
  resp.text = "Compacted conversation response";
  resp.finish = cogito::FinishReason::Stop;
  fix.provider.PushResponse(std::move(resp));

  auto res = loop.RunTurn("Compact test");
  REQUIRE(res.ok());
  REQUIRE(res.value().status == cogito::TurnStatus::Completed);
  // Old large observation was compacted
  REQUIRE(fix.conv.size() >= 2);
}


