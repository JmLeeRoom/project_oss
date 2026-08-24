// SPDX-License-Identifier: Apache-2.0

#include "cogito/digest.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <openssl/evp.h>

namespace cogito {
namespace {

Error MakeError(Errc code, const char* reason_code, const char* message) {
  Error error;
  error.code = code;
  if (reason_code != nullptr) {
    error.reason_code = reason_code;
  }
  if (message != nullptr) {
    error.message = message;
  }
  return error;
}

Error InternalError(const char* message) {
  return MakeError(Errc::Internal, nullptr, message);
}

Error InvalidProjection(const char* message) {
  return MakeError(Errc::InvalidArgument, reason::kSchemaViolation, message);
}

bool IsValidUtf8(std::string_view text) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = bytes[i];
    if (lead <= 0x7FU) {
      ++i;
      continue;
    }

    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      continuation_count = 1;
      code_point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      continuation_count = 2;
      code_point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      continuation_count = 3;
      code_point = lead & 0x07U;
    } else {
      return false;
    }

    if (continuation_count > text.size() - i - 1U) {
      return false;
    }
    for (std::size_t j = 1; j <= continuation_count; ++j) {
      const unsigned char continuation = bytes[i + j];
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    if ((continuation_count == 1U && code_point < 0x80U) ||
        (continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU) ||
        code_point > 0x10FFFFU) {
      return false;
    }
    i += continuation_count + 1U;
  }
  return true;
}

bool Utf8ByteLess(std::string_view lhs, std::string_view rhs) noexcept {
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char lhs_byte, char rhs_byte) {
        return static_cast<unsigned char>(lhs_byte) < static_cast<unsigned char>(rhs_byte);
      });
}

bool IsOneOf(std::string_view value,
             std::initializer_list<std::string_view> allowed) noexcept {
  return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

template <typename AppendFunction>
Result<Digest> BuildProjection(std::string_view domain_tag,
                               AppendFunction&& append_fields) {
  try {
    auto created = LpBuffer::Create(domain_tag);
    if (!created) {
      return created.error();
    }
    LpBuffer buffer = std::move(created).take();
    const Error append_error = append_fields(buffer);
    if (append_error) {
      return append_error;
    }
    return buffer.ComputeDigest();
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while computing a digest projection");
  } catch (...) {
    return InternalError("unexpected digest projection failure");
  }
}

Error DuplicateProjectionKey(const char* message) {
  return MakeError(Errc::DuplicateKey, reason::kSchemaViolation, message);
}

Error ValidateProjectionString(std::string_view value) {
  if (!IsValidUtf8(value)) {
    return MakeError(Errc::NotUtf8, reason::kInputNotUtf8,
                     "digest projection contains a non-UTF-8 string");
  }
  return Error::Ok();
}

}  // namespace

Result<Digest> Sha256(const void* data, std::size_t len) {
  if (data == nullptr && len != 0U) {
    return InvalidProjection("SHA-256 received a null pointer with a nonzero length");
  }

  using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  Context context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context) {
    return InternalError("EVP_MD_CTX_new failed");
  }
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    return InternalError("EVP_DigestInit_ex failed");
  }
  if (len != 0U && EVP_DigestUpdate(context.get(), data, len) != 1) {
    return InternalError("EVP_DigestUpdate failed");
  }

  Digest digest;
  unsigned int digest_length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest.bytes.data(), &digest_length) != 1) {
    return InternalError("EVP_DigestFinal_ex failed");
  }
  if (digest_length != Digest::kSize) {
    return InternalError("OpenSSL returned an unexpected SHA-256 length");
  }
  return digest;
}

Result<LpBuffer> LpBuffer::Create(std::string_view domain_tag) {
  if (domain_tag.empty()) {
    return InvalidProjection("a digest domain tag cannot be empty");
  }

  LpBuffer result;
  const Error error = result.AppendString(domain_tag);
  if (error) {
    return error;
  }
  return result;
}

Error LpBuffer::AppendString(std::string_view str) {
  if (!last_error_.ok()) {
    return last_error_;
  }
  if (!IsValidUtf8(str)) {
    last_error_ = MakeError(Errc::NotUtf8, reason::kInputNotUtf8,
                            "LP string is not valid UTF-8");
    return last_error_;
  }
  return AppendBytes(str.data(), str.size());
}

Error LpBuffer::AppendBytes(const void* data, std::size_t size) {
  if (!last_error_.ok()) {
    return last_error_;
  }
  if (data == nullptr && size != 0U) {
    last_error_ = InvalidProjection("LP bytes received a null pointer with a nonzero length");
    return last_error_;
  }
  if (size > std::numeric_limits<std::uint32_t>::max()) {
    last_error_ = MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "an LP field exceeds the u32 length limit");
    return last_error_;
  }

  constexpr std::size_t kPrefixSize = sizeof(std::uint32_t);
  if (size > buffer_.max_size() - kPrefixSize ||
      buffer_.size() > buffer_.max_size() - kPrefixSize - size) {
    last_error_ = MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "an LP append exceeds the buffer size limit");
    return last_error_;
  }

  try {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> aliased_bytes;
    if (size != 0U && !buffer_.empty()) {
      const std::uintptr_t input_address = reinterpret_cast<std::uintptr_t>(bytes);
      const std::uintptr_t buffer_begin =
          reinterpret_cast<std::uintptr_t>(buffer_.data());
      const std::uintptr_t buffer_end = buffer_begin + buffer_.size();
      if (input_address >= buffer_begin && input_address < buffer_end) {
        if (size > buffer_end - input_address) {
          last_error_ = InvalidProjection(
              "an aliased LP byte range extends beyond the current buffer");
          return last_error_;
        }
        aliased_bytes.assign(bytes, bytes + size);
        bytes = aliased_bytes.data();
      }
    }

    buffer_.reserve(buffer_.size() + kPrefixSize + size);
    const std::uint32_t length = static_cast<std::uint32_t>(size);
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
      buffer_.push_back(static_cast<std::uint8_t>((length >> shift) & 0xFFU));
    }
    if (size != 0U) {
      buffer_.insert(buffer_.end(), bytes, bytes + size);
    }
    return Error::Ok();
  } catch (const std::bad_alloc&) {
    last_error_ = InternalError("out of memory while appending an LP field");
    return last_error_;
  } catch (...) {
    last_error_ = InternalError("unexpected LP byte append failure");
    return last_error_;
  }
}

Error LpBuffer::AppendU64(std::uint64_t value) {
  if (!last_error_.ok()) {
    return last_error_;
  }
  constexpr std::size_t kEncodedSize = sizeof(std::uint64_t);
  if (buffer_.size() > buffer_.max_size() - kEncodedSize) {
    last_error_ = MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "an LP integer exceeds the buffer size limit");
    return last_error_;
  }

  try {
    buffer_.reserve(buffer_.size() + kEncodedSize);
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
      buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    return Error::Ok();
  } catch (const std::bad_alloc&) {
    last_error_ = InternalError("out of memory while appending an LP integer");
    return last_error_;
  } catch (...) {
    last_error_ = InternalError("unexpected LP integer append failure");
    return last_error_;
  }
}

Error LpBuffer::AppendDigest(const Digest& digest) {
  if (!last_error_.ok()) {
    return last_error_;
  }
  if (buffer_.size() > buffer_.max_size() - digest.bytes.size()) {
    last_error_ = MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "an LP digest exceeds the buffer size limit");
    return last_error_;
  }

  try {
    buffer_.reserve(buffer_.size() + digest.bytes.size());
    buffer_.insert(buffer_.end(), digest.bytes.begin(), digest.bytes.end());
    return Error::Ok();
  } catch (const std::bad_alloc&) {
    last_error_ = InternalError("out of memory while appending an LP digest");
    return last_error_;
  } catch (...) {
    last_error_ = InternalError("unexpected LP digest append failure");
    return last_error_;
  }
}

Error LpBuffer::AppendJson(const ccj::Json& json) {
  if (!last_error_.ok()) {
    return last_error_;
  }

  auto serialized = ccj::Serialize(json);
  if (!serialized) {
    last_error_ = serialized.error();
    return last_error_;
  }
  return AppendBytes(serialized.value().data(), serialized.value().size());
}

Result<Digest> LpBuffer::ComputeDigest() const {
  if (!last_error_.ok()) {
    return last_error_;
  }
  return Sha256(buffer_);
}

Result<Digest> ComputeActionDigest(const SessionId& session_id,
                                   TurnId turn_id,
                                   const ActionId& action_id,
                                   const std::string& tool_name,
                                   const ccj::Json& arguments) {
  return BuildProjection(domain::kAction, [&](LpBuffer& buffer) {
    Error error = buffer.AppendString(session_id);
    if (error) return error;
    error = buffer.AppendU64(turn_id);
    if (error) return error;
    error = buffer.AppendString(action_id);
    if (error) return error;
    error = buffer.AppendString(tool_name);
    if (error) return error;
    return buffer.AppendJson(arguments);
  });
}

Result<Digest> ComputeOperationDigest(const std::string& tool_name,
                                      const ccj::Json& arguments) {
  return BuildProjection(domain::kOperation, [&](LpBuffer& buffer) {
    Error error = buffer.AppendString(tool_name);
    if (error) return error;
    return buffer.AppendJson(arguments);
  });
}

Result<Digest> ComputePermitDigest(const Digest& action_digest,
                                   const std::string& subject_id,
                                   std::uint64_t mode,
                                   const Digest& policy_digest,
                                   const Digest& registry_digest) {
  return BuildProjection(domain::kPermit, [&](LpBuffer& buffer) {
    Error error = buffer.AppendDigest(action_digest);
    if (error) return error;
    error = buffer.AppendString(subject_id);
    if (error) return error;
    error = buffer.AppendU64(mode);
    if (error) return error;
    error = buffer.AppendDigest(policy_digest);
    if (error) return error;
    return buffer.AppendDigest(registry_digest);
  });
}

Result<Digest> ComputeAuditDigest(const Digest& prev_hash,
                                  const std::string& event_id,
                                  const SessionId& session_id,
                                  TurnId turn_id,
                                  const ActionId& action_id,
                                  const std::string& wall_time_utc,
                                  std::uint64_t monotonic_ns,
                                  const std::string& process_epoch_id,
                                  const std::string& kind,
                                  std::uint64_t actor_type,
                                  const std::string& actor_id,
                                  const ccj::Json& payload,
                                  std::uint64_t schema_version) {
  return BuildProjection(domain::kAudit, [&](LpBuffer& buffer) {
    Error error = buffer.AppendDigest(prev_hash);
    if (error) return error;
    error = buffer.AppendString(event_id);
    if (error) return error;
    error = buffer.AppendString(session_id);
    if (error) return error;
    error = buffer.AppendU64(turn_id);
    if (error) return error;
    error = buffer.AppendString(action_id);
    if (error) return error;
    error = buffer.AppendString(wall_time_utc);
    if (error) return error;
    error = buffer.AppendU64(monotonic_ns);
    if (error) return error;
    error = buffer.AppendString(process_epoch_id);
    if (error) return error;
    error = buffer.AppendString(kind);
    if (error) return error;
    error = buffer.AppendU64(actor_type);
    if (error) return error;
    error = buffer.AppendString(actor_id);
    if (error) return error;
    error = buffer.AppendJson(payload);
    if (error) return error;
    return buffer.AppendU64(schema_version);
  });
}

Result<Digest> ComputeToolSchemaDigest(const std::string& name,
                                       const ccj::Json& input_schema,
                                       const ccj::Json& output_schema) {
  return BuildProjection(domain::kToolSchema, [&](LpBuffer& buffer) {
    Error error = buffer.AppendString(name);
    if (error) return error;
    error = buffer.AppendJson(input_schema);
    if (error) return error;
    return buffer.AppendJson(output_schema);
  });
}

Result<Digest> ComputeRegistryDigest(std::vector<ToolProjectionDto> tools) {
  try {
    for (const ToolProjectionDto& tool : tools) {
      if (tool.name.empty()) {
        return InvalidProjection("a registry tool name cannot be empty");
      }
      const std::string_view strings[] = {
          tool.name,          tool.status,      tool.forbidden_reason,
          tool.grammar_coverage, tool.effect,  tool.risk,
          tool.idempotency,   tool.provider_id, tool.invoker_id};
      for (std::string_view value : strings) {
        const Error validation = ValidateProjectionString(value);
        if (validation) return validation;
      }
      if (!IsOneOf(tool.status, {"enabled", "forbidden"}) ||
          !IsOneOf(tool.grammar_coverage, {"none", "partial", "full"}) ||
          !IsOneOf(tool.effect, {"none", "write", "destructive"}) ||
          !IsOneOf(tool.risk, {"low", "medium", "high", "critical"}) ||
          !IsOneOf(tool.idempotency, {"safe", "conditional", "unsafe"}) ||
          tool.approval_required > 1U) {
        return InvalidProjection("a registry projection contains an invalid enum value");
      }
      if ((tool.status == "enabled" && !tool.forbidden_reason.empty()) ||
          (tool.status == "forbidden" && tool.forbidden_reason.empty())) {
        return InvalidProjection("registry status and forbidden_reason disagree");
      }
    }

    std::sort(tools.begin(), tools.end(), [](const ToolProjectionDto& lhs,
                                             const ToolProjectionDto& rhs) {
      return Utf8ByteLess(lhs.name, rhs.name);
    });
    for (std::size_t i = 1; i < tools.size(); ++i) {
      if (tools[i - 1U].name == tools[i].name) {
        return DuplicateProjectionKey("registry projection contains a duplicate tool name");
      }
    }

    return BuildProjection(domain::kRegistry, [&](LpBuffer& buffer) {
      for (const ToolProjectionDto& tool : tools) {
        Error error = buffer.AppendString(tool.name);
        if (error) return error;
        error = buffer.AppendString(tool.status);
        if (error) return error;
        error = buffer.AppendString(tool.forbidden_reason);
        if (error) return error;
        error = buffer.AppendDigest(tool.toolschema_digest);
        if (error) return error;
        error = buffer.AppendString(tool.grammar_coverage);
        if (error) return error;
        error = buffer.AppendString(tool.effect);
        if (error) return error;
        error = buffer.AppendString(tool.risk);
        if (error) return error;
        error = buffer.AppendString(tool.idempotency);
        if (error) return error;
        error = buffer.AppendU64(tool.approval_required);
        if (error) return error;
        error = buffer.AppendU64(tool.timeout_ms);
        if (error) return error;
        error = buffer.AppendU64(tool.max_output_bytes);
        if (error) return error;
        error = buffer.AppendString(tool.provider_id);
        if (error) return error;
        error = buffer.AppendString(tool.invoker_id);
        if (error) return error;
      }
      return Error::Ok();
    });
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while sorting the registry projection");
  } catch (...) {
    return InternalError("unexpected registry projection failure");
  }
}

Result<Digest> ComputePolicyDigest(std::uint64_t schema_version,
                                   const std::string& default_decision,
                                   std::vector<PolicyRuleProjectionDto> rules) {
  try {
    Error validation = ValidateProjectionString(default_decision);
    if (validation) return validation;
    if (default_decision.empty()) {
      return InvalidProjection("a policy default decision cannot be empty");
    }

    std::unordered_set<std::string> rule_ids;
    rule_ids.reserve(rules.size());
    for (const PolicyRuleProjectionDto& rule : rules) {
      if (rule.rule_id.empty()) {
        return InvalidProjection("a policy rule ID cannot be empty");
      }
      validation = ValidateProjectionString(rule.rule_id);
      if (validation) return validation;
      if (!rule_ids.insert(rule.rule_id).second) {
        return DuplicateProjectionKey("policy projection contains a duplicate rule ID");
      }
    }

    std::sort(rules.begin(), rules.end(), [](const PolicyRuleProjectionDto& lhs,
                                             const PolicyRuleProjectionDto& rhs) {
      if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
      }
      return Utf8ByteLess(lhs.rule_id, rhs.rule_id);
    });

    return BuildProjection(domain::kPolicy, [&](LpBuffer& buffer) {
      Error error = buffer.AppendU64(schema_version);
      if (error) return error;
      error = buffer.AppendString(default_decision);
      if (error) return error;
      for (const PolicyRuleProjectionDto& rule : rules) {
        error = buffer.AppendU64(rule.priority);
        if (error) return error;
        error = buffer.AppendString(rule.rule_id);
        if (error) return error;
        error = buffer.AppendJson(rule.normalized_rule);
        if (error) return error;
      }
      return Error::Ok();
    });
  } catch (const std::bad_alloc&) {
    return InternalError("out of memory while sorting the policy projection");
  } catch (...) {
    return InternalError("unexpected policy projection failure");
  }
}

Result<Digest> ComputeConfigDigest(std::uint64_t schema_version,
                                   const ccj::Json& normalized_config) {
  return BuildProjection(domain::kConfig, [&](LpBuffer& buffer) {
    Error error = buffer.AppendU64(schema_version);
    if (error) return error;
    return buffer.AppendJson(normalized_config);
  });
}

Result<Digest> ComputeModelDigest(const std::string& provider_id,
                                  const std::string& model_id,
                                  const std::string& weights_sha256,
                                  const std::string& chat_template_digest,
                                  const std::string& tokenizer_digest,
                                  const std::string& quantization) {
  return BuildProjection(domain::kModel, [&](LpBuffer& buffer) {
    Error error = buffer.AppendString(provider_id);
    if (error) return error;
    error = buffer.AppendString(model_id);
    if (error) return error;
    error = buffer.AppendString(weights_sha256);
    if (error) return error;
    error = buffer.AppendString(chat_template_digest);
    if (error) return error;
    error = buffer.AppendString(tokenizer_digest);
    if (error) return error;
    return buffer.AppendString(quantization);
  });
}

}  // namespace cogito
