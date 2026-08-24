// SPDX-License-Identifier: Apache-2.0

#include "cogito/ids.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <new>
#include <string>

#include <openssl/rand.h>

namespace cogito {
namespace {

std::mutex g_process_epoch_mutex;
std::string g_process_epoch_id;

Error MakeError(Errc code, const char* message) {
  Error error;
  error.code = code;
  if (message != nullptr) {
    error.message = message;
  }
  return error;
}

int HexNibble(char value) noexcept {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

}  // namespace

bool Digest::is_zero() const noexcept {
  return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) {
    return byte == 0U;
  });
}

std::string Digest::hex() const {
  constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.resize(kSize * 2U);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    result[i * 2U] = kHex[(bytes[i] >> 4U) & 0x0FU];
    result[i * 2U + 1U] = kHex[bytes[i] & 0x0FU];
  }
  return result;
}

Result<Digest> Digest::FromHex(const std::string& lowercase_hex) {
  if (lowercase_hex.size() != kSize * 2U) {
    return MakeError(Errc::InvalidArgument,
                     "a SHA-256 digest must contain exactly 64 lowercase hex characters");
  }

  Digest result;
  for (std::size_t i = 0; i < result.bytes.size(); ++i) {
    const int high = HexNibble(lowercase_hex[i * 2U]);
    const int low = HexNibble(lowercase_hex[i * 2U + 1U]);
    if (high < 0 || low < 0) {
      return MakeError(Errc::InvalidArgument,
                       "a SHA-256 digest contains a non-lowercase-hex character");
    }
    result.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return result;
}

Result<std::string> NewUuidV4() {
  try {
    std::array<unsigned char, 16> random_bytes{};
    if (RAND_bytes(random_bytes.data(), static_cast<int>(random_bytes.size())) != 1) {
      return MakeError(Errc::Internal, "OpenSSL RAND_bytes failed");
    }

    random_bytes[6] = static_cast<unsigned char>((random_bytes[6] & 0x0FU) | 0x40U);
    random_bytes[8] = static_cast<unsigned char>((random_bytes[8] & 0x3FU) | 0x80U);

    constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(36U);
    for (std::size_t i = 0; i < random_bytes.size(); ++i) {
      if (i == 4U || i == 6U || i == 8U || i == 10U) {
        result.push_back('-');
      }
      result.push_back(kHex[(random_bytes[i] >> 4U) & 0x0FU]);
      result.push_back(kHex[random_bytes[i] & 0x0FU]);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return MakeError(Errc::Internal, "out of memory while creating a UUID");
  } catch (...) {
    return MakeError(Errc::Internal, "unexpected UUID generation failure");
  }
}

const std::string& ProcessEpochId() {
  return g_process_epoch_id;
}

Error InitProcessEpoch() {
  std::lock_guard<std::mutex> lock(g_process_epoch_mutex);
  if (!g_process_epoch_id.empty()) {
    return MakeError(Errc::Internal, "the process epoch is already initialized");
  }

  auto generated = NewUuidV4();
  if (!generated) {
    return generated.error();
  }
  g_process_epoch_id = std::move(generated).take();
  return Error::Ok();
}

}  // namespace cogito
