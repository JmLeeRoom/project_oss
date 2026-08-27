// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

std::filesystem::path UniqueConfigPath(std::string_view label) {
  static std::atomic<std::uint64_t> sequence{0U};
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("cogito-s2-config-" + std::string(label) + "-" + std::to_string(tick) + "-" +
          std::to_string(sequence.fetch_add(1U)) + ".json");
}

class ScopedConfigFile {
 public:
  ScopedConfigFile(std::string_view label, std::string_view contents)
      : path_(UniqueConfigPath(label)) {
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      throw std::runtime_error("test configuration file open failed");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
      throw std::runtime_error("test configuration file write failed");
    }
  }

  ~ScopedConfigFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  ScopedConfigFile(const ScopedConfigFile&) = delete;
  ScopedConfigFile& operator=(const ScopedConfigFile&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

std::string Serialize(const cogito::ccj::Json& json) {
  auto serialized = cogito::ccj::Serialize(json);
  if (!serialized) {
    throw std::runtime_error("test JSON serialization failed");
  }
  return std::move(serialized).take();
}

cogito::ccj::Json StrictJson(std::string_view text) {
  auto parsed = cogito::ccj::ParseStrict(text);
  if (!parsed) {
    throw std::runtime_error("test JSON fixture is invalid");
  }
  return std::move(parsed).take();
}

void RequireSchemaViolation(std::string_view json) {
  auto result = cogito::ConfigLoader::FromJsonString(json);
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error().code == cogito::Errc::SchemaViolation);
}

}  // namespace

TEST_CASE("ConfigLoader supplies documented defaults", "[config][loader]") {
  auto loaded = cogito::ConfigLoader::FromJsonString(R"json({"schema_version":1})json");
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().schema_version == 1U);
  REQUIRE(loaded.value().engine.max_concurrent_sessions == 64U);
  REQUIRE(loaded.value().engine.default_turn_timeout_ms == 30000U);
  REQUIRE(loaded.value().engine.log_level == "info");
  REQUIRE(loaded.value().secrets.empty());

  auto explicit_config = cogito::ConfigLoader::FromJsonString(
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":7,"default_turn_timeout_ms":1250,"log_level":"debug"},"secrets":{"api_key":"env:API_KEY","db":"file:relative-is-schema-valid"}})json");
  REQUIRE(explicit_config.ok());
  REQUIRE(explicit_config.value().engine.max_concurrent_sessions == 7U);
  REQUIRE(explicit_config.value().engine.default_turn_timeout_ms == 1250U);
  REQUIRE(explicit_config.value().engine.log_level == "debug");
  REQUIRE(explicit_config.value().secrets.at("api_key").uri == "env:API_KEY");
  REQUIRE(explicit_config.value().secrets.at("db").uri ==
          "file:relative-is-schema-valid");
}

TEST_CASE("ConfigLoader preserves all strict parser error categories",
          "[config][loader][strict]") {
  auto malformed = cogito::ConfigLoader::FromJsonString(R"json({"schema_version":})json");
  REQUIRE_FALSE(malformed.ok());
  REQUIRE(malformed.error().code == cogito::Errc::InvalidArgument);

  auto duplicate = cogito::ConfigLoader::FromJsonString(
      R"json({"schema_version":1,"schema_version":1})json");
  REQUIRE_FALSE(duplicate.ok());
  REQUIRE(duplicate.error().code == cogito::Errc::DuplicateKey);

  std::string invalid_utf8 = "{\"schema_version\":1,\"secrets\":{\"x\":\"env:";
  invalid_utf8.push_back(static_cast<char>(0xC0));
  invalid_utf8 += "\"}}";
  auto not_utf8 = cogito::ConfigLoader::FromJsonString(invalid_utf8);
  REQUIRE_FALSE(not_utf8.ok());
  REQUIRE(not_utf8.error().code == cogito::Errc::NotUtf8);

  std::string too_deep(34U, '[');
  too_deep += "0";
  too_deep.append(34U, ']');
  auto depth = cogito::ConfigLoader::FromJsonString(too_deep);
  REQUIRE_FALSE(depth.ok());
  REQUIRE(depth.error().code == cogito::Errc::DepthExceeded);

  const std::string too_large(262145U, ' ');
  auto size = cogito::ConfigLoader::FromJsonString(too_large);
  REQUIRE_FALSE(size.ok());
  REQUIRE(size.error().code == cogito::Errc::TooLarge);

  std::string exact_limit = R"json({"schema_version":1})json";
  exact_limit.append(262144U - exact_limit.size(), ' ');
  REQUIRE(cogito::ConfigLoader::FromJsonString(exact_limit).ok());
}

TEST_CASE("ConfigLoader enforces root and additionalProperties constraints",
          "[config][schema]") {
  const std::string_view invalid_documents[] = {
      R"json(null)json",
      R"json([])json",
      R"json({})json",
      R"json({"schema_version":0})json",
      R"json({"schema_version":2})json",
      R"json({"schema_version":1.0})json",
      R"json({"schema_version":1,"unknown":true})json",
      R"json({"schema_version":1,"engine":null})json",
      R"json({"schema_version":1,"engine":{"unknown":1}})json",
      R"json({"schema_version":1,"secrets":[]})json",
  };
  for (const std::string_view document : invalid_documents) {
    INFO("invalid config: " << document);
    RequireSchemaViolation(document);
  }
}

TEST_CASE("ConfigLoader enforces every engine boundary", "[config][schema][engine]") {
  const std::string_view invalid_documents[] = {
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":0}})json",
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":65537}})json",
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":1.5}})json",
      R"json({"schema_version":1,"engine":{"default_turn_timeout_ms":99}})json",
      R"json({"schema_version":1,"engine":{"default_turn_timeout_ms":3600001}})json",
      R"json({"schema_version":1,"engine":{"default_turn_timeout_ms":-1}})json",
      R"json({"schema_version":1,"engine":{"log_level":"verbose"}})json",
      R"json({"schema_version":1,"engine":{"log_level":1}})json",
  };
  for (const std::string_view document : invalid_documents) {
    INFO("invalid config: " << document);
    RequireSchemaViolation(document);
  }

  auto boundaries = cogito::ConfigLoader::FromJsonString(
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":65536,"default_turn_timeout_ms":3600000,"log_level":"error"}})json");
  REQUIRE(boundaries.ok());
}

TEST_CASE("ConfigLoader enforces secret property names and generic URI shape",
          "[config][schema][secret]") {
  const std::string_view invalid_documents[] = {
      R"json({"schema_version":1,"secrets":{"":"env:X"}})json",
      R"json({"schema_version":1,"secrets":{"bad/name":"env:X"}})json",
      R"json({"schema_version":1,"secrets":{"name":1}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:"}})json",
      R"json({"schema_version":1,"secrets":{"name":"ENV:X"}})json",
      R"json({"schema_version":1,"secrets":{"name":"unknown:X"}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:X\u0000Y"}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:X\rY"}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:X\nY"}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:X\u2028Y"}})json",
      R"json({"schema_version":1,"secrets":{"name":"env:X\u2029Y"}})json",
  };
  for (const std::string_view document : invalid_documents) {
    INFO("invalid config: " << document);
    RequireSchemaViolation(document);
  }

  cogito::ccj::Json oversized_name = {
      {"schema_version", 1U},
      {"secrets", cogito::ccj::Json::object({{std::string(129U, 'a'), "env:X"}})},
  };
  auto name_result = cogito::ConfigLoader::FromJson(oversized_name);
  REQUIRE_FALSE(name_result.ok());
  REQUIRE(name_result.error().code == cogito::Errc::SchemaViolation);

  cogito::ccj::Json oversized_uri = {
      {"schema_version", 1U},
      {"secrets", {{"name", "env:" + std::string(1021U, 'A')}}},
  };
  auto uri_result = cogito::ConfigLoader::FromJson(oversized_uri);
  REQUIRE_FALSE(uri_result.ok());
  REQUIRE(uri_result.error().code == cogito::Errc::SchemaViolation);

  const std::string boundary_uri = "keyring:" + std::string(1016U, 'u');
  cogito::ccj::Json boundary = {
      {"schema_version", 1U},
      {"secrets", {{std::string(128U, 'n'), boundary_uri}}},
  };
  REQUIRE(cogito::ConfigLoader::FromJson(boundary).ok());
}

TEST_CASE("CogitoConfig Validate blocks programmatic schema bypasses",
          "[config][validate]") {
  cogito::CogitoConfig config;
  REQUIRE(config.Validate().ok());

  config.schema_version = 2U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.schema_version = 1U;

  config.engine.max_concurrent_sessions = 0U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.max_concurrent_sessions = 64U;
  config.engine.default_turn_timeout_ms = 99U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.default_turn_timeout_ms = 30000U;
  config.engine.log_level = "verbose";
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.log_level = "info";

  config.secrets.emplace("bad/name", cogito::SecretRef{"env:X"});
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.secrets.clear();
  config.secrets.emplace("valid-name", cogito::SecretRef{"env:"});
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);

  auto digest = config.ComputeDigest();
  REQUIRE_FALSE(digest.ok());
  REQUIRE(digest.error().code == cogito::Errc::SchemaViolation);
}

TEST_CASE("ToNormalizedJson is shape-only and redacts every file path",
          "[config][normalize]") {
  cogito::CogitoConfig config;
  config.engine.max_concurrent_sessions = 7U;
  config.engine.default_turn_timeout_ms = 2500U;
  config.engine.log_level = "warn";
  config.secrets.emplace("a", cogito::SecretRef{"env:API_KEY"});
  config.secrets.emplace("b", cogito::SecretRef{"file:/host/private/secret"});
  config.secrets.emplace("c", cogito::SecretRef{"wincred:target"});
  config.secrets.emplace("d", cogito::SecretRef{"keyring:service/user"});

  REQUIRE(Serialize(config.ToNormalizedJson()) ==
          R"json({"engine":{"default_turn_timeout_ms":2500,"log_level":"warn","max_concurrent_sessions":7},"secrets":{"a":"env:API_KEY","b":"file:<redacted>","c":"wincred:target","d":"keyring:service/user"}})json");
}

TEST_CASE("All three config digest golden vectors are stable", "[config][digest][golden]") {
  auto direct = cogito::ComputeConfigDigest(1U, StrictJson(R"json({"mode":"readonly"})json"));
  REQUIRE(direct.ok());
  REQUIRE(direct.value().hex() ==
          "91daa0313f6836bd456339804217c1fbc2ea64c56157133ad84031c08b081737");

  cogito::CogitoConfig canonical;
  canonical.secrets.emplace("api_key", cogito::SecretRef{"env:LLM_API_KEY"});
  canonical.secrets.emplace("db_pass", cogito::SecretRef{"file:/etc/secrets/db.pass"});
  REQUIRE(Serialize(canonical.ToNormalizedJson()) ==
          R"json({"engine":{"default_turn_timeout_ms":30000,"log_level":"info","max_concurrent_sessions":64},"secrets":{"api_key":"env:LLM_API_KEY","db_pass":"file:<redacted>"}})json");
  auto canonical_digest = canonical.ComputeDigest();
  REQUIRE(canonical_digest.ok());
  REQUIRE(canonical_digest.value().hex() ==
          "77d1657862fdf0b228c554009d115ec0e9494b7b355809c0a7e01e4cc9964bd5");

  const cogito::CogitoConfig defaults;
  REQUIRE(Serialize(defaults.ToNormalizedJson()) ==
          R"json({"engine":{"default_turn_timeout_ms":30000,"log_level":"info","max_concurrent_sessions":64},"secrets":{}})json");
  auto default_digest = defaults.ComputeDigest();
  REQUIRE(default_digest.ok());
  REQUIRE(default_digest.value().hex() ==
          "c78f2d4be2e13fec7057495c07eedb7ff3817e7a2a89ba7c12fa5038bd30fe64");
}

TEST_CASE("LoadFromFile preserves parse errors and enforces 256 KiB",
          "[config][loader][file]") {
  ScopedConfigFile valid("valid", R"json({"schema_version":1})json");
  auto loaded = cogito::ConfigLoader::LoadFromFile(valid.path().string());
  REQUIRE(loaded.ok());

  ScopedConfigFile malformed("malformed", R"json({"schema_version":})json");
  auto parse_error = cogito::ConfigLoader::LoadFromFile(malformed.path().string());
  REQUIRE_FALSE(parse_error.ok());
  REQUIRE(parse_error.error().code == cogito::Errc::InvalidArgument);

  std::string exact_contents = R"json({"schema_version":1})json";
  exact_contents.append(262144U - exact_contents.size(), ' ');
  ScopedConfigFile exact("exact-limit", exact_contents);
  REQUIRE(cogito::ConfigLoader::LoadFromFile(exact.path().string()).ok());

  ScopedConfigFile oversized("oversized", std::string(262145U, ' '));
  auto too_large = cogito::ConfigLoader::LoadFromFile(oversized.path().string());
  REQUIRE_FALSE(too_large.ok());
  REQUIRE(too_large.error().code == cogito::Errc::TooLarge);

  const std::filesystem::path missing = UniqueConfigPath("missing");
  auto absent = cogito::ConfigLoader::LoadFromFile(missing.string());
  REQUIRE_FALSE(absent.ok());
  REQUIRE(absent.error().code == cogito::Errc::ConfigError);
}
