// SPDX-License-Identifier: Apache-2.0

#include "cogito/permit.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

cogito::Digest FilledDigest(std::uint8_t value) {
  cogito::Digest digest;
  digest.bytes.fill(value);
  return digest;
}

cogito::ExecutionPermit MakePermit(std::int64_t expires_ns = 1000,
                                   bool consumed = false) {
  return cogito::testing::PermitTestSeam::Create(
      FilledDigest(0x11U), FilledDigest(0x22U), "motor.start", "idem-key",
      expires_ns, 2500, cogito::Effect::Write, consumed);
}

void RequireApprovalError(const cogito::Error& error, const char* reason_code) {
  REQUIRE(error.code == cogito::Errc::ApprovalInvalid);
  REQUIRE(error.reason_code == reason_code);
}

}  // namespace

static_assert(!std::is_default_constructible<cogito::ExecutionPermit>::value,
              "Only PermissionGate and the test seam may create permits");
static_assert(!std::is_copy_constructible<cogito::ExecutionPermit>::value,
              "Permits must be move-only");
static_assert(!std::is_copy_assignable<cogito::ExecutionPermit>::value,
              "Permits must be move-only");
static_assert(std::is_nothrow_move_constructible<cogito::ExecutionPermit>::value,
              "Permit moves must not throw");
static_assert(std::is_nothrow_move_assignable<cogito::ExecutionPermit>::value,
              "Permit moves must not throw");

TEST_CASE("Permit exposes immutable issuance fields", "[permit]") {
  const cogito::ExecutionPermit permit = MakePermit();
  REQUIRE(permit.valid());
  REQUIRE(permit.action_digest() == FilledDigest(0x11U));
  REQUIRE(permit.permit_scope_digest() == FilledDigest(0x22U));
  REQUIRE(permit.tool_name() == "motor.start");
  REQUIRE(permit.idempotency_key() == "idem-key");
  REQUIRE(permit.expires_ns() == 1000);
  REQUIRE(permit.timeout_ms() == 2500);
  REQUIRE(permit.effect() == cogito::Effect::Write);
  REQUIRE(cogito::kVerdictTtlNs == 60'000'000'000LL);
}

TEST_CASE("Permit move construction invalidates the source", "[permit][move]") {
  cogito::ExecutionPermit source = MakePermit();
  cogito::ExecutionPermit destination(std::move(source));

  REQUIRE(destination.valid());
  REQUIRE(destination.action_digest() == FilledDigest(0x11U));
  REQUIRE(destination.permit_scope_digest() == FilledDigest(0x22U));
  REQUIRE(destination.tool_name() == "motor.start");
  REQUIRE(destination.idempotency_key() == "idem-key");
  REQUIRE(destination.expires_ns() == 1000);
  REQUIRE(destination.timeout_ms() == 2500);
  REQUIRE(destination.effect() == cogito::Effect::Write);
  REQUIRE_FALSE(source.valid());
  REQUIRE(source.action_digest().is_zero());
  REQUIRE(source.permit_scope_digest().is_zero());
  REQUIRE(source.tool_name().empty());
  REQUIRE(source.idempotency_key().empty());
  REQUIRE(source.expires_ns() == 0);
  REQUIRE(source.timeout_ms() == 0);

  const cogito::Error source_error =
      source.CheckUsable("motor.start", FilledDigest(0x22U), 999);
  REQUIRE(source_error.code == cogito::Errc::Internal);
  REQUIRE(source_error.reason_code.empty());
}

TEST_CASE("Permit move assignment transfers state and invalidates the source",
          "[permit][move]") {
  cogito::ExecutionPermit source = MakePermit(2000);
  cogito::ExecutionPermit destination = cogito::testing::PermitTestSeam::Create(
      FilledDigest(0x33U), FilledDigest(0x44U), "old.tool", "old-key", 3000,
      10, cogito::Effect::None);

  destination = std::move(source);
  REQUIRE(destination.valid());
  REQUIRE(destination.action_digest() == FilledDigest(0x11U));
  REQUIRE(destination.permit_scope_digest() == FilledDigest(0x22U));
  REQUIRE(destination.tool_name() == "motor.start");
  REQUIRE(destination.idempotency_key() == "idem-key");
  REQUIRE(destination.expires_ns() == 2000);
  REQUIRE(destination.timeout_ms() == 2500);
  REQUIRE(destination.effect() == cogito::Effect::Write);
  REQUIRE_FALSE(source.valid());
  REQUIRE(source.action_digest().is_zero());
  REQUIRE(source.permit_scope_digest().is_zero());
  REQUIRE(source.tool_name().empty());
  REQUIRE(source.idempotency_key().empty());
}

TEST_CASE("Permit moves never resurrect consumed authority", "[permit][move]") {
  cogito::ExecutionPermit consumed = MakePermit(2000, true);
  cogito::ExecutionPermit moved(std::move(consumed));
  REQUIRE_FALSE(consumed.valid());
  REQUIRE_FALSE(moved.valid());
  RequireApprovalError(
      moved.CheckUsable("motor.start", FilledDigest(0x22U), 1999),
      cogito::reason::kApprovalAlreadyConsumed);

  cogito::ExecutionPermit self = MakePermit(2000);
  cogito::ExecutionPermit* const address = &self;
  self = std::move(*address);
  REQUIRE(self.valid());
  REQUIRE(self.CheckUsable("motor.start", FilledDigest(0x22U), 1999).ok());
}

TEST_CASE("Permit is valid immediately before expiry and expired at the boundary",
          "[permit][expiry]") {
  const cogito::ExecutionPermit permit = MakePermit(1000);
  REQUIRE_FALSE(permit.IsExpired(999));
  REQUIRE(permit.CheckUsable("motor.start", FilledDigest(0x22U), 999).ok());

  REQUIRE(permit.IsExpired(1000));
  RequireApprovalError(
      permit.CheckUsable("motor.start", FilledDigest(0x22U), 1000),
      cogito::reason::kApprovalExpired);
  RequireApprovalError(
      permit.CheckUsable("motor.start", FilledDigest(0x22U), 1001),
      cogito::reason::kApprovalExpired);

  const cogito::ExecutionPermit latest = MakePermit(INT64_MAX);
  REQUIRE(latest.CheckUsable("motor.start", FilledDigest(0x22U),
                             INT64_MAX - 1LL).ok());
  RequireApprovalError(
      latest.CheckUsable("motor.start", FilledDigest(0x22U), INT64_MAX),
      cogito::reason::kApprovalExpired);

  const cogito::ExecutionPermit earliest = MakePermit(INT64_MIN);
  RequireApprovalError(
      earliest.CheckUsable("motor.start", FilledDigest(0x22U), INT64_MIN),
      cogito::reason::kApprovalExpired);
}

TEST_CASE("Permit rejects consumed, tool, and scope mismatches", "[permit][scope]") {
  cogito::ExecutionPermit consumed = MakePermit();
  cogito::testing::PermitTestSeam::Consume(consumed);
  cogito::testing::PermitTestSeam::Consume(consumed);
  REQUIRE_FALSE(consumed.valid());
  RequireApprovalError(
      consumed.CheckUsable("motor.start", FilledDigest(0x22U), 999),
      cogito::reason::kApprovalAlreadyConsumed);

  const cogito::ExecutionPermit permit = MakePermit();
  RequireApprovalError(permit.CheckUsable("motor.stop", FilledDigest(0x22U), 999),
                       cogito::reason::kApprovalScopeMismatch);
  RequireApprovalError(permit.CheckUsable("motor.start", FilledDigest(0x23U), 999),
                       cogito::reason::kApprovalScopeMismatch);
  REQUIRE(permit.valid());
}

TEST_CASE("Permit check order is deterministic and state preserving", "[permit][order]") {
  const cogito::ExecutionPermit invalid =
      cogito::testing::PermitTestSeam::Create({}, {}, "", "", 0, 0,
                                               cogito::Effect::Destructive, true);
  const cogito::Error invalid_error = invalid.CheckUsable("", {}, 0);
  REQUIRE(invalid_error.code == cogito::Errc::Internal);
  REQUIRE(invalid_error.reason_code.empty());

  const cogito::ExecutionPermit expired_and_consumed = MakePermit(1000, true);
  RequireApprovalError(
      expired_and_consumed.CheckUsable("wrong", FilledDigest(0x23U), 1000),
      cogito::reason::kApprovalExpired);

  const cogito::ExecutionPermit consumed_and_mismatched = MakePermit(1001, true);
  RequireApprovalError(
      consumed_and_mismatched.CheckUsable("wrong", FilledDigest(0x23U), 1000),
      cogito::reason::kApprovalAlreadyConsumed);
}
