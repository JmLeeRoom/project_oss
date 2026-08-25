// SPDX-License-Identifier: Apache-2.0

#include "cogito/secret_string.hpp"

#include <exception>
#include <utility>

#include <openssl/crypto.h>

namespace cogito {
namespace {

thread_local testing::CleanseObserver g_cleanse_observer;

void NotifyCleanse(const volatile void* ptr, std::size_t size) noexcept {
  if (!g_cleanse_observer) {
    return;
  }
  try {
    g_cleanse_observer(ptr, size);
  } catch (...) {
    // Test-only observers must never compromise SecretString cleanup.
  }
}

void CleanseStorage(std::string& value, bool notify_observer) noexcept {
  const std::size_t size = value.size();
  if (size == 0U) {
    value.clear();
    return;
  }

  char* const ptr = value.data();
  OPENSSL_cleanse(ptr, size);
  if (notify_observer) {
    NotifyCleanse(static_cast<const volatile void*>(ptr), size);
  }
  value.clear();
}

}  // namespace

SecretString::SecretString(std::string secret) {
  try {
    data_.assign(secret);
  } catch (...) {
    CleanseStorage(secret, false);
    throw;
  }
  CleanseStorage(secret, false);
}

SecretString::~SecretString() { Clear(); }

SecretString::SecretString(SecretString&& other) noexcept {
  try {
    data_.assign(other.data_);
  } catch (...) {
    other.Clear();
    std::terminate();
  }
  other.Clear();
}

SecretString& SecretString::operator=(SecretString&& other) noexcept {
  if (this != &other) {
    Clear();
    try {
      data_.assign(other.data_);
    } catch (...) {
      other.Clear();
      std::terminate();
    }
    other.Clear();
  }
  return *this;
}

void SecretString::Clear() {
  CleanseStorage(data_, true);
}

namespace testing {

void SetCleanseObserver(CleanseObserver observer) {
  g_cleanse_observer = std::move(observer);
}

void ClearCleanseObserver() { g_cleanse_observer = nullptr; }

}  // namespace testing
}  // namespace cogito
