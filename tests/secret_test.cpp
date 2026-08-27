// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <accctrl.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

bool IsZeroed(const volatile void* ptr, std::size_t size) {
  const auto* bytes = static_cast<const volatile unsigned char*>(ptr);
  for (std::size_t index = 0U; index < size; ++index) {
    if (bytes[index] != 0U) {
      return false;
    }
  }
  return true;
}

std::uint64_t ProcessId() noexcept {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path UniqueTemporaryPath(std::string_view label) {
  static std::atomic<std::uint64_t> sequence{0U};
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("cogito-s2-" + std::string(label) + "-" + std::to_string(ProcessId()) + "-" +
          std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1U)));
}

class ScopedPath {
 public:
  explicit ScopedPath(std::string_view label) : path_(UniqueTemporaryPath(label)) {}
  ~ScopedPath() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  ScopedPath(const ScopedPath&) = delete;
  ScopedPath& operator=(const ScopedPath&) = delete;

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

#if defined(_WIN32)

class LocalAcl {
 public:
  ~LocalAcl() {
    if (acl_ != nullptr) {
      LocalFree(acl_);
    }
  }

  PACL* out() noexcept { return &acl_; }
  PACL get() const noexcept { return acl_; }

 private:
  PACL acl_ = nullptr;
};

std::vector<std::max_align_t> CurrentTokenUserBuffer() {
  HANDLE raw_token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
    throw std::runtime_error("OpenProcessToken failed");
  }
  const auto close_token = [&] { CloseHandle(raw_token); };

  DWORD required = 0U;
  static_cast<void>(GetTokenInformation(raw_token, TokenUser, nullptr, 0U, &required));
  if (required == 0U || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    close_token();
    throw std::runtime_error("GetTokenInformation size query failed");
  }
  const std::size_t units =
      (static_cast<std::size_t>(required) + sizeof(std::max_align_t) - 1U) /
      sizeof(std::max_align_t);
  std::vector<std::max_align_t> buffer(units);
  if (GetTokenInformation(raw_token, TokenUser, buffer.data(), required, &required) == FALSE) {
    close_token();
    throw std::runtime_error("GetTokenInformation failed");
  }
  close_token();
  return buffer;
}

void SetTestDacl(const std::filesystem::path& path, bool allow_everyone_read) {
  std::vector<std::max_align_t> token_buffer = CurrentTokenUserBuffer();
  const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());

  alignas(SID) std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_storage{};
  alignas(SID) std::array<unsigned char, SECURITY_MAX_SID_SIZE>
      administrators_storage{};
  alignas(SID) std::array<unsigned char, SECURITY_MAX_SID_SIZE> everyone_storage{};
  DWORD system_size = static_cast<DWORD>(system_storage.size());
  DWORD administrators_size = static_cast<DWORD>(administrators_storage.size());
  DWORD everyone_size = static_cast<DWORD>(everyone_storage.size());
  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system_storage.data(), &system_size) ==
          FALSE ||
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators_storage.data(),
                         &administrators_size) == FALSE ||
      CreateWellKnownSid(WinWorldSid, nullptr, everyone_storage.data(), &everyone_size) == FALSE) {
    throw std::runtime_error("CreateWellKnownSid failed");
  }

  std::array<EXPLICIT_ACCESSW, 4U> entries{};
  entries[0].grfAccessPermissions = FILE_ALL_ACCESS;
  entries[1].grfAccessPermissions = GENERIC_READ;
  entries[2].grfAccessPermissions = GENERIC_READ;
  entries[3].grfAccessPermissions = GENERIC_READ;
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    entries[index].grfAccessMode = SET_ACCESS;
    entries[index].grfInheritance = NO_INHERITANCE;
  }
  BuildTrusteeWithSidW(&entries[0].Trustee, token_user->User.Sid);
  BuildTrusteeWithSidW(&entries[1].Trustee, system_storage.data());
  BuildTrusteeWithSidW(&entries[2].Trustee, administrators_storage.data());
  BuildTrusteeWithSidW(&entries[3].Trustee, everyone_storage.data());

  LocalAcl acl;
  const ULONG entry_count = allow_everyone_read ? 4U : 3U;
  if (SetEntriesInAclW(entry_count, entries.data(), nullptr, acl.out()) != ERROR_SUCCESS) {
    throw std::runtime_error("SetEntriesInAclW failed");
  }
  std::wstring mutable_path = path.wstring();
  const DWORD result = SetNamedSecurityInfoW(
      mutable_path.data(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
      acl.get(), nullptr);
  if (result != ERROR_SUCCESS) {
    throw std::runtime_error("SetNamedSecurityInfoW failed");
  }
}

#endif

void WriteFile(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("test file open failed");
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("test file write failed");
  }
  output.close();
#if defined(_WIN32)
  SetTestDacl(path, false);
#else
  if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    throw std::runtime_error("chmod failed");
  }
#endif
}

std::string FileUri(const std::filesystem::path& path) {
  return "file:" + path.u8string();
}

void SetEnvironment(const std::string& name, const std::string& value) {
#if defined(_WIN32)
  if (_putenv_s(name.c_str(), value.c_str()) != 0) {
    throw std::runtime_error("_putenv_s failed");
  }
#else
  if (setenv(name.c_str(), value.c_str(), 1) != 0) {
    throw std::runtime_error("setenv failed");
  }
#endif
}

void UnsetEnvironment(const std::string& name) noexcept {
#if defined(_WIN32)
  static_cast<void>(_putenv_s(name.c_str(), ""));
#else
  static_cast<void>(unsetenv(name.c_str()));
#endif
}

std::string UniqueEnvironmentName() {
  return "COGITO_S2_SECRET_TEST_" + std::to_string(ProcessId()) + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

struct SecretSeamReset {
  ~SecretSeamReset() {
    cogito::testing::SecretTestSeam::ClearMockSecrets();
    cogito::testing::ClearCleanseObserver();
  }
};

}  // namespace

TEST_CASE("SecretRef splits only at the first colon", "[secret][uri]") {
  const cogito::SecretRef env{"env:API_TOKEN"};
  REQUIRE(env.scheme() == "env");
  REQUIRE(env.location() == "API_TOKEN");

  const cogito::SecretRef nested{"keyring:service:user/name"};
  REQUIRE(nested.scheme() == "keyring");
  REQUIRE(nested.location() == "service:user/name");

  const cogito::SecretRef missing{"no_separator"};
  REQUIRE(missing.scheme().empty());
  REQUIRE(missing.location().empty());
}

TEST_CASE("ResolveSecret rejects malformed URIs before consulting the seam",
          "[secret][uri][negative]") {
  SecretSeamReset reset;
  const std::string oversized = "env:" + std::string(1021U, 'A');
  std::string embedded_nul = "env:VALID";
  embedded_nul.push_back('\0');
  embedded_nul += "TAIL";
  const std::string unicode_line_separator = "env:X\xE2\x80\xA8Y";
  const std::string unicode_paragraph_separator = "env:X\xE2\x80\xA9Y";

  const std::string invalid_uris[] = {
      "",          "env:",     "ENV:NAME", "unknown:value", "missing-colon",
      "env:X\rY", "env:X\nY", oversized,   embedded_nul,     unicode_line_separator,
      unicode_paragraph_separator};
  for (const std::string& uri : invalid_uris) {
    INFO("invalid URI size: " << uri.size());
    cogito::testing::SecretTestSeam::SetMockSecret(uri, "must-not-escape");
    auto resolved = cogito::ResolveSecret(cogito::SecretRef{uri});
    REQUIRE_FALSE(resolved.ok());
    REQUIRE(resolved.error().code == cogito::Errc::SecretError);
  }
}

TEST_CASE("Environment secrets enforce names, presence, emptiness, and value size",
          "[secret][env]") {
  const std::string name = UniqueEnvironmentName();
  UnsetEnvironment(name);

  auto missing = cogito::ResolveSecret(cogito::SecretRef{"env:" + name});
  REQUIRE_FALSE(missing.ok());
  REQUIRE(missing.error().code == cogito::Errc::SecretError);

  SetEnvironment(name, "environment-secret");
  auto resolved = cogito::ResolveSecret(cogito::SecretRef{"env:" + name});
  REQUIRE(resolved.ok());
  REQUIRE(resolved.value().Expose() == "environment-secret");

  SetEnvironment(name, "");
  auto empty = cogito::ResolveSecret(cogito::SecretRef{"env:" + name});
  REQUIRE_FALSE(empty.ok());
  REQUIRE(empty.error().code == cogito::Errc::SecretError);
  UnsetEnvironment(name);

  const std::string invalid_names[] = {"1BAD", "HAS-DASH", "HAS.DOT",
                                       std::string(257U, 'A')};
  for (const std::string& invalid_name : invalid_names) {
    auto invalid = cogito::ResolveSecret(cogito::SecretRef{"env:" + invalid_name});
    REQUIRE_FALSE(invalid.ok());
    REQUIRE(invalid.error().code == cogito::Errc::SecretError);
  }

#if !defined(_WIN32)
  SetEnvironment(name, std::string(65537U, 'x'));
  auto oversized = cogito::ResolveSecret(cogito::SecretRef{"env:" + name});
  REQUIRE_FALSE(oversized.ok());
  REQUIRE(oversized.error().code == cogito::Errc::TooLarge);
  UnsetEnvironment(name);
#endif
}

TEST_CASE("Secret files use the validated handle and remove exactly one final newline",
          "[secret][file]") {
  ScopedPath file("normalization");

  const std::pair<std::string, std::string> vectors[] = {
      {"plain", "plain"},
      {"line\n", "line"},
      {"line\r\n", "line"},
      {"line\n\n", "line\n"},
      {"line\r\n\r\n", "line\r\n"},
  };
  for (const auto& vector : vectors) {
    WriteFile(file.path(), vector.first);
    auto resolved = cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())});
    REQUIRE(resolved.ok());
    REQUIRE(resolved.value().Expose() == vector.second);
    REQUIRE(cogito::CheckSecretFilePermissions(file.path().u8string()).ok());
  }
}

TEST_CASE("Secret files preserve embedded NUL bytes", "[secret][file][binary]") {
  ScopedPath file("binary");
  const std::string bytes{"a\0b\n", 4U};
  WriteFile(file.path(), bytes);

  auto resolved = cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())});
  REQUIRE(resolved.ok());
  REQUIRE(resolved.value().size() == 3U);
  REQUIRE(resolved.value().Expose() == std::string_view(bytes.data(), 3U));
}

TEST_CASE("Secret file content size boundaries fail closed", "[secret][file][negative]") {
  ScopedPath empty_file("empty");
  WriteFile(empty_file.path(), "");
  REQUIRE(cogito::CheckSecretFilePermissions(empty_file.path().u8string()).ok());
  auto empty = cogito::ResolveSecret(cogito::SecretRef{FileUri(empty_file.path())});
  REQUIRE_FALSE(empty.ok());
  REQUIRE(empty.error().code == cogito::Errc::SecretError);

  ScopedPath newline_file("newline-only");
  WriteFile(newline_file.path(), "\r\n");
  auto newline = cogito::ResolveSecret(cogito::SecretRef{FileUri(newline_file.path())});
  REQUIRE_FALSE(newline.ok());
  REQUIRE(newline.error().code == cogito::Errc::SecretError);

  ScopedPath maximum_file("maximum");
  WriteFile(maximum_file.path(), std::string(65536U, 'm'));
  auto maximum = cogito::ResolveSecret(cogito::SecretRef{FileUri(maximum_file.path())});
  REQUIRE(maximum.ok());
  REQUIRE(maximum.value().size() == 65536U);

  ScopedPath oversized_file("oversized");
  WriteFile(oversized_file.path(), std::string(65537U, 'x'));
  auto oversized = cogito::ResolveSecret(cogito::SecretRef{FileUri(oversized_file.path())});
  REQUIRE_FALSE(oversized.ok());
  REQUIRE(oversized.error().code == cogito::Errc::TooLarge);
}

TEST_CASE("Relative secret file paths are rejected by the real backend",
          "[secret][file][negative]") {
  auto relative = cogito::ResolveSecret(cogito::SecretRef{"file:relative/secret"});
  REQUIRE_FALSE(relative.ok());
  REQUIRE(relative.error().code == cogito::Errc::SecretError);
  REQUIRE(cogito::CheckSecretFilePermissions("relative/secret").code ==
          cogito::Errc::Forbidden);
}

#if !defined(_WIN32)

TEST_CASE("POSIX secret permissions allow only 0600 and 0400",
          "[secret][file][permissions]") {
  ScopedPath file("permissions");
  WriteFile(file.path(), "secret");

  REQUIRE(chmod(file.path().c_str(), S_IRUSR | S_IWUSR) == 0);
  REQUIRE(cogito::CheckSecretFilePermissions(file.path().string()).ok());
  REQUIRE(cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())}).ok());

  REQUIRE(chmod(file.path().c_str(), S_IRUSR) == 0);
  REQUIRE(cogito::CheckSecretFilePermissions(file.path().string()).ok());
  REQUIRE(cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())}).ok());

  const mode_t rejected_modes[] = {
      static_cast<mode_t>(S_IRUSR | S_IWUSR | S_IRGRP),
      static_cast<mode_t>(S_IRWXU | S_IRWXG | S_IRWXO),
      static_cast<mode_t>(S_IRUSR | S_IWUSR | S_IXUSR),
  };
  for (const mode_t mode : rejected_modes) {
    REQUIRE(chmod(file.path().c_str(), mode) == 0);
    REQUIRE(cogito::CheckSecretFilePermissions(file.path().string()).code ==
            cogito::Errc::Forbidden);
    auto resolved = cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())});
    REQUIRE_FALSE(resolved.ok());
    REQUIRE(resolved.error().code == cogito::Errc::Forbidden);
  }
}

TEST_CASE("POSIX symlinks and FIFOs are rejected without blocking",
          "[secret][file][permissions]") {
  ScopedPath target("target");
  ScopedPath link("symlink");
  WriteFile(target.path(), "secret");
  std::error_code symlink_error;
  std::filesystem::create_symlink(target.path(), link.path(), symlink_error);
  REQUIRE_FALSE(symlink_error);

  REQUIRE(cogito::CheckSecretFilePermissions(link.path().string()).code ==
          cogito::Errc::Forbidden);
  auto symlink = cogito::ResolveSecret(cogito::SecretRef{FileUri(link.path())});
  REQUIRE_FALSE(symlink.ok());
  REQUIRE(symlink.error().code == cogito::Errc::Forbidden);

  ScopedPath fifo("fifo");
  REQUIRE(mkfifo(fifo.path().c_str(), S_IRUSR | S_IWUSR) == 0);
  REQUIRE(cogito::CheckSecretFilePermissions(fifo.path().string()).code ==
          cogito::Errc::Forbidden);
  auto fifo_result = cogito::ResolveSecret(cogito::SecretRef{FileUri(fifo.path())});
  REQUIRE_FALSE(fifo_result.ok());
  REQUIRE(fifo_result.error().code == cogito::Errc::Forbidden);
}

#else

TEST_CASE("Windows secret DACL rejects read access for Everyone",
          "[secret][file][permissions]") {
  ScopedPath file("unsafe-dacl");
  WriteFile(file.path(), "secret");
  SetTestDacl(file.path(), true);

  REQUIRE(cogito::CheckSecretFilePermissions(file.path().u8string()).code ==
          cogito::Errc::Forbidden);
  auto resolved = cogito::ResolveSecret(cogito::SecretRef{FileUri(file.path())});
  REQUIRE_FALSE(resolved.ok());
  REQUIRE(resolved.error().code == cogito::Errc::Forbidden);
}

TEST_CASE("Windows directories and available reparse-point symlinks are rejected",
          "[secret][file][permissions]") {
  ScopedPath directory("directory");
  REQUIRE(std::filesystem::create_directory(directory.path()));
  REQUIRE(cogito::CheckSecretFilePermissions(directory.path().u8string()).code ==
          cogito::Errc::Forbidden);

  ScopedPath target("target");
  ScopedPath link("symlink");
  WriteFile(target.path(), "secret");
  std::error_code symlink_error;
  std::filesystem::create_symlink(target.path(), link.path(), symlink_error);
  if (symlink_error) {
    WARN("Windows symlink creation unavailable: " << symlink_error.message());
  } else {
    REQUIRE(cogito::CheckSecretFilePermissions(link.path().u8string()).code ==
            cogito::Errc::Forbidden);
    auto resolved = cogito::ResolveSecret(cogito::SecretRef{FileUri(link.path())});
    REQUIRE_FALSE(resolved.ok());
    REQUIRE(resolved.error().code == cogito::Errc::Forbidden);
  }
}

#endif

TEST_CASE("SecretTestSeam is URI-gated, thread-local, and supports unsupported backends",
          "[secret][seam]") {
  SecretSeamReset reset;
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  cogito::testing::SecretTestSeam::SetMockSecret("keyring:service/user", "mock-keyring");
  cogito::testing::SecretTestSeam::SetMockSecret("file:relative", "mock-relative");
  const std::string boundary_uri = "keyring:" + std::string(1016U, 'u');
  cogito::testing::SecretTestSeam::SetMockSecret(boundary_uri, "mock-boundary");

  auto keyring = cogito::ResolveSecret(cogito::SecretRef{"keyring:service/user"});
  REQUIRE(keyring.ok());
  REQUIRE(keyring.value().Expose() == "mock-keyring");
  auto relative = cogito::ResolveSecret(cogito::SecretRef{"file:relative"});
  REQUIRE(relative.ok());
  REQUIRE(relative.value().Expose() == "mock-relative");
  auto boundary = cogito::ResolveSecret(cogito::SecretRef{boundary_uri});
  REQUIRE(boundary.ok());
  REQUIRE(boundary.value().Expose() == "mock-boundary");

  std::atomic<bool> worker_rejected{false};
  std::thread worker([&] {
    auto isolated = cogito::ResolveSecret(cogito::SecretRef{"keyring:service/user"});
    worker_rejected.store(!isolated.ok() &&
                          isolated.error().code == cogito::Errc::SecretError);
  });
  worker.join();
  REQUIRE(worker_rejected.load());
}

TEST_CASE("ClearMockSecrets zeroizes registered values before releasing storage",
          "[secret][seam][zeroize]") {
  SecretSeamReset reset;
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  const std::string mock_value(256U, 's');
  cogito::testing::SecretTestSeam::SetMockSecret("wincred:test-target", mock_value);

  std::size_t observed_calls = 0U;
  bool observed_zeroed = false;
  cogito::testing::SetCleanseObserver(
      [&](const volatile void* ptr, std::size_t size) {
        if (size == mock_value.size()) {
          ++observed_calls;
          observed_zeroed = IsZeroed(ptr, size);
        }
      });
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  REQUIRE(observed_calls == 1U);
  REQUIRE(observed_zeroed);

  auto cleared = cogito::ResolveSecret(cogito::SecretRef{"wincred:test-target"});
  REQUIRE_FALSE(cleared.ok());
  REQUIRE(cleared.error().code == cogito::Errc::SecretError);
}

TEST_CASE("Unsupported real secret backends fail closed", "[secret][backend]") {
  SecretSeamReset reset;
  cogito::testing::SecretTestSeam::ClearMockSecrets();
  auto keyring = cogito::ResolveSecret(cogito::SecretRef{"keyring:service/user"});
  REQUIRE_FALSE(keyring.ok());
  REQUIRE(keyring.error().code == cogito::Errc::SecretError);

  auto wincred = cogito::ResolveSecret(
      cogito::SecretRef{"wincred:cogito-test-credential-that-does-not-exist"});
  REQUIRE_FALSE(wincred.ok());
  REQUIRE(wincred.error().code == cogito::Errc::SecretError);
}
