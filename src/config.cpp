// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace cogito {
namespace {

constexpr std::size_t kMaxConfigBytes = 256U * 1024U;
constexpr std::size_t kMaxSecretUriBytes = 1024U;
constexpr std::size_t kMaxSecretNameBytes = 128U;

Error SchemaError(const char* message) {
  return Error{Errc::SchemaViolation, reason::kSchemaViolation, message};
}

Error ConfigError(const char* message) {
  return Error{Errc::ConfigError, {}, message};
}

Error TooLargeError(const char* message) {
  return Error{Errc::TooLarge, reason::kInputTooLarge, message};
}

Error InternalError(const char* message) {
  return Error{Errc::Internal, {}, message};
}

bool IsAsciiAlpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(char value) noexcept { return value >= '0' && value <= '9'; }

bool IsValidSecretName(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxSecretNameBytes) {
    return false;
  }
  for (const char value : name) {
    if (!IsAsciiAlpha(value) && !IsAsciiDigit(value) && value != '_' && value != '.' &&
        value != '-') {
      return false;
    }
  }
  return true;
}

bool HasSupportedScheme(std::string_view scheme) noexcept {
  return scheme == "env" || scheme == "file" || scheme == "wincred" ||
         scheme == "keyring";
}

bool ContainsRegexLineTerminator(std::string_view text) noexcept {
  for (std::size_t index = 0U; index < text.size(); ++index) {
    const unsigned char value = static_cast<unsigned char>(text[index]);
    if (value == static_cast<unsigned char>('\r') ||
        value == static_cast<unsigned char>('\n')) {
      return true;
    }
    if (index + 2U < text.size() && value == 0xE2U &&
        static_cast<unsigned char>(text[index + 1U]) == 0x80U &&
        (static_cast<unsigned char>(text[index + 2U]) == 0xA8U ||
         static_cast<unsigned char>(text[index + 2U]) == 0xA9U)) {
      return true;
    }
  }
  return false;
}

bool IsValidSecretUri(std::string_view uri) noexcept {
  if (uri.empty() || uri.size() > kMaxSecretUriBytes ||
      uri.find('\0') != std::string_view::npos || ContainsRegexLineTerminator(uri)) {
    return false;
  }
  const std::size_t separator = uri.find(':');
  return separator != std::string_view::npos && separator != 0U &&
         separator + 1U < uri.size() && HasSupportedScheme(uri.substr(0U, separator));
}

bool IsAllowedTopLevelKey(std::string_view key) noexcept {
  return key == "schema_version" || key == "engine" || key == "secrets";
}

bool IsAllowedEngineKey(std::string_view key) noexcept {
  return key == "max_concurrent_sessions" || key == "default_turn_timeout_ms" ||
         key == "log_level";
}

bool ReadUnsignedInteger(const ccj::Json& value, std::uint64_t& output) noexcept {
  if (value.is_number_unsigned()) {
    output = value.get<std::uint64_t>();
    return true;
  }
  if (!value.is_number_integer()) {
    return false;
  }
  const std::int64_t signed_value = value.get<std::int64_t>();
  if (signed_value < 0) {
    return false;
  }
  output = static_cast<std::uint64_t>(signed_value);
  return true;
}

bool IsValidLogLevel(std::string_view level) noexcept {
  return level == "trace" || level == "debug" || level == "info" ||
         level == "warn" || level == "error";
}

}  // namespace

Error CogitoConfig::Validate() const {
  if (schema_version != 1U) {
    return SchemaError("schema_version must be 1");
  }
  if (engine.max_concurrent_sessions < 1U || engine.max_concurrent_sessions > 65536U) {
    return SchemaError("max_concurrent_sessions is outside the allowed range");
  }
  if (engine.default_turn_timeout_ms < 100U ||
      engine.default_turn_timeout_ms > 3600000U) {
    return SchemaError("default_turn_timeout_ms is outside the allowed range");
  }
  if (!IsValidLogLevel(engine.log_level)) {
    return SchemaError("log_level is not supported");
  }
  for (const auto& entry : secrets) {
    if (!IsValidSecretName(entry.first)) {
      return SchemaError("a secret name violates the configuration schema");
    }
    if (!IsValidSecretUri(entry.second.uri)) {
      return SchemaError("a secret reference violates the configuration schema");
    }
  }
  return Error::Ok();
}

ccj::Json CogitoConfig::ToNormalizedJson() const {
  ccj::Json normalized = ccj::Json::object();
  normalized["engine"] = ccj::Json{
      {"default_turn_timeout_ms", engine.default_turn_timeout_ms},
      {"log_level", engine.log_level},
      {"max_concurrent_sessions", engine.max_concurrent_sessions},
  };

  ccj::Json normalized_secrets = ccj::Json::object();
  for (const auto& entry : secrets) {
    normalized_secrets[entry.first] =
        entry.second.scheme() == "file" ? "file:<redacted>" : entry.second.uri;
  }
  normalized["secrets"] = std::move(normalized_secrets);
  return normalized;
}

Result<Digest> CogitoConfig::ComputeDigest() const {
  const Error validation = Validate();
  if (validation) {
    return validation;
  }
  try {
    return ComputeConfigDigest(schema_version, ToNormalizedJson());
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while normalizing the configuration");
  } catch (...) {
    return InternalError("unexpected configuration normalization failure");
  }
}

Result<CogitoConfig> ConfigLoader::FromJson(const ccj::Json& json) {
  try {
    if (!json.is_object()) {
      return SchemaError("the configuration root must be an object");
    }
    for (auto it = json.begin(); it != json.end(); ++it) {
      if (!IsAllowedTopLevelKey(it.key())) {
        return SchemaError("the configuration contains an unknown top-level property");
      }
    }

    const auto schema_it = json.find("schema_version");
    if (schema_it == json.end()) {
      return SchemaError("schema_version is required");
    }
    std::uint64_t schema_version = 0U;
    if (!ReadUnsignedInteger(*schema_it, schema_version) || schema_version != 1U) {
      return SchemaError("schema_version must be the integer 1");
    }

    CogitoConfig config;
    config.schema_version = schema_version;

    const auto engine_it = json.find("engine");
    if (engine_it != json.end()) {
      if (!engine_it->is_object()) {
        return SchemaError("engine must be an object");
      }
      for (auto it = engine_it->begin(); it != engine_it->end(); ++it) {
        if (!IsAllowedEngineKey(it.key())) {
          return SchemaError("engine contains an unknown property");
        }
      }

      const auto sessions_it = engine_it->find("max_concurrent_sessions");
      if (sessions_it != engine_it->end()) {
        std::uint64_t sessions = 0U;
        if (!ReadUnsignedInteger(*sessions_it, sessions) || sessions < 1U ||
            sessions > 65536U) {
          return SchemaError("max_concurrent_sessions violates the schema");
        }
        config.engine.max_concurrent_sessions = static_cast<std::uint32_t>(sessions);
      }

      const auto timeout_it = engine_it->find("default_turn_timeout_ms");
      if (timeout_it != engine_it->end()) {
        std::uint64_t timeout = 0U;
        if (!ReadUnsignedInteger(*timeout_it, timeout) || timeout < 100U ||
            timeout > 3600000U) {
          return SchemaError("default_turn_timeout_ms violates the schema");
        }
        config.engine.default_turn_timeout_ms = timeout;
      }

      const auto level_it = engine_it->find("log_level");
      if (level_it != engine_it->end()) {
        if (!level_it->is_string()) {
          return SchemaError("log_level must be a string");
        }
        config.engine.log_level = level_it->get<std::string>();
        if (!IsValidLogLevel(config.engine.log_level)) {
          return SchemaError("log_level violates the schema");
        }
      }
    }

    const auto secrets_it = json.find("secrets");
    if (secrets_it != json.end()) {
      if (!secrets_it->is_object()) {
        return SchemaError("secrets must be an object");
      }
      for (auto it = secrets_it->begin(); it != secrets_it->end(); ++it) {
        if (!IsValidSecretName(it.key()) || !it->is_string()) {
          return SchemaError("a secrets entry violates the schema");
        }
        SecretRef ref{it->get<std::string>()};
        if (!IsValidSecretUri(ref.uri)) {
          return SchemaError("a secret reference violates the schema");
        }
        config.secrets.emplace(it.key(), std::move(ref));
      }
    }

    const Error validation = config.Validate();
    if (validation) {
      return validation;
    }
    return config;
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while loading the configuration");
  } catch (...) {
    return ConfigError("unexpected configuration conversion failure");
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
  try {
    std::ifstream input(file_path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
      return ConfigError("the configuration file could not be opened");
    }

    const std::streamoff end = input.tellg();
    if (end < 0) {
      return ConfigError("the configuration file size could not be determined");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > kMaxConfigBytes) {
      return TooLargeError("the configuration file exceeds 256 KiB");
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
      return TooLargeError("the configuration file exceeds the addressable size");
    }

    input.seekg(0, std::ios::beg);
    if (!input) {
      return ConfigError("the configuration file could not be rewound");
    }

    std::string contents;
    contents.reserve(static_cast<std::size_t>(size));
    std::array<char, 4096U> buffer{};
    for (;;) {
      const std::size_t remaining = kMaxConfigBytes + 1U - contents.size();
      const std::size_t requested = std::min(remaining, buffer.size());
      input.read(buffer.data(), static_cast<std::streamsize>(requested));
      const std::streamsize count = input.gcount();
      if (count > 0) {
        contents.append(buffer.data(), static_cast<std::size_t>(count));
        if (contents.size() > kMaxConfigBytes) {
          return TooLargeError("the configuration file exceeds 256 KiB");
        }
      }
      if (input.bad()) {
        return ConfigError("the configuration file read failed");
      }
      if (input.eof()) {
        break;
      }
      if (input.fail()) {
        return ConfigError("the configuration file read failed");
      }
    }
    return FromJsonString(contents);
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while reading the configuration file");
  } catch (...) {
    return ConfigError("unexpected configuration file read failure");
  }
}

}  // namespace cogito
