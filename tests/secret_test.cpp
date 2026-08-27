// SPDX-License-Identifier: Apache-2.0

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cogito/config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <aclapi.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

class TempDirectory {
 public:
  TempDirectory() {
    static std::atomic<unsigned long long> sequence{0U};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
      path_ = base / ("cogito_secret_test_" + std::to_string(timestamp) + "_" +
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

class ScopedEnvironment {
 public:
  ScopedEnvironment(std::string name, std::string value) : name_(std::move(name)) {
    const char* existing = std::getenv(name_.c_str());
    if (existing != nullptr) {
      previous_ = existing;
    }
    Set(value);
  }

  ~ScopedEnvironment() {
    try {
      if (previous_) {
        Set(*previous_);
      } else {
        Unset();
      }
    } catch (...) {
    }
  }

  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

 private:
  void Set(const std::string& value) {
#if defined(_WIN32)
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("unable to set a test environment variable");
    }
#else
    if (setenv(name_.c_str(), value.c_str(), 1) != 0) {
      throw std::runtime_error("unable to set a test environment variable");
    }
#endif
  }

  void Unset() {
#if defined(_WIN32)
    if (_putenv_s(name_.c_str(), "") != 0) {
      throw std::runtime_error("unable to clear a test environment variable");
    }
#else
    if (unsetenv(name_.c_str()) != 0) {
      throw std::runtime_error("unable to clear a test environment variable");
    }
#endif
  }

  std::string name_;
  std::optional<std::string> previous_;
};

std::string NativePath(const std::filesystem::path& path) {
  return path.u8string();
}

std::string FileUri(const std::filesystem::path& path) {
  return "file:" + NativePath(path);
}

#if defined(_WIN32)

class WindowsHandle {
 public:
  explicit WindowsHandle(HANDLE handle) noexcept : handle_(handle) {}
  ~WindowsHandle() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  WindowsHandle(const WindowsHandle&) = delete;
  WindowsHandle& operator=(const WindowsHandle&) = delete;

  HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class LocalAcl {
 public:
  explicit LocalAcl(PACL acl) noexcept : acl_(acl) {}
  ~LocalAcl() {
    if (acl_ != nullptr) {
      LocalFree(acl_);
    }
  }

  LocalAcl(const LocalAcl&) = delete;
  LocalAcl& operator=(const LocalAcl&) = delete;

  PACL get() const noexcept { return acl_; }

 private:
  PACL acl_ = nullptr;
};

void SetFileDacl(const std::filesystem::path& path, bool grant_everyone_read) {
  HANDLE raw_token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
    throw std::runtime_error("unable to inspect the test process token");
  }
  WindowsHandle token(raw_token);

  DWORD token_bytes = 0U;
  GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &token_bytes);
  if (token_bytes == 0U || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    throw std::runtime_error("unable to size the test process token");
  }
  std::vector<unsigned char> token_storage(token_bytes);
  if (GetTokenInformation(token.get(), TokenUser, token_storage.data(), token_bytes,
                          &token_bytes) == FALSE) {
    throw std::runtime_error("unable to inspect the test process user");
  }
  auto* token_user = reinterpret_cast<TOKEN_USER*>(token_storage.data());

  alignas(void*) unsigned char system_storage[SECURITY_MAX_SID_SIZE]{};
  alignas(void*) unsigned char admin_storage[SECURITY_MAX_SID_SIZE]{};
  alignas(void*) unsigned char world_storage[SECURITY_MAX_SID_SIZE]{};
  DWORD system_size = SECURITY_MAX_SID_SIZE;
  DWORD admin_size = SECURITY_MAX_SID_SIZE;
  DWORD world_size = SECURITY_MAX_SID_SIZE;
  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system_storage, &system_size) == FALSE ||
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin_storage,
                         &admin_size) == FALSE ||
      CreateWellKnownSid(WinWorldSid, nullptr, world_storage, &world_size) == FALSE) {
    throw std::runtime_error("unable to construct test SIDs");
  }

  std::vector<EXPLICIT_ACCESSW> entries(grant_everyone_read ? 4U : 3U);
  const auto configure = [](EXPLICIT_ACCESSW& entry, PSID sid, DWORD rights,
                            TRUSTEE_TYPE type) {
    entry.grfAccessPermissions = rights;
    entry.grfAccessMode = SET_ACCESS;
    entry.grfInheritance = NO_INHERITANCE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = type;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
  };
  configure(entries[0], token_user->User.Sid, GENERIC_ALL, TRUSTEE_IS_USER);
  configure(entries[1], system_storage, GENERIC_READ, TRUSTEE_IS_USER);
  configure(entries[2], admin_storage, GENERIC_READ, TRUSTEE_IS_GROUP);
  if (grant_everyone_read) {
    configure(entries[3], world_storage, GENERIC_READ, TRUSTEE_IS_WELL_KNOWN_GROUP);
  }

  PACL raw_acl = nullptr;
  const DWORD acl_status = SetEntriesInAclW(static_cast<ULONG>(entries.size()),
                                             entries.data(), nullptr, &raw_acl);
  if (acl_status != ERROR_SUCCESS || raw_acl == nullptr) {
    if (raw_acl != nullptr) {
      LocalFree(raw_acl);
    }
    throw std::runtime_error("unable to construct a test DACL");
  }
  LocalAcl acl(raw_acl);

  std::wstring wide_path = path.wstring();
  const DWORD set_status = SetNamedSecurityInfoW(
      wide_path.data(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
      acl.get(), nullptr);
  if (set_status != ERROR_SUCCESS) {
    throw std::runtime_error("unable to apply a test DACL");
  }
}

void SetNullFileDacl(const std::filesystem::path& path) {
  std::wstring wide_path = path.wstring();
  const DWORD set_status = SetNamedSecurityInfoW(
      wide_path.data(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
      nullptr, nullptr);
  if (set_status != ERROR_SUCCESS) {
    throw std::runtime_error("unable to apply a NULL test DACL");
  }
}

#endif

void WriteSecretFile(const std::filesystem::path& path, std::string_view bytes,
                     unsigned int mode = 0600U) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.is_open());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  REQUIRE(output.good());
#if defined(_WIN32)
  static_cast<void>(mode);
  SetFileDacl(path, false);
#else
  REQUIRE(chmod(path.c_str(), static_cast<mode_t>(mode)) == 0);
#endif
}

void RequireSecretError(const cogito::Result<cogito::SecretString>& result,
                        cogito::Errc expected) {
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error().code == expected);
}

void RequireSecretValue(cogito::Result<cogito::SecretString> result,
                        std::string_view expected) {
  REQUIRE(result.ok());
  REQUIRE(result.value().Expose() == expected);
}

}  // namespace

TEST_CASE("SecretRef splits at the first colon without validating", "[secret]") {
  const cogito::SecretRef environment{"env:NAME:with:colons"};
  REQUIRE(environment.scheme() == "env");
  REQUIRE(environment.location() == "NAME:with:colons");

  const cogito::SecretRef missing_separator{"env"};
  REQUIRE(missing_separator.scheme().empty());
  REQUIRE(missing_separator.location().empty());

  const cogito::SecretRef empty_scheme{":location"};
  REQUIRE(empty_scheme.scheme().empty());
  REQUIRE(empty_scheme.location() == "location");
}

TEST_CASE("ResolveSecret validates generic URIs before consulting the test seam",
          "[secret][seam]") {
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  const std::vector<cogito::SecretRef> invalid{
      {""}, {"env:"}, {"ENV:NAME"}, {"unknown:value"}, {std::string(1025U, 'x')}};
  for (const cogito::SecretRef& ref : invalid) {
    cogito::testing::SecretTestSeam::SetMockSecret(ref.uri, "must-not-resolve");
    RequireSecretError(cogito::ResolveSecret(ref), cogito::Errc::SecretError);
  }

  const cogito::SecretRef embedded_nul{std::string("env:NAME\0hidden", 15U)};
  cogito::testing::SecretTestSeam::SetMockSecret(embedded_nul.uri, "must-not-resolve");
  RequireSecretError(cogito::ResolveSecret(embedded_nul), cogito::Errc::SecretError);

  cogito::testing::SecretTestSeam::SetMockSecret("env:1backend-invalid", "mock-value");
  RequireSecretValue(cogito::ResolveSecret({"env:1backend-invalid"}), "mock-value");
  cogito::testing::SecretTestSeam::ClearMockSecrets();
}

TEST_CASE("SecretTestSeam is thread-local and cleanses replaced and cleared values",
          "[secret][seam][zeroize]") {
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  cogito::testing::SecretTestSeam::SetMockSecret("keyring:service/user", "old-secret");

  bool replacement_observed = false;
  bool replacement_zero = true;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* pointer, std::size_t size) {
        if (size == std::string_view("old-secret").size()) {
          replacement_observed = true;
          const auto* bytes = static_cast<const volatile unsigned char*>(pointer);
          for (std::size_t index = 0U; index < size; ++index) {
            replacement_zero = replacement_zero && bytes[index] == 0U;
          }
        }
      });
  cogito::testing::SecretTestSeam::SetMockSecret("keyring:service/user",
                                                 "replacement-secret");
  cogito::testing::ClearCleanseObserver();
  REQUIRE(replacement_observed);
  REQUIRE(replacement_zero);

  std::atomic<bool> other_thread_resolved{true};
  std::thread other_thread([&]() {
    other_thread_resolved.store(
        cogito::ResolveSecret({"keyring:service/user"}).ok(),
        std::memory_order_relaxed);
  });
  other_thread.join();
  REQUIRE_FALSE(other_thread_resolved.load(std::memory_order_relaxed));
  RequireSecretValue(cogito::ResolveSecret({"keyring:service/user"}),
                     "replacement-secret");

  bool clear_observed = false;
  bool clear_zero = true;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* pointer, std::size_t size) {
        if (size == std::string_view("replacement-secret").size()) {
          clear_observed = true;
          const auto* bytes = static_cast<const volatile unsigned char*>(pointer);
          for (std::size_t index = 0U; index < size; ++index) {
            clear_zero = clear_zero && bytes[index] == 0U;
          }
        }
      });
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  cogito::testing::ClearCleanseObserver();
  REQUIRE(clear_observed);
  REQUIRE(clear_zero);
}

TEST_CASE("ResolveSecret enforces environment name and value bounds", "[secret][env]") {
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  {
    ScopedEnvironment environment("COGITO_SECRET_TEST_VALUE", "environment-secret");
    RequireSecretValue(cogito::ResolveSecret({"env:COGITO_SECRET_TEST_VALUE"}),
                       "environment-secret");
  }
  {
    ScopedEnvironment empty("COGITO_SECRET_TEST_EMPTY", "");
    RequireSecretError(cogito::ResolveSecret({"env:COGITO_SECRET_TEST_EMPTY"}),
                       cogito::Errc::SecretError);
  }

  RequireSecretError(cogito::ResolveSecret({"env:COGITO_SECRET_TEST_MISSING"}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({"env:1INVALID"}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({"env:BAD-NAME"}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({"env:" + std::string(257U, 'A')}),
                     cogito::Errc::SecretError);

#if !defined(_WIN32)
  const std::string maximum_name = "A" + std::string(255U, 'B');
  ScopedEnvironment maximum(maximum_name, std::string(65536U, 'x'));
  auto maximum_result = cogito::ResolveSecret({"env:" + maximum_name});
  REQUIRE(maximum_result.ok());
  REQUIRE(maximum_result.value().size() == 65536U);

  ScopedEnvironment oversized("COGITO_SECRET_TEST_OVERSIZED", std::string(65537U, 'x'));
  RequireSecretError(cogito::ResolveSecret({"env:COGITO_SECRET_TEST_OVERSIZED"}),
                     cogito::Errc::TooLarge);
#endif
}

TEST_CASE("ResolveSecret reads secure files and normalizes one trailing newline",
          "[secret][file]") {
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  TempDirectory temporary;

  const std::filesystem::path plain = temporary.path() / "plain.secret";
  const std::filesystem::path lf = temporary.path() / "lf.secret";
  const std::filesystem::path crlf = temporary.path() / "crlf.secret";
  const std::filesystem::path twice = temporary.path() / "twice.secret";
  const std::filesystem::path newline_only = temporary.path() / "newline.secret";
  const std::filesystem::path carriage_return = temporary.path() / "cr.secret";
  const std::filesystem::path binary = temporary.path() / "binary.secret";
  const std::filesystem::path empty = temporary.path() / "empty.secret";
  const std::filesystem::path maximum = temporary.path() / "maximum.secret";
  const std::filesystem::path oversized = temporary.path() / "oversized.secret";

  WriteSecretFile(plain, "plain-secret");
  WriteSecretFile(lf, "line-feed\n");
  WriteSecretFile(crlf, "windows-line\r\n");
  WriteSecretFile(twice, "two\n\n");
  WriteSecretFile(newline_only, "\n");
  WriteSecretFile(carriage_return, "\r");
  WriteSecretFile(binary, std::string("a\0b\n", 4U));
  WriteSecretFile(empty, "");
  WriteSecretFile(maximum, std::string(65536U, 'm'));
  WriteSecretFile(oversized, std::string(65537U, 'o'));

  RequireSecretValue(cogito::ResolveSecret({FileUri(plain)}), "plain-secret");
  RequireSecretValue(cogito::ResolveSecret({FileUri(lf)}), "line-feed");
  RequireSecretValue(cogito::ResolveSecret({FileUri(crlf)}), "windows-line");
  RequireSecretValue(cogito::ResolveSecret({FileUri(twice)}), "two\n");
  RequireSecretValue(cogito::ResolveSecret({FileUri(carriage_return)}), "\r");
  RequireSecretValue(cogito::ResolveSecret({FileUri(binary)}),
                     std::string_view("a\0b", 3U));
  RequireSecretError(cogito::ResolveSecret({FileUri(newline_only)}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({FileUri(empty)}),
                     cogito::Errc::SecretError);

  auto maximum_result = cogito::ResolveSecret({FileUri(maximum)});
  REQUIRE(maximum_result.ok());
  REQUIRE(maximum_result.value().size() == 65536U);
  RequireSecretError(cogito::ResolveSecret({FileUri(oversized)}),
                     cogito::Errc::TooLarge);

  RequireSecretError(cogito::ResolveSecret({"file:relative.secret"}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({FileUri(temporary.path() / "missing.secret")}),
                     cogito::Errc::SecretError);
}

TEST_CASE("CheckSecretFilePermissions fails closed for unsafe file objects",
          "[secret][file][permissions]") {
  TempDirectory temporary;
  const std::filesystem::path secure = temporary.path() / "secure.secret";
  WriteSecretFile(secure, "secret", 0600U);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(secure)).ok());

#if defined(_WIN32)
  const std::filesystem::path broad = temporary.path() / "broad.secret";
  WriteSecretFile(broad, "secret");
  SetFileDacl(broad, true);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(broad)).code ==
          cogito::Errc::Forbidden);
  RequireSecretError(cogito::ResolveSecret({FileUri(broad)}), cogito::Errc::Forbidden);

  const std::filesystem::path null_dacl = temporary.path() / "null-dacl.secret";
  WriteSecretFile(null_dacl, "secret");
  SetNullFileDacl(null_dacl);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(null_dacl)).code ==
          cogito::Errc::Forbidden);
  RequireSecretError(cogito::ResolveSecret({FileUri(null_dacl)}),
                     cogito::Errc::Forbidden);

  const std::filesystem::path link = temporary.path() / "link.secret";
  std::error_code link_error;
  std::filesystem::create_symlink(secure, link, link_error);
  if (!link_error) {
    REQUIRE(cogito::CheckSecretFilePermissions(NativePath(link)).code ==
            cogito::Errc::Forbidden);
    RequireSecretError(cogito::ResolveSecret({FileUri(link)}),
                       cogito::Errc::Forbidden);
  }
#else
  const std::filesystem::path readonly = temporary.path() / "readonly.secret";
  const std::filesystem::path group_readable = temporary.path() / "group.secret";
  const std::filesystem::path executable = temporary.path() / "executable.secret";
  WriteSecretFile(readonly, "secret", 0400U);
  WriteSecretFile(group_readable, "secret", 0644U);
  WriteSecretFile(executable, "secret", 0777U);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(readonly)).ok());
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(group_readable)).code ==
          cogito::Errc::Forbidden);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(executable)).code ==
          cogito::Errc::Forbidden);
  RequireSecretError(cogito::ResolveSecret({FileUri(group_readable)}),
                     cogito::Errc::Forbidden);

  const std::filesystem::path link = temporary.path() / "link.secret";
  std::error_code link_error;
  std::filesystem::create_symlink(secure, link, link_error);
  REQUIRE_FALSE(link_error);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(link)).code ==
          cogito::Errc::Forbidden);
  RequireSecretError(cogito::ResolveSecret({FileUri(link)}), cogito::Errc::Forbidden);

  const std::filesystem::path fifo = temporary.path() / "secret.fifo";
  REQUIRE(mkfifo(fifo.c_str(), static_cast<mode_t>(0600)) == 0);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(fifo)).code ==
          cogito::Errc::Forbidden);
  RequireSecretError(cogito::ResolveSecret({FileUri(fifo)}), cogito::Errc::Forbidden);
#endif

  REQUIRE(cogito::CheckSecretFilePermissions("relative.secret").code ==
          cogito::Errc::Forbidden);
  REQUIRE(cogito::CheckSecretFilePermissions(
              NativePath(temporary.path() / "missing.secret"))
              .code == cogito::Errc::Forbidden);
  REQUIRE(cogito::CheckSecretFilePermissions(NativePath(temporary.path())).code ==
          cogito::Errc::Forbidden);
}

TEST_CASE("ResolveSecret rejects unsupported credential backends after validation",
          "[secret][backend]") {
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  RequireSecretError(cogito::ResolveSecret({"keyring:service/user"}),
                     cogito::Errc::SecretError);
  const auto unique_suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  RequireSecretError(cogito::ResolveSecret(
                         {"wincred:cogito-test-missing-" +
                          std::to_string(unique_suffix)}),
                     cogito::Errc::SecretError);
  RequireSecretError(cogito::ResolveSecret({"wincred:" + std::string(257U, 'x')}),
                     cogito::Errc::SecretError);
  std::string invalid_target = "wincred:";
  invalid_target.push_back(static_cast<char>(0xC0));
  invalid_target.push_back(static_cast<char>(0xAF));
  RequireSecretError(cogito::ResolveSecret({invalid_target}), cogito::Errc::SecretError);
}

TEST_CASE("Resolved SecretString storage is zeroized before release",
          "[secret][zeroize]") {
  ScopedEnvironment environment("COGITO_SECRET_TEST_ZEROIZE", "zeroize-canary");
  bool observed = false;
  bool all_zero = true;
  {
    auto secret = cogito::ResolveSecret({"env:COGITO_SECRET_TEST_ZEROIZE"});
    REQUIRE(secret.ok());
    REQUIRE(secret.value().Expose() == "zeroize-canary");
    const volatile void* final_storage = secret.value().Expose().data();
    cogito::testing::SetCleanseObserver(
        [&, final_storage](const volatile void* pointer, std::size_t size) {
          if (pointer == final_storage &&
              size == std::string_view("zeroize-canary").size()) {
            observed = true;
            const auto* bytes = static_cast<const volatile unsigned char*>(pointer);
            for (std::size_t index = 0U; index < size; ++index) {
              all_zero = all_zero && bytes[index] == 0U;
            }
          }
        });
  }
  cogito::testing::ClearCleanseObserver();
  REQUIRE(observed);
  REQUIRE(all_zero);
}
