// SPDX-License-Identifier: Apache-2.0

#include "cogito/tool_schema.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using Json = cogito::ccj::Json;

std::unique_ptr<cogito::CompiledSchema> RequireCompiled(const Json& schema) {
  auto compiled = cogito::SchemaCompiler::Compile(schema);
  REQUIRE(compiled.ok());
  return std::move(compiled).take();
}

void RequireCompileFailure(const Json& schema) {
  auto compiled = cogito::SchemaCompiler::Compile(schema);
  REQUIRE_FALSE(compiled.ok());
  REQUIRE(compiled.error().code == cogito::Errc::SchemaCompileFailed);
}

Json PatternedString(std::string pattern, std::uint64_t max_length = 64U) {
  return Json{{"type", "string"},
              {"maxLength", max_length},
              {"pattern", std::move(pattern)}};
}

}  // namespace

TEST_CASE("SchemaCompiler validates the Draft-7 subset and runtime instances", "[schema]") {
  const Json schema{{"$schema", "http://json-schema.org/draft-07/schema#"},
                    {"type", "object"},
                    {"properties",
                     {{"name", PatternedString("^[a-z]+$", 12U)},
                      {"count", Json{{"type", "integer"}, {"minimum", 1}, {"maximum", 9}}},
                      {"enabled", Json{{"type", "boolean"}}}}},
                    {"required", Json::array({"name", "count"})},
                    {"additionalProperties", false}};
  auto compiled = RequireCompiled(schema);
  REQUIRE(compiled->audit().coverage == cogito::GrammarCoverage::Full);
  REQUIRE(compiled->audit().tier_v_keywords.empty());
  REQUIRE(std::string(cogito::ToString(compiled->audit().coverage)) == "full");

  const Json valid{{"name", "motor"}, {"count", 3}, {"enabled", true}};
  REQUIRE(compiled->Check(valid).empty());
  REQUIRE(compiled->Validate(valid).ok());

  const Json pattern_failure{{"name", "Motor-1"}, {"count", 3}};
  const cogito::Error pattern_error = compiled->Validate(pattern_failure);
  REQUIRE(pattern_error.code == cogito::Errc::SchemaViolation);
  REQUIRE(pattern_error.reason_code == cogito::reason::kSchemaViolation);
  REQUIRE(pattern_error.detail.find("Motor-1") == std::string::npos);

  const Json missing_required{{"name", "motor"}};
  REQUIRE(compiled->Validate(missing_required).code == cogito::Errc::SchemaViolation);
  const Json unknown_property{{"name", "motor"}, {"count", 3}, {"extra", 1}};
  REQUIRE(compiled->Validate(unknown_property).code == cogito::Errc::SchemaViolation);
}

TEST_CASE("Pattern is removed before upstream validation and dot matches one Unicode scalar",
          "[schema][pattern][utf8]") {
  auto compiled = RequireCompiled(PatternedString("^.$", 1U));
  REQUIRE(compiled->Check("a").empty());
  REQUIRE(compiled->Check(u8"한").empty());
  REQUIRE_FALSE(compiled->Check("ab").empty());
  REQUIRE(compiled->Validate(u8"한").ok());
}

TEST_CASE("GrammarCoverage is deterministic for structural, Tier-G, and Tier-V schemas",
          "[schema][coverage]") {
  const Json structural{{"type", "object"},
                        {"properties", {{"value", Json{{"type", "string"}}}}},
                        {"required", Json::array({"value"})},
                        {"additionalProperties", false}};
  auto none = RequireCompiled(structural);
  REQUIRE(none->audit().coverage == cogito::GrammarCoverage::None);
  REQUIRE(none->audit().tier_v_keywords.empty());
  REQUIRE(std::string(cogito::ToString(none->audit().coverage)) == "none");

  auto full = RequireCompiled(Json{{"type", "integer"}, {"minimum", 0}, {"maximum", 10}});
  REQUIRE(full->audit().coverage == cogito::GrammarCoverage::Full);
  REQUIRE(full->audit().tier_v_keywords.empty());

  auto integral_number =
      RequireCompiled(Json{{"type", "number"}, {"minimum", 0}, {"maximum", 10}});
  REQUIRE(integral_number->audit().coverage == cogito::GrammarCoverage::Partial);
  const std::vector<std::string> integral_number_keywords{"number_bounds", "type:number"};
  REQUIRE(integral_number->audit().tier_v_keywords == integral_number_keywords);

  const Json partial_schema{{"type", "array"},
                            {"minItems", 1},
                            {"maxItems", 4},
                            {"items",
                             Json{{"type", "number"},
                                  {"minimum", 0.25},
                                  {"maximum", 4.5},
                                  {"exclusiveMinimum", 0.0},
                                  {"const", 1.0}}}};
  auto partial = RequireCompiled(partial_schema);
  REQUIRE(partial->audit().coverage == cogito::GrammarCoverage::Partial);
  const std::vector<std::string> expected{
      "const", "exclusive_bounds", "items", "max_items", "min_items",
      "number_bounds", "type:number"};
  REQUIRE(partial->audit().tier_v_keywords == expected);
  REQUIRE(std::string(cogito::ToString(partial->audit().coverage)) == "partial");
}

TEST_CASE("Forbidden and unknown schema keywords fail closed at every schema depth",
          "[schema][whitelist]") {
  const std::vector<std::string> forbidden{
      "$ref",          "definitions",        "$id",       "patternProperties",
      "dependencies",  "allOf",             "anyOf",     "oneOf",
      "not",           "if",                "then",      "else",
      "propertyNames", "contains",           "additionalItems",
      "default",       "format",             "uniqueItems", "multipleOf"};
  for (const std::string& keyword : forbidden) {
    CAPTURE(keyword);
    Json root = Json::object();
    root[keyword] = Json::object();
    RequireCompileFailure(root);

    Json nested{{"type", "object"},
                {"properties", {{"value", Json{{keyword, Json::object()}}}}}};
    RequireCompileFailure(nested);

    Json items{{"type", "array"}, {"items", Json{{keyword, Json::object()}}}};
    RequireCompileFailure(items);
  }

  RequireCompileFailure(Json{{"type", "string"}, {"futureKeyword", true}});

  const Json property_named_ref{{"type", "object"},
                                {"properties", {{"$ref", Json{{"type", "string"}}}}},
                                {"additionalProperties", false}};
  auto compiled = RequireCompiled(property_named_ref);
  REQUIRE(compiled->Validate(Json{{"$ref", "ordinary-property"}}).ok());
}

TEST_CASE("Schema keyword value shapes are checked before upstream compilation",
          "[schema][shape]") {
  RequireCompileFailure(Json{{"$schema", "https://json-schema.org/draft/2020-12/schema"}});
  RequireCompileFailure(Json{{"type", Json::array({"string", "null"})}});
  RequireCompileFailure(Json{{"type", "object"}, {"additionalProperties", true}});
  RequireCompileFailure(Json{{"type", "object"}, {"additionalProperties", Json::object()}});
  RequireCompileFailure(Json{{"type", "array"}, {"items", Json::array()}});
  RequireCompileFailure(Json{{"type", "string"}, {"minLength", -1}});
  RequireCompileFailure(Json{{"type", "string"}, {"enum", Json::array({true})}});
  RequireCompileFailure(Json{{"type", "integer"}, {"minimum", 0.5}});
  RequireCompileFailure(Json{{"type", "string"}, {"minimum", 0}});
  RequireCompileFailure(Json{{"minimum", 0}});
  RequireCompileFailure(Json{{"type", "object"}, {"required", Json::array({"x", "x"})}});
  RequireCompileFailure(Json{{"type", "object"},
                             {"properties", {{"x", Json{{"title", "nested metadata"}}}}}});
}

TEST_CASE("Pattern static safety limits reject unsafe production schemas", "[schema][pattern]") {
  RequireCompileFailure(PatternedString("[a-z]+"));

  Json missing_max{{"type", "string"}, {"pattern", "^[a-z]+$"}};
  RequireCompileFailure(missing_max);
  RequireCompileFailure(PatternedString("^[a-z]+$", cogito::kMaxMatchStringBytes + 1U));

  const std::string too_long = "^" + std::string(cogito::kMaxPatternBytes, 'a') + "$";
  RequireCompileFailure(PatternedString(too_long));
  RequireCompileFailure(PatternedString("^a{1,1025}$"));
  RequireCompileFailure(PatternedString("^a{2}$"));
  RequireCompileFailure(PatternedString("^(a+)+$"));
  RequireCompileFailure(PatternedString("^(ab)+$"));
  RequireCompileFailure(PatternedString("^(?=a)a$"));
  RequireCompileFailure(PatternedString("^(a)\\1$"));

  std::string product = "^";
  for (int i = 0; i < 9; ++i) {
    product += "(a|b)";
  }
  product += "$";
  RequireCompileFailure(PatternedString(product));
  RequireCompileFailure(PatternedString("^(a|aa){1,64}$"));
}

TEST_CASE("Pattern plans traverse nested objects and array items deterministically",
          "[schema][pattern][nested]") {
  const Json schema{
      {"type", "object"},
      {"properties",
       {{"rows",
         Json{{"type", "array"},
              {"items",
               Json{{"type", "object"},
                    {"properties", {{"code", PatternedString("^[A-Z]{2,2}$", 2U)}}},
                    {"additionalProperties", false}}}}}}}};
  auto compiled = RequireCompiled(schema);
  REQUIRE(compiled->audit().coverage == cogito::GrammarCoverage::Partial);
  REQUIRE(compiled->Validate(Json::object()).ok());
  REQUIRE(compiled->Validate(Json{{"rows", Json::array({Json{{"code", "AB"}},
                                                         Json{{"code", "CD"}}})}})
              .ok());
  REQUIRE(compiled->Validate(Json{{"rows", Json::array({Json{{"code", "A1"}}})}}).code ==
          cogito::Errc::SchemaViolation);
}

TEST_CASE("Schema diagnostics are bounded and never include offending argument text",
          "[schema][diagnostic]") {
  const std::string long_name(600U, 'p');
  const Json schema{{"type", "object"},
                    {"properties", {{long_name, Json{{"type", "integer"}}}}},
                    {"additionalProperties", false}};
  auto compiled = RequireCompiled(schema);
  const std::string diagnostic = compiled->Check(Json{{long_name, "sensitive-value"}});
  REQUIRE(diagnostic.size() <= cogito::kMaxDiagnosticBytes);
  REQUIRE(diagnostic.size() >= 3U);
  REQUIRE(diagnostic.substr(diagnostic.size() - 3U) == "...");
  REQUIRE(diagnostic.find("sensitive-value") == std::string::npos);

  std::string unicode_name;
  for (std::size_t i = 0U; i < 200U; ++i) {
    unicode_name += u8"한";
  }
  const Json unicode_schema{{"type", "object"},
                            {"properties", {{unicode_name, Json{{"type", "integer"}}}}},
                            {"additionalProperties", false}};
  auto unicode_compiled = RequireCompiled(unicode_schema);
  const std::string unicode_diagnostic =
      unicode_compiled->Check(Json{{unicode_name, "sensitive-value"}});
  REQUIRE(unicode_diagnostic.size() <= cogito::kMaxDiagnosticBytes);
  REQUIRE(unicode_diagnostic.substr(unicode_diagnostic.size() - 3U) == "...");
  REQUIRE_NOTHROW(Json(unicode_diagnostic).dump());
}

TEST_CASE("MatcherTestSeam exposes exact match, UTF-8, and budget outcomes",
          "[schema][nfa][seam]") {
  auto ascii_exhausted =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^a$", "a", 1U);
  REQUIRE_FALSE(ascii_exhausted.ok());
  REQUIRE(ascii_exhausted.error().code == cogito::Errc::PatternBudgetExhausted);
  auto ascii = cogito::testing::MatcherTestSeam::DirectNfaMatch("^a$", "a", 2U);
  REQUIRE(ascii.ok());
  REQUIRE(ascii.value());
  auto no_match = cogito::testing::MatcherTestSeam::DirectNfaMatch("^a$", "b", 2U);
  REQUIRE(no_match.ok());
  REQUIRE_FALSE(no_match.value());

  auto unicode = cogito::testing::MatcherTestSeam::DirectNfaMatch("^.$", u8"한", 4U);
  REQUIRE(unicode.ok());
  REQUIRE(unicode.value());
  auto unicode_exhausted =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^.$", u8"한", 3U);
  REQUIRE_FALSE(unicode_exhausted.ok());
  REQUIRE(unicode_exhausted.error().code == cogito::Errc::PatternBudgetExhausted);
  REQUIRE(unicode_exhausted.error().reason_code == cogito::reason::kPatternTimeout);

  auto zero_budget = cogito::testing::MatcherTestSeam::DirectNfaMatch("a", "a", 0U);
  REQUIRE_FALSE(zero_budget.ok());
  REQUIRE(zero_budget.error().code == cogito::Errc::PatternBudgetExhausted);
  auto malformed = cogito::testing::MatcherTestSeam::DirectNfaMatch("(", "a", 100U);
  REQUIRE_FALSE(malformed.ok());
  REQUIRE(malformed.error().code == cogito::Errc::SchemaCompileFailed);

  const std::string invalid_utf8(1U, static_cast<char>(0xC3));
  auto invalid = cogito::testing::MatcherTestSeam::DirectNfaMatch(".", invalid_utf8, 100U);
  REQUIRE_FALSE(invalid.ok());
  REQUIRE(invalid.error().code == cogito::Errc::SchemaViolation);
  auto invalid_with_zero_budget =
      cogito::testing::MatcherTestSeam::DirectNfaMatch(".", invalid_utf8, 0U);
  REQUIRE_FALSE(invalid_with_zero_budget.ok());
  REQUIRE(invalid_with_zero_budget.error().code == cogito::Errc::SchemaViolation);
  auto oversized = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      "a*", std::string(cogito::kMaxMatchStringBytes + 1U, 'a'), 200000U);
  REQUIRE_FALSE(oversized.ok());
  REQUIRE(oversized.error().code == cogito::Errc::SchemaViolation);
}

TEST_CASE("MatcherTestSeam accounts structural epsilon transitions exactly",
          "[schema][nfa][budget]") {
  auto grouped_exhausted =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^(a)$", "a", 3U);
  REQUIRE_FALSE(grouped_exhausted.ok());
  REQUIRE(grouped_exhausted.error().code == cogito::Errc::PatternBudgetExhausted);
  auto grouped = cogito::testing::MatcherTestSeam::DirectNfaMatch("^(a)$", "a", 4U);
  REQUIRE(grouped.ok());
  REQUIRE(grouped.value());

  auto optional_exhausted =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^a?$", "", 2U);
  REQUIRE_FALSE(optional_exhausted.ok());
  REQUIRE(optional_exhausted.error().code == cogito::Errc::PatternBudgetExhausted);
  auto optional = cogito::testing::MatcherTestSeam::DirectNfaMatch("^a?$", "", 3U);
  REQUIRE(optional.ok());
  REQUIRE(optional.value());

  auto plus_empty = cogito::testing::MatcherTestSeam::DirectNfaMatch("^a+$", "", 1U);
  REQUIRE(plus_empty.ok());
  REQUIRE_FALSE(plus_empty.value());

  auto reentry_exhausted =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^(a?|(b?))$", "", 10U);
  REQUIRE_FALSE(reentry_exhausted.ok());
  REQUIRE(reentry_exhausted.error().code == cogito::Errc::PatternBudgetExhausted);
  auto reentry =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("^(a?|(b?))$", "", 11U);
  REQUIRE(reentry.ok());
  REQUIRE(reentry.value());
}

TEST_CASE("MatcherTestSeam dot consumes one valid Unicode scalar and excludes CR LF",
          "[schema][nfa][utf8]") {
  const std::vector<std::pair<std::string, std::uint64_t>> matches{
      {"a", 2U}, {u8"¢", 3U}, {u8"한", 4U}, {u8"😀", 5U}};
  for (const auto& test : matches) {
    CAPTURE(test.first, test.second);
    auto result =
        cogito::testing::MatcherTestSeam::DirectNfaMatch("^.$", test.first, test.second);
    REQUIRE(result.ok());
    REQUIRE(result.value());
  }

  for (const std::string& input : {std::string("\n"), std::string("\r")}) {
    auto result = cogito::testing::MatcherTestSeam::DirectNfaMatch("^.$", input, 2U);
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value());
  }

  const std::vector<std::string> invalid_utf8{
      std::string{"\xC0\x80", 2U}, std::string{"\xED\xA0\x80", 3U},
      std::string{"\xF4\x90\x80\x80", 4U}};
  for (const std::string& input : invalid_utf8) {
    auto result = cogito::testing::MatcherTestSeam::DirectNfaMatch("^.$", input, 100U);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == cogito::Errc::SchemaViolation);
  }
}

TEST_CASE("MatcherTestSeam bypasses exactly the five production static limits",
          "[schema][nfa][seam]") {
  auto no_anchors = cogito::testing::MatcherTestSeam::DirectNfaMatch("a", "a", 10U);
  REQUIRE(no_anchors.ok());
  REQUIRE(no_anchors.value());

  const std::string long_pattern(257U, 'a');
  auto length_bypass = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      long_pattern, std::string(257U, 'a'), 1000U);
  REQUIRE(length_bypass.ok());
  REQUIRE(length_bypass.value());

  auto quantifier_bypass = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      "a{1025,1025}", std::string(1025U, 'a'), 2000U);
  REQUIRE(quantifier_bypass.ok());
  REQUIRE(quantifier_bypass.value());

  auto product_bypass = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      "(a|aa){1,64}", std::string(64U, 'a') + "b", 10U);
  REQUIRE_FALSE(product_bypass.ok());
  REQUIRE(product_bypass.error().code == cogito::Errc::PatternBudgetExhausted);

  auto catastrophic = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      "(a|aa){1,2048}", std::string(2048U, 'a') + "b",
      cogito::kPatternMatchStepBudget);
  REQUIRE_FALSE(catastrophic.ok());
  REQUIRE(catastrophic.error().code == cogito::Errc::PatternBudgetExhausted);
  REQUIRE(catastrophic.error().reason_code == cogito::reason::kPatternTimeout);

  auto nested_still_rejected =
      cogito::testing::MatcherTestSeam::DirectNfaMatch("(a+)+", "aaaa", 1000U);
  REQUIRE_FALSE(nested_still_rejected.ok());
  REQUIRE(nested_still_rejected.error().code == cogito::Errc::SchemaCompileFailed);

  const std::string deepest_supported = std::string(256U, '(') + "a" +
                                        std::string(256U, ')');
  auto bounded_depth = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      deepest_supported, "a", 514U);
  REQUIRE(bounded_depth.ok());
  REQUIRE(bounded_depth.value());

  const std::string too_deep = std::string(257U, '(') + "a" +
                               std::string(257U, ')');
  auto rejected_depth =
      cogito::testing::MatcherTestSeam::DirectNfaMatch(too_deep, "a", 1000U);
  REQUIRE_FALSE(rejected_depth.ok());
  REQUIRE(rejected_depth.error().code == cogito::Errc::SchemaCompileFailed);

  auto rejected_state_explosion = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      "a{999999999,999999999}", "a", cogito::kPatternMatchStepBudget);
  REQUIRE_FALSE(rejected_state_explosion.ok());
  REQUIRE(rejected_state_explosion.error().code == cogito::Errc::SchemaCompileFailed);

  const std::string ast_explosion =
      "(" + std::string(cogito::kMaxPatternBytes * cogito::kMaxQuantifierBound, 'a') +
      "){0,0}";
  auto rejected_ast_explosion = cogito::testing::MatcherTestSeam::DirectNfaMatch(
      ast_explosion, "", cogito::kPatternMatchStepBudget);
  REQUIRE_FALSE(rejected_ast_explosion.ok());
  REQUIRE(rejected_ast_explosion.error().code == cogito::Errc::SchemaCompileFailed);
}

TEST_CASE("CompiledSchema maps public pattern exhaustion to the stable sentinel and reason",
          "[schema][nfa][budget]") {
  auto compiled = RequireCompiled(PatternedString("^[a-z]*$", cogito::kMaxMatchStringBytes));
  const Json input = std::string(cogito::kMaxMatchStringBytes, 'a');
  REQUIRE(compiled->Check(input) == "pattern_budget_exhausted");
  const cogito::Error error = compiled->Validate(input);
  REQUIRE(error.code == cogito::Errc::PatternBudgetExhausted);
  REQUIRE(error.reason_code == cogito::reason::kPatternTimeout);
}
