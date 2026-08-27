// SPDX-License-Identifier: Apache-2.0

#include "cogito/conversation.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "cogito/action.hpp"
#include "cogito/canonical_json.hpp"
#include "cogito/context_compactor.hpp"
#include "cogito/result.hpp"

namespace ccj = cogito::ccj;

TEST_CASE("Role string conversion and parsing", "[conversation]") {
  REQUIRE(cogito::ToString(cogito::Role::System) == "system");
  REQUIRE(cogito::ToString(cogito::Role::User) == "user");
  REQUIRE(cogito::ToString(cogito::Role::Assistant) == "assistant");
  REQUIRE(cogito::ToString(cogito::Role::Tool) == "tool");

  auto r_sys = cogito::ParseRole("system");
  REQUIRE(r_sys.ok());
  REQUIRE(r_sys.value() == cogito::Role::System);

  auto r_usr = cogito::ParseRole("user");
  REQUIRE(r_usr.ok());
  REQUIRE(r_usr.value() == cogito::Role::User);

  auto r_ast = cogito::ParseRole("assistant");
  REQUIRE(r_ast.ok());
  REQUIRE(r_ast.value() == cogito::Role::Assistant);

  auto r_tol = cogito::ParseRole("tool");
  REQUIRE(r_tol.ok());
  REQUIRE(r_tol.value() == cogito::Role::Tool);

  auto r_inv = cogito::ParseRole("invalid_role");
  REQUIRE(!r_inv.ok());
  REQUIRE(r_inv.error().code == cogito::Errc::InvalidArgument);
}

TEST_CASE("ConversationStore management and system prompt updates", "[conversation]") {
  cogito::ConversationStore store;
  REQUIRE(store.empty());
  REQUIRE(store.size() == 0);

  // Inject initial system prompt
  store.SetSystemPrompt("You are a helpful industrial assistant.");
  REQUIRE(store.size() == 1);
  REQUIRE(store.messages()[0].role == cogito::Role::System);
  REQUIRE(store.messages()[0].content == "You are a helpful industrial assistant.");
  REQUIRE(store.messages()[0].provenance == "system:policy");
  REQUIRE(!store.messages()[0].untrusted);

  // Add User message
  cogito::Message u_msg;
  u_msg.role = cogito::Role::User;
  u_msg.content = "Read sensor temperature";
  u_msg.untrusted = false;
  u_msg.provenance = "user:op1";
  store.AddMessage(std::move(u_msg));
  REQUIRE(store.size() == 2);

  // Update existing system prompt (must replace in place at index 0)
  store.SetSystemPrompt("Updated system prompt.");
  REQUIRE(store.size() == 2);
  REQUIRE(store.messages()[0].role == cogito::Role::System);
  REQUIRE(store.messages()[0].content == "Updated system prompt.");
  REQUIRE(store.messages()[1].role == cogito::Role::User);

  // Clear store
  store.Clear();
  REQUIRE(store.empty());
  REQUIRE(store.size() == 0);
}

TEST_CASE("ContextCompactor NoopCompactor preserves all messages", "[conversation]") {
  auto noop = cogito::MakeNoopCompactor();
  REQUIRE(noop != nullptr);
  REQUIRE(noop->version() == "none-v1.0");

  std::vector<cogito::Message> msgs;
  cogito::Message m1;
  m1.role = cogito::Role::User;
  m1.content = "Hello";
  msgs.push_back(m1);

  auto res = noop->CompactIfNeeded(&msgs, 10);
  REQUIRE(res.ok());
  REQUIRE(!res.value().compacted);
  REQUIRE(res.value().messages_before == 1);
  REQUIRE(res.value().messages_after == 1);
  REQUIRE(res.value().compactor_version == "none-v1.0");
  REQUIRE(msgs.size() == 1);
}

TEST_CASE("DropOldestObservationCompactor preserves 3 protected elements and drops oldest observations", "[conversation]") {
  auto compactor = cogito::MakeDropOldestObservationCompactor();
  REQUIRE(compactor != nullptr);
  REQUIRE(compactor->version() == "drop-oldest-observation-v1.0");

  std::vector<cogito::Message> msgs;

  // 0: System prompt (Protected #1)
  cogito::Message sys;
  sys.role = cogito::Role::System;
  sys.content = "System policy guidelines";
  sys.untrusted = false;
  sys.provenance = "system:policy";
  msgs.push_back(sys);

  // 1: User turn 1
  cogito::Message u1;
  u1.role = cogito::Role::User;
  u1.content = "Run tool A";
  msgs.push_back(u1);

  // 2: Assistant turn 1 with pending action (Protected #2)
  cogito::Message a1;
  a1.role = cogito::Role::Assistant;
  a1.content = "Calling tool A";
  cogito::ActionRequest act1;
  act1.action_id = "act-111";
  act1.tool_name = "sensor.read";
  act1.arguments = ccj::Json{{"sensor_id", 1}};
  a1.actions.push_back(act1);
  msgs.push_back(a1);

  // 3: Tool observation 1 (Oldest observation -> Candidate for dropping)
  cogito::Message t1;
  t1.role = cogito::Role::Tool;
  t1.tool_call_id = "act-111";
  t1.content = "Very large tool observation output payload 1111111111111111111111111111111111111111111111111111111111111111";
  t1.untrusted = true;
  t1.provenance = "tool:sensor.read";
  msgs.push_back(t1);

  // 4: Assistant turn 2
  cogito::Message a2;
  a2.role = cogito::Role::Assistant;
  a2.content = "Calling tool B";
  msgs.push_back(a2);

  // 5: Tool observation 2 (Latest observation -> Protected #3)
  cogito::Message t2;
  t2.role = cogito::Role::Tool;
  t2.tool_call_id = "act-222";
  t2.content = "Recent tool observation output payload 2222222222222222222222222222222222222222222222222222222222222222";
  t2.untrusted = true;
  t2.provenance = "tool:sensor.read";
  msgs.push_back(t2);

  std::size_t total_before = msgs.size();
  REQUIRE(total_before == 6);

  // Compact with a soft limit that forces removal of t1 but preserves others
  std::size_t limit = 180;
  auto res = compactor->CompactIfNeeded(&msgs, limit);
  REQUIRE(res.ok());
  const auto& comp_out = res.value();

  REQUIRE(comp_out.compacted);
  REQUIRE(comp_out.messages_before == 6);
  REQUIRE(comp_out.messages_after == 5);
  REQUIRE(comp_out.compactor_version == "drop-oldest-observation-v1.0");
  REQUIRE(comp_out.removed_first == 3);
  REQUIRE(comp_out.removed_last == 4);

  // Verify remaining messages
  REQUIRE(msgs.size() == 5);
  REQUIRE(msgs[0].role == cogito::Role::System);
  REQUIRE(msgs[0].content == "System policy guidelines");

  REQUIRE(msgs[1].role == cogito::Role::User);

  REQUIRE(msgs[2].role == cogito::Role::Assistant);
  REQUIRE(msgs[2].actions.size() == 1);
  REQUIRE(msgs[2].actions[0].action_id == "act-111");

  REQUIRE(msgs[3].role == cogito::Role::Assistant);
  REQUIRE(msgs[3].content == "Calling tool B");

  // Latest observation (t2) MUST be preserved with untrusted and provenance intact
  REQUIRE(msgs[4].role == cogito::Role::Tool);
  REQUIRE(msgs[4].tool_call_id == "act-222");
  REQUIRE(msgs[4].untrusted == true);
  REQUIRE(msgs[4].provenance == "tool:sensor.read");
}

TEST_CASE("DropOldestObservationCompactor determinism and null handling", "[conversation]") {
  auto compactor = cogito::MakeDropOldestObservationCompactor();

  // Null input returns InvalidArgument
  auto err_res = compactor->CompactIfNeeded(nullptr, 100);
  REQUIRE(!err_res.ok());
  REQUIRE(err_res.error().code == cogito::Errc::InvalidArgument);

  // Empty messages vector
  std::vector<cogito::Message> empty_msgs;
  auto empty_res = compactor->CompactIfNeeded(&empty_msgs, 100);
  REQUIRE(empty_res.ok());
  REQUIRE(!empty_res.value().compacted);
  REQUIRE(empty_res.value().messages_before == 0);
  REQUIRE(empty_res.value().messages_after == 0);

  // Only system prompt: never compacted
  std::vector<cogito::Message> sys_only;
  cogito::Message s;
  s.role = cogito::Role::System;
  s.content = "Policy prompt that exceeds small limit";
  sys_only.push_back(s);
  auto sys_res = compactor->CompactIfNeeded(&sys_only, 10);
  REQUIRE(sys_res.ok());
  REQUIRE(!sys_res.value().compacted);
  REQUIRE(sys_only.size() == 1);
}
