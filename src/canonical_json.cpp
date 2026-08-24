// SPDX-License-Identifier: Apache-2.0

#include "cogito/canonical_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale.h>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cogito::ccj {
namespace {

constexpr std::size_t kMaxSerializedBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaxSerializedDepth = 64U;
constexpr double kMaxExactInteger = 9007199254740992.0;  // 2^53

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

Error InvalidJson(const char* message) {
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

Error PreflightDepth(std::string_view text, std::size_t max_depth) {
  std::size_t nesting = 0;
  bool in_string = false;
  bool escaped = false;

  for (const char ch : text) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
    } else if (ch == '{' || ch == '[') {
      ++nesting;
      if (nesting > max_depth) {
        return MakeError(Errc::DepthExceeded, reason::kInputDepthExceeded,
                         "JSON nesting exceeds its depth limit");
      }
    } else if ((ch == '}' || ch == ']') && nesting > 0U) {
      --nesting;
    }
  }
  return Error::Ok();
}

bool DecodeUtf16SortKey(std::string_view text, std::vector<std::uint16_t>& result) {
  if (!IsValidUtf8(text)) {
    return false;
  }

  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char lead = bytes[i];
    std::uint32_t code_point = 0;
    std::size_t width = 1;
    if (lead <= 0x7FU) {
      code_point = lead;
    } else if (lead <= 0xDFU) {
      width = 2;
      code_point = ((lead & 0x1FU) << 6U) | (bytes[i + 1U] & 0x3FU);
    } else if (lead <= 0xEFU) {
      width = 3;
      code_point = ((lead & 0x0FU) << 12U) |
                   ((bytes[i + 1U] & 0x3FU) << 6U) |
                   (bytes[i + 2U] & 0x3FU);
    } else {
      width = 4;
      code_point = ((lead & 0x07U) << 18U) |
                   ((bytes[i + 1U] & 0x3FU) << 12U) |
                   ((bytes[i + 2U] & 0x3FU) << 6U) |
                   (bytes[i + 3U] & 0x3FU);
    }

    if (code_point <= 0xFFFFU) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      const std::uint32_t adjusted = code_point - 0x10000U;
      result.push_back(static_cast<std::uint16_t>(0xD800U + (adjusted >> 10U)));
      result.push_back(static_cast<std::uint16_t>(0xDC00U + (adjusted & 0x3FFU)));
    }
    i += width;
  }
  return true;
}

bool SameDoubleBits(double lhs, double rhs) noexcept {
  std::uint64_t lhs_bits = 0;
  std::uint64_t rhs_bits = 0;
  static_assert(sizeof(lhs_bits) == sizeof(lhs), "unexpected double size");
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs));
  return lhs_bits == rhs_bits;
}

const char* PrecisionFormat(int precision) noexcept {
  switch (precision) {
    case 15:
      return "%.15g";
    case 16:
      return "%.16g";
    default:
      return "%.17g";
  }
}

bool FormatAndParseC(double value, int precision, std::string& formatted,
                     double& reparsed) {
  std::array<char, 128> buffer{};
  char* parse_end = nullptr;

#if defined(_WIN32)
  _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
  if (c_locale == nullptr) {
    return false;
  }
  const int count = _snprintf_s_l(buffer.data(), buffer.size(), _TRUNCATE,
                                  PrecisionFormat(precision), c_locale, value);
  if (count < 0) {
    _free_locale(c_locale);
    return false;
  }
  reparsed = _strtod_l(buffer.data(), &parse_end, c_locale);
  _free_locale(c_locale);
#else
  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
  if (c_locale == static_cast<locale_t>(0)) {
    return false;
  }
  const locale_t previous_locale = uselocale(c_locale);
  if (previous_locale == static_cast<locale_t>(0)) {
    freelocale(c_locale);
    return false;
  }
  const int count = std::snprintf(buffer.data(), buffer.size(),
                                  PrecisionFormat(precision), value);
  const bool restored = uselocale(previous_locale) != static_cast<locale_t>(0);
  if (count < 0 || static_cast<std::size_t>(count) >= buffer.size() || !restored) {
    freelocale(c_locale);
    return false;
  }
  reparsed = strtod_l(buffer.data(), &parse_end, c_locale);
  freelocale(c_locale);
#endif

  if (parse_end == nullptr || *parse_end != '\0') {
    return false;
  }
  formatted.assign(buffer.data());
  return true;
}

Result<std::string> FormatDouble(double value) {
  if (!std::isfinite(value)) {
    return InvalidJson("NaN and infinity are not valid CCJ numbers");
  }
  if (value == 0.0) {
    return std::string{"0"};
  }
  if (std::trunc(value) == value && std::fabs(value) <= kMaxExactInteger) {
    if (value < 0.0) {
      return std::to_string(static_cast<std::int64_t>(value));
    }
    return std::to_string(static_cast<std::uint64_t>(value));
  }

  for (int precision = 15; precision <= 17; ++precision) {
    std::string formatted;
    double reparsed = 0.0;
    if (!FormatAndParseC(value, precision, formatted, reparsed)) {
      return MakeError(Errc::Internal, nullptr,
                       "failed to format a number in the C locale");
    }
    if (SameDoubleBits(value, reparsed)) {
      return formatted;
    }
  }
  return MakeError(Errc::Internal, nullptr,
                   "no C99 %g precision round-tripped the number");
}

class Serializer {
 public:
  Result<std::string> Run(const Json& value) {
    if (!Visit(value, 0U)) {
      return error_;
    }
    return std::move(output_);
  }

 private:
  struct Member {
    const std::string* key = nullptr;
    const Json* value = nullptr;
    std::vector<std::uint16_t> sort_key;
  };

  bool Fail(Error error) {
    if (error_.ok()) {
      error_ = std::move(error);
    }
    return false;
  }

  bool Append(std::string_view text) {
    if (text.size() > kMaxSerializedBytes - output_.size()) {
      return Fail(MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "CCJ output exceeds 16 MiB"));
    }
    output_.append(text.data(), text.size());
    return true;
  }

  bool AppendChar(char value) {
    if (output_.size() == kMaxSerializedBytes) {
      return Fail(MakeError(Errc::TooLarge, reason::kInputTooLarge,
                            "CCJ output exceeds 16 MiB"));
    }
    output_.push_back(value);
    return true;
  }

  bool AppendString(std::string_view value) {
    if (!IsValidUtf8(value)) {
      return Fail(MakeError(Errc::NotUtf8, reason::kInputNotUtf8,
                            "JSON string is not valid UTF-8"));
    }
    if (!AppendChar('"')) {
      return false;
    }

    constexpr char kHex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
      switch (byte) {
        case '"':
          if (!Append("\\\"")) return false;
          break;
        case '\\':
          if (!Append("\\\\")) return false;
          break;
        case '\b':
          if (!Append("\\b")) return false;
          break;
        case '\f':
          if (!Append("\\f")) return false;
          break;
        case '\n':
          if (!Append("\\n")) return false;
          break;
        case '\r':
          if (!Append("\\r")) return false;
          break;
        case '\t':
          if (!Append("\\t")) return false;
          break;
        default:
          if (byte < 0x20U) {
            const std::array<char, 6> escaped{
                '\\', 'u', '0', '0', kHex[(byte >> 4U) & 0x0FU], kHex[byte & 0x0FU]};
            if (!Append(std::string_view(escaped.data(), escaped.size()))) return false;
          } else if (!AppendChar(static_cast<char>(byte))) {
            return false;
          }
          break;
      }
    }
    return AppendChar('"');
  }

  bool Visit(const Json& value, std::size_t depth) {
    if (value.is_null()) {
      return Append("null");
    }
    if (value.is_boolean()) {
      return Append(value.get<bool>() ? "true" : "false");
    }
    if (value.type() == Json::value_t::number_integer) {
      return Append(std::to_string(value.get<std::int64_t>()));
    }
    if (value.type() == Json::value_t::number_unsigned) {
      return Append(std::to_string(value.get<std::uint64_t>()));
    }
    if (value.is_number_float()) {
      auto formatted = FormatDouble(value.get<double>());
      if (!formatted) {
        return Fail(formatted.error());
      }
      return Append(formatted.value());
    }
    if (value.is_string()) {
      return AppendString(value.get_ref<const std::string&>());
    }
    if (value.is_array()) {
      if (depth >= kMaxSerializedDepth) {
        return Fail(MakeError(Errc::DepthExceeded, reason::kInputDepthExceeded,
                              "JSON nesting exceeds 64 levels"));
      }
      if (!AppendChar('[')) return false;
      bool first = true;
      for (const auto& item : value) {
        if (!first && !AppendChar(',')) return false;
        first = false;
        if (!Visit(item, depth + 1U)) return false;
      }
      return AppendChar(']');
    }
    if (value.is_object()) {
      if (depth >= kMaxSerializedDepth) {
        return Fail(MakeError(Errc::DepthExceeded, reason::kInputDepthExceeded,
                              "JSON nesting exceeds 64 levels"));
      }

      std::vector<Member> members;
      members.reserve(value.size());
      for (auto it = value.cbegin(); it != value.cend(); ++it) {
        Member member;
        member.key = &it.key();
        member.value = &it.value();
        if (!DecodeUtf16SortKey(*member.key, member.sort_key)) {
          return Fail(MakeError(Errc::NotUtf8, reason::kInputNotUtf8,
                                "JSON object key is not valid UTF-8"));
        }
        members.push_back(std::move(member));
      }
      std::sort(members.begin(), members.end(), [](const Member& lhs, const Member& rhs) {
        return std::lexicographical_compare(lhs.sort_key.begin(), lhs.sort_key.end(),
                                            rhs.sort_key.begin(), rhs.sort_key.end());
      });

      if (!AppendChar('{')) return false;
      bool first = true;
      for (const Member& member : members) {
        if (!first && !AppendChar(',')) return false;
        first = false;
        if (!AppendString(*member.key) || !AppendChar(':') ||
            !Visit(*member.value, depth + 1U)) {
          return false;
        }
      }
      return AppendChar('}');
    }
    return Fail(InvalidJson("unsupported nlohmann JSON value type"));
  }

  std::string output_;
  Error error_{};
};

}  // namespace

Result<Json> ParseStrict(std::string_view text, const ParseLimits& lim) {
  if (text.size() > lim.max_bytes) {
    return MakeError(Errc::TooLarge, reason::kInputTooLarge,
                     "JSON input exceeds its byte limit");
  }
  if (lim.max_depth < 0) {
    return InvalidJson("max_depth cannot be negative");
  }
  if (!IsValidUtf8(text)) {
    return MakeError(Errc::NotUtf8, reason::kInputNotUtf8,
                     "JSON input is not valid UTF-8");
  }
  if (Error depth_error =
          PreflightDepth(text, static_cast<std::size_t>(lim.max_depth));
      depth_error) {
    return depth_error;
  }

  try {
    using ParseEvent = Json::parse_event_t;
    std::vector<std::unordered_set<std::string>> object_keys;
    std::size_t nesting = 0;
    Error violation = Error::Ok();

    const auto callback = [&](int /*parser_depth*/, ParseEvent event, Json& parsed) {
      if (event == ParseEvent::object_start || event == ParseEvent::array_start) {
        ++nesting;
        if (violation.ok() && nesting > static_cast<std::size_t>(lim.max_depth)) {
          violation = MakeError(Errc::DepthExceeded, reason::kInputDepthExceeded,
                                "JSON nesting exceeds its depth limit");
        }
        if (event == ParseEvent::object_start) {
          object_keys.emplace_back();
        }
      } else if (event == ParseEvent::key) {
        if (object_keys.empty()) {
          if (violation.ok()) {
            violation = MakeError(Errc::Internal, nullptr,
                                  "parser emitted a key outside an object");
          }
        } else {
          const std::string& key = parsed.get_ref<const std::string&>();
          const auto inserted = object_keys.back().insert(key);
          if (!inserted.second && violation.ok()) {
            violation = MakeError(Errc::DuplicateKey, reason::kInputDuplicateKey,
                                  "JSON object contains a duplicate key");
          } else if (inserted.second && object_keys.back().size() > lim.max_keys &&
                     violation.ok()) {
            violation = MakeError(Errc::TooLarge, reason::kInputTooLarge,
                                  "JSON object exceeds its key limit");
          }
        }
      } else if (event == ParseEvent::object_end) {
        if (!object_keys.empty()) {
          object_keys.pop_back();
        }
        if (nesting > 0U) {
          --nesting;
        }
      } else if (event == ParseEvent::array_end && nesting > 0U) {
        --nesting;
      }
      return true;
    };

    Json parsed = Json::parse(text.begin(), text.end(), callback, false, false);
    if (violation) {
      return violation;
    }
    if (parsed.is_discarded()) {
      return InvalidJson("input is not valid JSON");
    }
    return parsed;
  } catch (const std::bad_alloc&) {
    return MakeError(Errc::Internal, nullptr, "out of memory while parsing JSON");
  } catch (const Json::exception&) {
    return InvalidJson("input is not valid JSON");
  } catch (...) {
    return MakeError(Errc::Internal, nullptr, "unexpected JSON parser failure");
  }
}

Result<std::string> Serialize(const Json& j) {
  try {
    Serializer serializer;
    return serializer.Run(j);
  } catch (const std::bad_alloc&) {
    return MakeError(Errc::Internal, nullptr, "out of memory while serializing JSON");
  } catch (const Json::exception&) {
    return InvalidJson("JSON value cannot be serialized as CCJ");
  } catch (...) {
    return MakeError(Errc::Internal, nullptr, "unexpected JSON serializer failure");
  }
}

Error SelfTest() {
  struct GoldenVector {
    const char* input;
    const char* expected;
  };
  static constexpr std::array<GoldenVector, 24> kGoldenVectors{{
      {R"json({"b":1,"a":2})json", R"json({"a":2,"b":1})json"},
      {R"json({"a":{"z":1,"y":2}})json", R"json({"a":{"y":2,"z":1}})json"},
      {R"json({"A":1,"a":2})json", R"json({"A":1,"a":2})json"},
      {"0.0", "0"},
      {"-0.0", "0"},
      {"1.0", "1"},
      {"-7.0", "-7"},
      {"9007199254740992.0", "9007199254740992"},
      {"9007199254740994.0", "9007199254740994"},
      {"0.1", "0.1"},
      {"0.7", "0.7"},
      {"0.70000000000000007", "0.7000000000000001"},
      {"1e-7", "1e-07"},
      {"1.5e300", "1.5e+300"},
      {R"json("a\"b")json", R"json("a\"b")json"},
      {R"json("a\\b")json", R"json("a\\b")json"},
      {R"json("\u0000")json", R"json("\u0000")json"},
      {R"json("\u001F")json", R"json("\u001f")json"},
      {R"json("\n")json", R"json("\n")json"},
      {u8R"json("한글")json", u8R"json("한글")json"},
      {u8R"json("😀")json", u8R"json("😀")json"},
      {"[]", "[]"},
      {"{}", "{}"},
      {R"json([1,{"b":[2,3],"a":null}])json",
       R"json([1,{"a":null,"b":[2,3]}])json"},
  }};

  for (std::size_t i = 0; i < kGoldenVectors.size(); ++i) {
    auto parsed = ParseStrict(kGoldenVectors[i].input);
    if (!parsed) {
      Error error = parsed.error();
      error.message = "CCJ self-test parse failed at vector " + std::to_string(i);
      return error;
    }
    auto serialized = Serialize(parsed.value());
    if (!serialized) {
      Error error = serialized.error();
      error.message = "CCJ self-test serialization failed at vector " + std::to_string(i);
      return error;
    }
    if (serialized.value() != kGoldenVectors[i].expected) {
      return MakeError(Errc::Internal, nullptr,
                       ("CCJ self-test byte mismatch at vector " + std::to_string(i)).c_str());
    }
  }
  return Error::Ok();
}

}  // namespace cogito::ccj
