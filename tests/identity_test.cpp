// SPDX-License-Identifier: Apache-2.0

#include "cogito/identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using cogito::Effect;
using cogito::ExecutionMode;

constexpr std::array<ExecutionMode, 4> kModes{
    ExecutionMode::Default,
    ExecutionMode::Plan,
    ExecutionMode::Edit,
    ExecutionMode::ReadOnly,
};

constexpr std::array<Effect, 3> kEffects{
    Effect::None,
    Effect::Write,
    Effect::Destructive,
};

std::uint8_t EffectRank(Effect effect) {
  switch (effect) {
    case Effect::None:
      return 0U;
    case Effect::Write:
      return 1U;
    case Effect::Destructive:
      return 2U;
  }
  return 0U;
}

cogito::Subject ValidSubject() {
  return cogito::Subject{"subject-1", {"operator"}, "oidc", "line-a"};
}

}  // namespace

TEST_CASE("Subject validation accepts exact byte boundaries", "[identity][subject]") {
  cogito::Subject subject = ValidSubject();
  subject.subject_id = std::string(128U, 's');
  subject.line_id = std::string(128U, 'l');
  subject.roles = {std::string(64U, 'r')};
  REQUIRE(cogito::ValidateSubject(subject).ok());

  for (const std::string auth_method : {"os_user", "badge", "oidc", "mtls"}) {
    subject.auth_method = auth_method;
    REQUIRE(cogito::ValidateSubject(subject).ok());
  }

  subject.roles.clear();
  REQUIRE(cogito::ValidateSubject(subject).ok());
  subject.line_id.clear();
  REQUIRE(cogito::ValidateSubject(subject).ok());
}

TEST_CASE("Subject validation rejects every invalid field", "[identity][subject]") {
  struct Case {
    cogito::Subject subject;
    cogito::Errc code;
    const char* reason_code;
  };

  cogito::Subject missing_id = ValidSubject();
  missing_id.subject_id.clear();
  cogito::Subject long_id = ValidSubject();
  long_id.subject_id = std::string(129U, 's');
  cogito::Subject missing_auth = ValidSubject();
  missing_auth.auth_method.clear();
  cogito::Subject unknown_auth = ValidSubject();
  unknown_auth.auth_method = "password";
  cogito::Subject long_line = ValidSubject();
  long_line.line_id = std::string(129U, 'l');
  cogito::Subject empty_role = ValidSubject();
  empty_role.roles = {""};
  cogito::Subject long_role = ValidSubject();
  long_role.roles = {std::string(65U, 'r')};

  const std::array<Case, 7> cases{{
      {std::move(missing_id), cogito::Errc::InvalidArgument,
       cogito::reason::kInputMissingField},
      {std::move(long_id), cogito::Errc::TooLarge,
       cogito::reason::kInputTooLarge},
      {std::move(missing_auth), cogito::Errc::InvalidArgument,
       cogito::reason::kInputMissingField},
      {std::move(unknown_auth), cogito::Errc::InvalidArgument, ""},
      {std::move(long_line), cogito::Errc::TooLarge,
       cogito::reason::kInputTooLarge},
      {std::move(empty_role), cogito::Errc::InvalidArgument,
       cogito::reason::kInputMissingField},
      {std::move(long_role), cogito::Errc::TooLarge,
       cogito::reason::kInputTooLarge},
  }};

  for (const Case& test_case : cases) {
    const cogito::Error error = cogito::ValidateSubject(test_case.subject);
    CAPTURE(test_case.subject.subject_id, test_case.subject.auth_method);
    REQUIRE(error.code == test_case.code);
    REQUIRE(error.reason_code == test_case.reason_code);
  }
}

TEST_CASE("Subject normalization sorts and removes duplicate roles",
          "[identity][subject]") {
  cogito::Subject subject{"subject", {"viewer", "operator", "viewer", "admin"},
                          "mtls", ""};
  cogito::NormalizeSubject(subject);
  REQUIRE(subject.roles ==
          std::vector<std::string>{"admin", "operator", "viewer"});

  const cogito::Subject normalized = subject;
  cogito::NormalizeSubject(subject);
  REQUIRE(subject.roles == normalized.roles);
}

TEST_CASE("Execution modes map to the G0-29 effect ceilings", "[identity][mode]") {
  REQUIRE(cogito::ModeToMaxEffect(ExecutionMode::Default) == Effect::Destructive);
  REQUIRE(cogito::ModeToMaxEffect(ExecutionMode::Edit) == Effect::Write);
  REQUIRE(cogito::ModeToMaxEffect(ExecutionMode::Plan) == Effect::None);
  REQUIRE(cogito::ModeToMaxEffect(ExecutionMode::ReadOnly) == Effect::None);

  REQUIRE(std::string(cogito::ToString(ExecutionMode::Default)) == "default");
  REQUIRE(std::string(cogito::ToString(ExecutionMode::Plan)) == "plan");
  REQUIRE(std::string(cogito::ToString(ExecutionMode::Edit)) == "edit");
  REQUIRE(std::string(cogito::ToString(ExecutionMode::ReadOnly)) == "readonly");
}

TEST_CASE("Execution mode resolution implements the complete 4 by 4 matrix",
          "[identity][mode]") {
  constexpr std::array<std::array<ExecutionMode, 4>, 4> expected_modes{{
      {ExecutionMode::Default, ExecutionMode::ReadOnly, ExecutionMode::Edit,
       ExecutionMode::ReadOnly},
      {ExecutionMode::Plan, ExecutionMode::Plan, ExecutionMode::Plan,
       ExecutionMode::Plan},
      {ExecutionMode::Edit, ExecutionMode::ReadOnly, ExecutionMode::Edit,
       ExecutionMode::ReadOnly},
      {ExecutionMode::ReadOnly, ExecutionMode::ReadOnly, ExecutionMode::ReadOnly,
       ExecutionMode::ReadOnly},
  }};

  for (std::size_t requested_index = 0; requested_index < kModes.size();
       ++requested_index) {
    const ExecutionMode requested = kModes[requested_index];
    for (std::size_t configured_index = 0; configured_index < kModes.size();
         ++configured_index) {
      const ExecutionMode configured = kModes[configured_index];
      const Effect requested_cap = cogito::ModeToMaxEffect(requested);
      const Effect configured_cap = cogito::ModeToMaxEffect(configured);
      const Effect expected_cap = EffectRank(requested_cap) < EffectRank(configured_cap)
                                      ? requested_cap
                                      : configured_cap;
      const Effect resolved_cap = cogito::ResolveEffectiveMaxEffect(requested, configured);
      const ExecutionMode resolved = cogito::ResolveExecutionMode(requested, configured);

      CAPTURE(cogito::ToString(requested), cogito::ToString(configured));
      REQUIRE(resolved_cap == expected_cap);
      REQUIRE(resolved == expected_modes[requested_index][configured_index]);
      REQUIRE(cogito::ModeToMaxEffect(resolved) == expected_cap);
      REQUIRE(EffectRank(cogito::ModeToMaxEffect(resolved)) <=
              EffectRank(configured_cap));
    }
  }
}

TEST_CASE("Every mode enforces its effect ceiling fail closed", "[identity][mode]") {
  for (const ExecutionMode mode : kModes) {
    for (const Effect effect : kEffects) {
      const bool allowed = EffectRank(effect) <=
                           EffectRank(cogito::ModeToMaxEffect(mode));
      CAPTURE(cogito::ToString(mode), cogito::ToString(effect));
      switch (mode) {
        case ExecutionMode::Default:
          REQUIRE(allowed);
          break;
        case ExecutionMode::Edit:
          REQUIRE(allowed == (effect != Effect::Destructive));
          break;
        case ExecutionMode::Plan:
        case ExecutionMode::ReadOnly:
          REQUIRE(allowed == (effect == Effect::None));
          break;
      }
    }
  }
}
