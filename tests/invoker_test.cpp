// SPDX-License-Identifier: Apache-2.0

#include "cogito/invoker.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "cogito/canonical_json.hpp"
#include "cogito/clock.hpp"
#include "cogito/digest.hpp"
#include "cogito/identity.hpp"
#include "cogito/ids.hpp"
#include "cogito/inference.hpp"
#include "cogito/permit.hpp"
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

cogito::ToolDescriptor MakeTestTool(
    std::string name,
    cogito::Effect effect = cogito::Effect::None,
    std::size_t max_output_bytes = 64 * 1024,
    ccj::Json output_schema = nullptr,
    cogito::ToolHandler handler = nullptr) {
  cogito::ToolDescriptor td;
  td.name = std::move(name);
  td.description = "Test tool";
  td.input_schema = ccj::Json{{"type", "object"}};
  td.output_schema = std::move(output_schema);
  td.effect = effect;
  td.risk = (effect == cogito::Effect::Destructive)
                ? cogito::Risk::Critical
                : ((effect == cogito::Effect::Write) ? cogito::Risk::Medium
                                                     : cogito::Risk::Low);
  td.idempotency = (effect == cogito::Effect::Destructive)
                       ? cogito::Idempotency::Unsafe
                       : ((effect == cogito::Effect::Write)
                              ? cogito::Idempotency::Conditional
                              : cogito::Idempotency::Safe);
  td.approval_required = (effect != cogito::Effect::None);
  td.timeout_ms = 3000;
  td.max_output_bytes = max_output_bytes;
  td.provider_id = "test_provider";
  td.invoker_id = "test_invoker";
  td.status = cogito::ToolStatus::Enabled;
  if (handler) {
    td.SetHandler(std::move(handler));
  } else {
    td.SetHandler([](const ccj::Json&, const cogito::ToolCallContext&) {
      cogito::ToolResult r;
      r.status = cogito::ToolResultStatus::Ok;
      r.content = ccj::Json{{"result", "ok"}};
      return r;
    });
  }
  return td;
}

}  // namespace

TEST_CASE("ToolInvoker permit consumption and single execution", "[invoker]") {
  FakeTestClock clock;
  cogito::ToolRegistry reg;

  std::size_t call_count = 0;
  auto td = MakeTestTool("sensor.read", cogito::Effect::None, 64 * 1024, nullptr,
                         [&call_count](const ccj::Json&, const cogito::ToolCallContext&) {
                           ++call_count;
                           cogito::ToolResult r;
                           r.status = cogito::ToolResultStatus::Ok;
                           r.content = ccj::Json{{"temp", 25.5}};
                           return r;
                         });

  REQUIRE(reg.Register(std::move(td)).ok());
  REQUIRE(reg.Freeze().ok());

  cogito::ToolInvoker invoker(reg, clock);

  cogito::Digest act_digest = cogito::Sha256("action_data").value();
  cogito::Digest scope_digest = cogito::Sha256("scope_data").value();

  auto permit = cogito::testing::PermitTestSeam::Create(
      act_digest, scope_digest, "sensor.read", "idem-1",
      clock.MonotonicNs() + 5'000'000'000LL, 3000, cogito::Effect::None, false);

  REQUIRE(permit.valid());

  cogito::ToolCallContext ctx;
  ctx.idempotency_key = "idem-1";
  ctx.deadline_ns = clock.MonotonicNs() + 5'000'000'000LL;

  // First invoke: success, permit consumed
  auto res = invoker.Invoke(std::move(permit), ccj::Json::object(), ctx);
  REQUIRE(res.status == cogito::ToolResultStatus::Ok);
  REQUIRE(call_count == 1);
  REQUIRE(res.attempt_count == 1);
  REQUIRE(res.content["temp"] == 25.5);

  // Moved-from permit is now invalid
  REQUIRE(!permit.valid());
  auto res2 = invoker.Invoke(std::move(permit), ccj::Json::object(), ctx);
  REQUIRE(res2.status == cogito::ToolResultStatus::Error);
  REQUIRE(call_count == 1);  // Handler NOT invoked
}

TEST_CASE("ToolInvoker exception isolation (G0-09 Rule 3)", "[invoker]") {
  FakeTestClock clock;
  cogito::ToolRegistry reg;

  // 1. std::exception thrown
  auto td1 = MakeTestTool("sensor.faulty", cogito::Effect::None, 64 * 1024, nullptr,
                          [](const ccj::Json&, const cogito::ToolCallContext&) -> cogito::ToolResult {
                            throw std::runtime_error("Hardware bus fault");
                          });

  // 2. Non-std::exception thrown (int)
  auto td2 = MakeTestTool("sensor.panic", cogito::Effect::None, 64 * 1024, nullptr,
                          [](const ccj::Json&, const cogito::ToolCallContext&) -> cogito::ToolResult {
                            throw 42;
                          });

  REQUIRE(reg.Register(std::move(td1)).ok());
  REQUIRE(reg.Register(std::move(td2)).ok());
  REQUIRE(reg.Freeze().ok());

  cogito::ToolInvoker invoker(reg, clock);

  cogito::Digest act_digest = cogito::Sha256("action_data").value();
  cogito::Digest scope_digest = cogito::Sha256("scope_data").value();

  // Test std::exception isolation
  {
    auto permit = cogito::testing::PermitTestSeam::Create(
        act_digest, scope_digest, "sensor.faulty", "idem-2",
        clock.MonotonicNs() + 5'000'000'000LL, 3000, cogito::Effect::None, false);

    cogito::ToolCallContext ctx;
    ctx.idempotency_key = "idem-2";

    auto res = invoker.Invoke(std::move(permit), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Error);
    REQUIRE(res.error_code == "handler_exception");
    REQUIRE(res.content["status"] == "error");
    REQUIRE(res.content["tool"] == "sensor.faulty");
  }

  // Test non-std exception isolation (catch ...)
  {
    auto permit = cogito::testing::PermitTestSeam::Create(
        act_digest, scope_digest, "sensor.panic", "idem-3",
        clock.MonotonicNs() + 5'000'000'000LL, 3000, cogito::Effect::None, false);

    cogito::ToolCallContext ctx;
    ctx.idempotency_key = "idem-3";

    auto res = invoker.Invoke(std::move(permit), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Error);
    REQUIRE(res.error_code == "handler_exception");
    REQUIRE(res.content["status"] == "error");
    REQUIRE(res.content["tool"] == "sensor.panic");
  }
}

TEST_CASE("ToolInvoker cancellation and timeout classification (ADR-0001 R3)", "[invoker]") {
  FakeTestClock clock;
  cogito::ToolRegistry reg;

  // 1. Read-only tool (Effect::None)
  auto td_read = MakeTestTool("valve.read", cogito::Effect::None, 64 * 1024, nullptr,
                              [](const ccj::Json&, const cogito::ToolCallContext&) {
                                cogito::ToolResult r;
                                r.status = cogito::ToolResultStatus::Ok;
                                return r;
                              });

  // 2. Write tool (Effect::Write)
  auto td_write = MakeTestTool("valve.set", cogito::Effect::Write, 64 * 1024, nullptr,
                               [](const ccj::Json&, const cogito::ToolCallContext&) {
                                 cogito::ToolResult r;
                                 r.status = cogito::ToolResultStatus::Ok;
                                 return r;
                               });

  // 3. Destructive tool (Effect::Destructive)
  auto td_dest = MakeTestTool("valve.purge", cogito::Effect::Destructive, 64 * 1024, nullptr,
                              [](const ccj::Json&, const cogito::ToolCallContext&) {
                                cogito::ToolResult r;
                                r.status = cogito::ToolResultStatus::Ok;
                                return r;
                              });

  REQUIRE(reg.Register(std::move(td_read)).ok());
  REQUIRE(reg.Register(std::move(td_write)).ok());
  REQUIRE(reg.Register(std::move(td_dest)).ok());
  REQUIRE(reg.Freeze().ok());

  cogito::ToolInvoker invoker(reg, clock);

  std::atomic<bool> cancel_flag{true};
  cogito::CancelToken ctok{&cancel_flag};

  // Case A: Read tool cancelled -> ToolResultStatus::Cancelled
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a1").value(), cogito::Sha256("s1").value(), "valve.read",
        "k1", clock.MonotonicNs() + 5000, 3000, cogito::Effect::None);
    cogito::ToolCallContext ctx;
    ctx.cancel = &ctok;
    auto res = invoker.Invoke(std::move(p), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Cancelled);
  }

  // Case B: Write tool cancelled -> ToolResultStatus::Indeterminate
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a2").value(), cogito::Sha256("s2").value(), "valve.set",
        "k2", clock.MonotonicNs() + 5000, 3000, cogito::Effect::Write);
    cogito::ToolCallContext ctx;
    ctx.cancel = &ctok;
    auto res = invoker.Invoke(std::move(p), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Indeterminate);
  }

  // Case C: Destructive tool cancelled -> ToolResultStatus::Indeterminate
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a3").value(), cogito::Sha256("s3").value(), "valve.purge",
        "k3", clock.MonotonicNs() + 5000, 3000, cogito::Effect::Destructive);
    cogito::ToolCallContext ctx;
    ctx.cancel = &ctok;
    auto res = invoker.Invoke(std::move(p), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Indeterminate);
  }

  // Case D: Deadline exceeded on Write tool -> ToolResultStatus::Indeterminate
  {
    std::atomic<bool> no_cancel{false};
    cogito::CancelToken no_ctok{&no_cancel};

    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a4").value(), cogito::Sha256("s4").value(), "valve.set",
        "k4", clock.MonotonicNs() + 100, 3000, cogito::Effect::Write);

    cogito::ToolCallContext ctx;
    ctx.cancel = &no_ctok;
    ctx.deadline_ns = clock.MonotonicNs() - 10;  // already past deadline

    auto res = invoker.Invoke(std::move(p), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Indeterminate);
  }
}

TEST_CASE("ToolInvoker output validation (G0-27)", "[invoker]") {
  FakeTestClock clock;
  cogito::ToolRegistry reg;

  ccj::Json out_schema = ccj::Json{
      {"type", "object"},
      {"properties", {{"status", {{"type", "string"}}}, {"code", {{"type", "integer"}}}}},
      {"required", ccj::Json::array({"status", "code"})},
      {"additionalProperties", false}
  };

  // Tool with 100 byte max output and strict schema
  auto td = MakeTestTool("system.query", cogito::Effect::None, 100, out_schema,
                         [](const ccj::Json& args, const cogito::ToolCallContext&) {
                           cogito::ToolResult r;
                           r.status = cogito::ToolResultStatus::Ok;
                           if (args.contains("large") && args["large"].get<bool>()) {
                             r.content = ccj::Json{
                                 {"status", "ok"},
                                 {"code", 200},
                                 {"data", std::string(200, 'X')}  // Exceeds 100 bytes
                             };
                           } else if (args.contains("schema_violation") && args["schema_violation"].get<bool>()) {
                             r.content = ccj::Json{
                                 {"status", "ok"}  // Missing required "code"
                             };
                           } else {
                             r.content = ccj::Json{
                                 {"status", "ok"},
                                 {"code", 200}
                             };
                           }
                           return r;
                         });

  REQUIRE(reg.Register(std::move(td)).ok());
  REQUIRE(reg.Freeze().ok());

  cogito::ToolInvoker invoker(reg, clock);

  // 1. Normal valid output
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a1").value(), cogito::Sha256("s1").value(), "system.query",
        "k1", clock.MonotonicNs() + 5000, 3000, cogito::Effect::None);
    cogito::ToolCallContext ctx;
    auto res = invoker.Invoke(std::move(p), ccj::Json::object(), ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Ok);
    REQUIRE(res.content["status"] == "ok");
    REQUIRE(res.content["code"] == 200);
  }

  // 2. Output too large -> Error, truncated flag, MakeInvalidToolResult
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a2").value(), cogito::Sha256("s2").value(), "system.query",
        "k2", clock.MonotonicNs() + 5000, 3000, cogito::Effect::None);
    cogito::ToolCallContext ctx;
    auto res = invoker.Invoke(std::move(p), ccj::Json{{"large", true}}, ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Error);
    REQUIRE(res.error_code == cogito::reason::kInputTooLarge);
    REQUIRE(res.truncated == true);
    REQUIRE(res.content["status"] == "error");
    REQUIRE(res.content["reason_code"] == cogito::reason::kInputTooLarge);
  }

  // 3. Schema violation -> Error, MakeInvalidToolResult
  {
    auto p = cogito::testing::PermitTestSeam::Create(
        cogito::Sha256("a3").value(), cogito::Sha256("s3").value(), "system.query",
        "k3", clock.MonotonicNs() + 5000, 3000, cogito::Effect::None);
    cogito::ToolCallContext ctx;
    auto res = invoker.Invoke(std::move(p), ccj::Json{{"schema_violation", true}}, ctx);
    REQUIRE(res.status == cogito::ToolResultStatus::Error);
    REQUIRE(res.error_code == cogito::reason::kSchemaViolation);
    REQUIRE(res.content["status"] == "error");
    REQUIRE(res.content["reason_code"] == cogito::reason::kSchemaViolation);
  }
}
