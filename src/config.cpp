// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace cogito {
namespace {

constexpr std::size_t kMaxConfigBytes = 256U * 1024U;
constexpr std::size_t kMaxSecretKeyBytes = 128U;
constexpr std::size_t kMaxSecretUriBytes = 1024U;

Error SchemaError(const char* message) {
  return Error{Errc::SchemaViolation, reason::kSchemaViolation, message};
}

Error ConfigError(const char* message) {
  return Error{Errc::ConfigError, {}, message};
}

Error TooLargeError(const char* message) {
  return Error{Errc::TooLarge, reason::kInputTooLarge, message};
}

bool IsAsciiAlpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsValidSecretKey(std::string_view key) noexcept {
  if (key.empty() || key.size() > kMaxSecretKeyBytes) {
    return false;
  }
  for (const char value : key) {
    if (!IsAsciiAlpha(value) && (value < '0' || value > '9') && value != '_' &&
        value != '.' && value != '-') {
      return false;
    }
  }
  return true;
}

bool IsSupportedScheme(std::string_view scheme) noexcept {
  return scheme == "env" || scheme == "file" || scheme == "wincred" ||
         scheme == "keyring";
}

bool IsValidSecretUri(std::string_view uri) noexcept {
  if (uri.empty() || uri.size() > kMaxSecretUriBytes ||
      uri.find('\0') != std::string_view::npos ||
      uri.find_first_of("\r\n") != std::string_view::npos) {
    return false;
  }
  const std::size_t separator = uri.find(':');
  return separator != std::string_view::npos && separator > 0U &&
         separator + 1U < uri.size() && IsSupportedScheme(uri.substr(0U, separator));
}

bool IsValidLogLevel(std::string_view level) noexcept {
  return level == "trace" || level == "debug" || level == "info" ||
         level == "warn" || level == "error";
}

bool ReadUnsignedInteger(const ccj::Json& value, std::uint64_t& result) {
  if (value.type() == ccj::Json::value_t::number_unsigned) {
    result = value.get<std::uint64_t>();
    return true;
  }
  if (value.type() == ccj::Json::value_t::number_integer) {
    const std::int64_t signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
      return false;
    }
    result = static_cast<std::uint64_t>(signed_value);
    return true;
  }
  return false;
}

bool IsAllowedTopLevelKey(std::string_view key) noexcept {
  return key == "schema_version" || key == "engine" || key == "secrets";
}

bool IsAllowedEngineKey(std::string_view key) noexcept {
  return key == "max_concurrent_sessions" || key == "default_turn_timeout_ms" ||
         key == "log_level";
}

Result<CogitoConfig> ParseConfigObject(const ccj::Json& json) {
  if (!json.is_object()) {
    return SchemaError("configuration root must be an object");
  }
  for (auto member = json.cbegin(); member != json.cend(); ++member) {
    if (!IsAllowedTopLevelKey(member.key())) {
      return SchemaError("configuration contains an unknown top-level property");
    }
  }

  const auto schema_member = json.find("schema_version");
  if (schema_member == json.cend()) {
    return SchemaError("configuration is missing schema_version");
  }

  CogitoConfig config;
  std::uint64_t schema_version = 0U;
  if (!ReadUnsignedInteger(*schema_member, schema_version) || schema_version != 1U) {
    return SchemaError("schema_version must be integer 1");
  }
  config.schema_version = schema_version;

  const auto engine_member = json.find("engine");
  if (engine_member != json.cend()) {
    if (!engine_member->is_object()) {
      return SchemaError("engine must be an object");
    }
    for (auto member = engine_member->cbegin(); member != engine_member->cend();
         ++member) {
      if (!IsAllowedEngineKey(member.key())) {
        return SchemaError("engine contains an unknown property");
      }
    }

    const auto sessions_member = engine_member->find("max_concurrent_sessions");
    if (sessions_member != engine_member->cend()) {
      std::uint64_t sessions = 0U;
      if (!ReadUnsignedInteger(*sessions_member, sessions) || sessions < 1U ||
          sessions > 65536U) {
        return SchemaError("max_concurrent_sessions is outside its allowed range");
      }
      config.engine.max_concurrent_sessions = static_cast<std::uint32_t>(sessions);
    }

    const auto timeout_member = engine_member->find("default_turn_timeout_ms");
    if (timeout_member != engine_member->cend()) {
      std::uint64_t timeout = 0U;
      if (!ReadUnsignedInteger(*timeout_member, timeout) || timeout < 100U ||
          timeout > 3600000U) {
        return SchemaError("default_turn_timeout_ms is outside its allowed range");
      }
      config.engine.default_turn_timeout_ms = timeout;
    }

    const auto level_member = engine_member->find("log_level");
    if (level_member != engine_member->cend()) {
      if (!level_member->is_string()) {
        return SchemaError("log_level must be a string");
      }
      const std::string& level = level_member->get_ref<const std::string&>();
      if (!IsValidLogLevel(level)) {
        return SchemaError("log_level is not supported");
      }
      config.engine.log_level = level;
    }
  }

  const auto secrets_member = json.find("secrets");
  if (secrets_member != json.cend()) {
    if (!secrets_member->is_object()) {
      return SchemaError("secrets must be an object");
    }
    for (auto member = secrets_member->cbegin(); member != secrets_member->cend();
         ++member) {
      if (!IsValidSecretKey(member.key()) || !member->is_string()) {
        return SchemaError("secret property violates the configuration schema");
      }
      const std::string& uri = member->get_ref<const std::string&>();
      if (!IsValidSecretUri(uri)) {
        return SchemaError("secret reference URI violates the configuration schema");
      }
      config.secrets.emplace(member.key(), SecretRef{uri});
    }
  }

  if (Error error = config.Validate(); error) {
    return error;
  }
  return config;
}

}  // namespace

Error CogitoConfig::Validate() const {
  if (schema_version != 1U) {
    return SchemaError("schema_version must be 1");
  }
  if (engine.max_concurrent_sessions < 1U ||
      engine.max_concurrent_sessions > 65536U) {
    return SchemaError("max_concurrent_sessions is outside its allowed range");
  }
  if (engine.default_turn_timeout_ms < 100U ||
      engine.default_turn_timeout_ms > 3600000U) {
    return SchemaError("default_turn_timeout_ms is outside its allowed range");
  }
  if (!IsValidLogLevel(engine.log_level)) {
    return SchemaError("log_level is not supported");
  }
  for (const auto& secret : secrets) {
    if (!IsValidSecretKey(secret.first) || !IsValidSecretUri(secret.second.uri)) {
      return SchemaError("secret reference violates the configuration schema");
    }
  }
  return Error::Ok();
}

ccj::Json CogitoConfig::ToNormalizedJson() const {
  ccj::Json normalized_secrets = ccj::Json::object();
  for (const auto& secret : secrets) {
    if (secret.second.scheme() == "file") {
      normalized_secrets[secret.first] = "file:<redacted>";
    } else {
      normalized_secrets[secret.first] = secret.second.uri;
    }
  }

  return ccj::Json{
      {"engine",
       ccj::Json{{"default_turn_timeout_ms", engine.default_turn_timeout_ms},
                 {"log_level", engine.log_level},
                 {"max_concurrent_sessions", engine.max_concurrent_sessions}}},
      {"secrets", std::move(normalized_secrets)}};
}

Result<Digest> CogitoConfig::ComputeDigest() const {
  if (Error error = Validate(); error) {
    return error;
  }
  return ComputeConfigDigest(schema_version, ToNormalizedJson());
}

Result<CogitoConfig> ConfigLoader::FromJson(const ccj::Json& json) {
  try {
    return ParseConfigObject(json);
  } catch (const std::bad_alloc&) {
    return Error{Errc::Internal, {}, "out of memory while loading configuration"};
  } catch (const ccj::Json::exception&) {
    return SchemaError("configuration JSON value could not be inspected");
  } catch (...) {
    return Error{Errc::Internal, {}, "unexpected configuration loader failure"};
  }
}

Result<CogitoConfig> ConfigLoader::FromJsonString(std::string_view json_str) {
  auto parsed = ccj::ParseStrict(json_str);
  if (!parsed) {
    return parsed.error();
  }
  return FromJson(parsed.value());
}

Result<CogitoConfig> ConfigLoader::LoadFromFile(const std::string& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    return ConfigError("unable to open the configuration file");
  }

  std::string contents;
  std::array<char, 8192U> buffer{};
  for (;;) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) {
      const std::size_t byte_count = static_cast<std::size_t>(count);
      if (byte_count > kMaxConfigBytes - contents.size()) {
        return TooLargeError("configuration file exceeds its byte limit");
      }
      contents.append(buffer.data(), byte_count);
    }
    if (input.eof()) {
      break;
    }
    if (!input) {
      return ConfigError("unable to read the configuration file");
    }
  }
  return FromJsonString(contents);
}

}  // namespace cogito
