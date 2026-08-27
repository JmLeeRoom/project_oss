// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class TempDirectory {
 public:
  TempDirectory() {
    static std::atomic<unsigned long long> sequence{0U};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
      path_ = base / ("cogito_config_test_" + std::to_string(timestamp) + "_" +
                      std::to_string(sequence.fetch_add(1U)));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error)) {
        return;
      }
      if (error) {
        throw std::runtime_error("unable to create a temporary test directory");
      }
    }
    throw std::runtime_error("unable to allocate a unique temporary test directory");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.is_open());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  REQUIRE(output.good());
}

void RequireConfigError(const cogito::Result<cogito::CogitoConfig>& result,
                        cogito::Errc expected) {
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error().code == expected);
}

cogito::ccj::Json StrictJson(std::string_view text) {
  auto parsed = cogito::ccj::ParseStrict(text);
  REQUIRE(parsed.ok());
  return std::move(parsed).take();
}

void RequireDigest(const cogito::Result<cogito::Digest>& digest,
                   std::string_view expected) {
  REQUIRE(digest.ok());
  REQUIRE(digest.value().hex() == expected);
}

}  // namespace

TEST_CASE("ConfigLoader fills defaults and accepts every SecretRef scheme", "[config]") {
  auto defaults = cogito::ConfigLoader::FromJsonString(R"json({"schema_version":1})json");
  REQUIRE(defaults.ok());
  REQUIRE(defaults.value().schema_version == 1U);
  REQUIRE(defaults.value().engine.max_concurrent_sessions == 64U);
  REQUIRE(defaults.value().engine.default_turn_timeout_ms == 30000U);
  REQUIRE(defaults.value().engine.log_level == "info");
  REQUIRE(defaults.value().secrets.empty());

  auto partial = cogito::ConfigLoader::FromJsonString(
      R"json({"schema_version":1,"engine":{"log_level":"debug"}})json");
  REQUIRE(partial.ok());
  REQUIRE(partial.value().engine.max_concurrent_sessions == 64U);
  REQUIRE(partial.value().engine.default_turn_timeout_ms == 30000U);
  REQUIRE(partial.value().engine.log_level == "debug");

  auto complete = cogito::ConfigLoader::FromJsonString(R"json({
    "schema_version": 1,
    "engine": {
      "max_concurrent_sessions": 1,
      "default_turn_timeout_ms": 3600000,
      "log_level": "trace"
    },
    "secrets": {
      "env_ref": "env:API_KEY",
      "file.ref": "file:/var/lib/cogito/key",
      "win-ref": "wincred:cogito/key",
      "keyring_ref": "keyring:cogito/user"
    }
  })json");
  REQUIRE(complete.ok());
  REQUIRE(complete.value().engine.max_concurrent_sessions == 1U);
  REQUIRE(complete.value().engine.default_turn_timeout_ms == 3600000U);
  REQUIRE(complete.value().engine.log_level == "trace");
  REQUIRE(complete.value().secrets.size() == 4U);
}

TEST_CASE("ConfigLoader preserves strict JSON parser error codes", "[config][strict]") {
  RequireConfigError(cogito::ConfigLoader::FromJsonString("{"),
                     cogito::Errc::InvalidArgument);
  RequireConfigError(cogito::ConfigLoader::FromJsonString(
                         R"json({"schema_version":1,"schema_version":1})json"),
                     cogito::Errc::DuplicateKey);

  std::string invalid_utf8 = R"json({"schema_version":1,"secrets":{"x":"env:)json";
  invalid_utf8.push_back(static_cast<char>(0xC0));
  invalid_utf8.push_back(static_cast<char>(0xAF));
  invalid_utf8 += R"json("}})json";
  RequireConfigError(cogito::ConfigLoader::FromJsonString(invalid_utf8),
                     cogito::Errc::NotUtf8);

  const std::string too_deep = std::string(33U, '[') + "0" + std::string(33U, ']');
  RequireConfigError(cogito::ConfigLoader::FromJsonString(too_deep),
                     cogito::Errc::DepthExceeded);

  RequireConfigError(
      cogito::ConfigLoader::FromJsonString(std::string(256U * 1024U + 1U, ' ')),
      cogito::Errc::TooLarge);

  std::string too_many_keys = "{";
  for (std::size_t index = 0U; index < 513U; ++index) {
    if (index != 0U) {
      too_many_keys.push_back(',');
    }
    too_many_keys += "\"k" + std::to_string(index) + "\":0";
  }
  too_many_keys.push_back('}');
  RequireConfigError(cogito::ConfigLoader::FromJsonString(too_many_keys),
                     cogito::Errc::TooLarge);
}

TEST_CASE("ConfigLoader rejects all schema violations", "[config][schema]") {
  const std::vector<std::string> invalid_documents{
      R"json([])json",
      R"json({})json",
      R"json({"schema_version":2})json",
      R"json({"schema_version":1.0})json",
      R"json({"schema_version":true})json",
      R"json({"schema_version":1,"unknown":0})json",
      R"json({"schema_version":1,"engine":null})json",
      R"json({"schema_version":1,"engine":{"unknown":0}})json",
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":0}})json",
      R"json({"schema_version":1,"engine":{"max_concurrent_sessions":65537}})json",
      R"json({"schema_version":1,"engine":{"default_turn_timeout_ms":99}})json",
      R"json({"schema_version":1,"engine":{"default_turn_timeout_ms":3600001}})json",
      R"json({"schema_version":1,"engine":{"log_level":"fatal"}})json",
      R"json({"schema_version":1,"secrets":[]})json",
      R"json({"schema_version":1,"secrets":{"": "env:X"}})json",
      R"json({"schema_version":1,"secrets":{"bad/key": "env:X"}})json",
      R"json({"schema_version":1,"secrets":{"x": 7}})json",
      R"json({"schema_version":1,"secrets":{"x": "ENV:X"}})json",
      R"json({"schema_version":1,"secrets":{"x": "env:"}})json",
  };
  for (const std::string& document : invalid_documents) {
    CAPTURE(document);
    const auto result = cogito::ConfigLoader::FromJsonString(document);
    RequireConfigError(result, cogito::Errc::SchemaViolation);
    REQUIRE(result.error().reason_code == cogito::reason::kSchemaViolation);
  }

  cogito::ccj::Json embedded_nul{
      {"schema_version", 1},
      {"secrets", {{"x", std::string("env:X\0hidden", 12U)}}}};
  RequireConfigError(cogito::ConfigLoader::FromJson(embedded_nul),
                     cogito::Errc::SchemaViolation);

  cogito::ccj::Json long_key{{"schema_version", 1},
                             {"secrets", {{std::string(129U, 'a'), "env:X"}}}};
  RequireConfigError(cogito::ConfigLoader::FromJson(long_key),
                     cogito::Errc::SchemaViolation);

  const std::string maximum_uri = "env:" + std::string(1020U, 'x');
  auto maximum = cogito::ConfigLoader::FromJson(
      cogito::ccj::Json{{"schema_version", 1}, {"secrets", {{"x", maximum_uri}}}});
  REQUIRE(maximum.ok());
  auto oversized = cogito::ConfigLoader::FromJson(cogito::ccj::Json{
      {"schema_version", 1}, {"secrets", {{"x", maximum_uri + "x"}}}});
  RequireConfigError(oversized, cogito::Errc::SchemaViolation);
}

TEST_CASE("CogitoConfig validation prevents direct construction bypasses", "[config]") {
  cogito::CogitoConfig config;
  REQUIRE(config.Validate().ok());

  config.schema_version = 0U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.schema_version = 1U;

  config.engine.max_concurrent_sessions = 0U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.max_concurrent_sessions = 65536U;
  REQUIRE(config.Validate().ok());

  config.engine.default_turn_timeout_ms = 99U;
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.default_turn_timeout_ms = 100U;
  config.engine.log_level = "warning";
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);
  config.engine.log_level = "error";

  config.secrets.emplace("bad/key", cogito::SecretRef{"env:X"});
  REQUIRE(config.Validate().code == cogito::Errc::SchemaViolation);

  const auto digest = config.ComputeDigest();
  REQUIRE_FALSE(digest.ok());
  REQUIRE(digest.error().code == cogito::Errc::SchemaViolation);
}

TEST_CASE("CogitoConfig normalization redacts file paths and matches golden digests",
          "[config][digest][golden]") {
  RequireDigest(cogito::ComputeConfigDigest(1U, StrictJson(R"json({"mode":"readonly"})json")),
                "91daa0313f6836bd456339804217c1fbc2ea64c56157133ad84031c08b081737");

  cogito::CogitoConfig config;
  config.secrets.emplace("api_key", cogito::SecretRef{"env:LLM_API_KEY"});
  config.secrets.emplace("db_pass", cogito::SecretRef{"file:/etc/secrets/db.pass"});
  config.secrets.emplace("keyring", cogito::SecretRef{"keyring:cogito/user"});
  config.secrets.emplace("windows", cogito::SecretRef{"wincred:cogito/key"});

  auto serialized = cogito::ccj::Serialize(config.ToNormalizedJson());
  REQUIRE(serialized.ok());
  REQUIRE(serialized.value() ==
          R"json({"engine":{"default_turn_timeout_ms":30000,"log_level":"info","max_concurrent_sessions":64},"secrets":{"api_key":"env:LLM_API_KEY","db_pass":"file:<redacted>","keyring":"keyring:cogito/user","windows":"wincred:cogito/key"}})json");

  cogito::CogitoConfig golden;
  golden.secrets.emplace("api_key", cogito::SecretRef{"env:LLM_API_KEY"});
  golden.secrets.emplace("db_pass", cogito::SecretRef{"file:/etc/secrets/db.pass"});
  RequireDigest(golden.ComputeDigest(),
                "77d1657862fdf0b228c554009d115ec0e9494b7b355809c0a7e01e4cc9964bd5");

  cogito::CogitoConfig defaults;
  RequireDigest(defaults.ComputeDigest(),
                "c78f2d4be2e13fec7057495c07eedb7ff3817e7a2a89ba7c12fa5038bd30fe64");

  cogito::CogitoConfig other_host = golden;
  other_host.secrets["db_pass"].uri = "file:/different/host/path";
  const auto golden_digest = golden.ComputeDigest();
  const auto other_host_digest = other_host.ComputeDigest();
  REQUIRE(golden_digest.ok());
  REQUIRE(other_host_digest.ok());
  REQUIRE(golden_digest.value() == other_host_digest.value());
}

TEST_CASE("ConfigLoader reads files with an exact 256 KiB boundary", "[config][file]") {
  TempDirectory temporary;
  const std::filesystem::path valid_path = temporary.path() / "valid.json";
  const std::filesystem::path maximum_path = temporary.path() / "maximum.json";
  const std::filesystem::path oversized_path = temporary.path() / "oversized.json";
  const std::filesystem::path malformed_path = temporary.path() / "malformed.json";

  WriteFile(valid_path,
            R"json({"schema_version":1,"engine":{"max_concurrent_sessions":8}})json");
  auto loaded = cogito::ConfigLoader::LoadFromFile(valid_path.string());
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().engine.max_concurrent_sessions == 8U);

  std::string maximum = R"json({"schema_version":1})json";
  maximum.resize(256U * 1024U, ' ');
  WriteFile(maximum_path, maximum);
  REQUIRE(cogito::ConfigLoader::LoadFromFile(maximum_path.string()).ok());

  maximum.push_back(' ');
  WriteFile(oversized_path, maximum);
  RequireConfigError(cogito::ConfigLoader::LoadFromFile(oversized_path.string()),
                     cogito::Errc::TooLarge);

  WriteFile(malformed_path, "{");
  RequireConfigError(cogito::ConfigLoader::LoadFromFile(malformed_path.string()),
                     cogito::Errc::InvalidArgument);
  RequireConfigError(
      cogito::ConfigLoader::LoadFromFile((temporary.path() / "missing.json").string()),
      cogito::Errc::ConfigError);
}
