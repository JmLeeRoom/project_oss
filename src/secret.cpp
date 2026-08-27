// SPDX-License-Identifier: Apache-2.0

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cogito/config.hpp"

#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/crypto.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <aclapi.h>
#include <wincred.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cogito {
namespace {

constexpr std::size_t kMaxSecretUriBytes = 1024U;
constexpr std::size_t kMaxSecretLocationBytes = 1024U;
constexpr std::size_t kMaxBackendNameBytes = 256U;
constexpr std::size_t kMaxSecretBytes = 65536U;

Error MakeSecretError(Errc code, const char* message) {
  return Error{code, code == Errc::TooLarge ? reason::kInputTooLarge : "", message};
}

Error SecretError(const char* message) {
  return MakeSecretError(Errc::SecretError, message);
}

Error ForbiddenError(const char* message) {
  return MakeSecretError(Errc::Forbidden, message);
}

Error TooLargeError(const char* message) {
  return MakeSecretError(Errc::TooLarge, message);
}

bool ContainsNul(std::string_view value) noexcept {
  return value.find('\0') != std::string_view::npos;
}

bool IsSupportedScheme(std::string_view scheme) noexcept {
  return scheme == "env" || scheme == "file" || scheme == "wincred" ||
         scheme == "keyring";
}

Error ValidateSecretUri(const SecretRef& ref) {
  const std::string_view uri(ref.uri);
  if (uri.empty() || uri.size() > kMaxSecretUriBytes || ContainsNul(uri) ||
      uri.find_first_of("\r\n") != std::string_view::npos) {
    return SecretError("secret reference URI violates its byte constraints");
  }

  const std::size_t separator = uri.find(':');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= uri.size() ||
      !IsSupportedScheme(uri.substr(0U, separator))) {
    return SecretError("secret reference URI has an unsupported format");
  }
  return Error::Ok();
}

bool IsAsciiAlpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsValidEnvironmentName(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxBackendNameBytes || ContainsNul(name)) {
    return false;
  }
  const char first = name.front();
  if (!IsAsciiAlpha(first) && first != '_') {
    return false;
  }
  for (const char value : name.substr(1U)) {
    if (!IsAsciiAlpha(value) && (value < '0' || value > '9') && value != '_') {
      return false;
    }
  }
  return true;
}

bool IsValidUtf8(std::string_view value) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
  std::size_t offset = 0U;
  while (offset < value.size()) {
    const unsigned char lead = bytes[offset];
    if (lead <= 0x7FU) {
      ++offset;
      continue;
    }

    std::size_t continuation_count = 0U;
    std::uint32_t code_point = 0U;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      continuation_count = 1U;
      code_point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      continuation_count = 2U;
      code_point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      continuation_count = 3U;
      code_point = lead & 0x07U;
    } else {
      return false;
    }

    if (continuation_count > value.size() - offset - 1U) {
      return false;
    }
    for (std::size_t index = 1U; index <= continuation_count; ++index) {
      const unsigned char continuation = bytes[offset + index];
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
    offset += continuation_count + 1U;
  }
  return true;
}

bool IsAbsoluteSecretPath(std::string_view path) noexcept {
  if (path.empty() || path.size() > kMaxSecretLocationBytes || ContainsNul(path)) {
    return false;
  }
#if defined(_WIN32)
  const bool drive_path = path.size() >= 3U && IsAsciiAlpha(path[0]) &&
                          path[1] == ':' && path[2] == '\\';
  const bool unc_path = path.size() >= 3U && path[0] == '\\' && path[1] == '\\';
  return drive_path || unc_path;
#else
  return path.front() == '/';
#endif
}

void CleanseString(std::string& value) noexcept {
  if (!value.empty()) {
    OPENSSL_cleanse(value.data(), value.size());
  }
  value.clear();
}

void TrimOneTrailingNewline(std::string& value) {
  if (value.size() >= 2U && value[value.size() - 2U] == '\r' &&
      value.back() == '\n') {
    OPENSSL_cleanse(value.data() + value.size() - 2U, 2U);
    value.resize(value.size() - 2U);
  } else if (!value.empty() && value.back() == '\n') {
    OPENSSL_cleanse(value.data() + value.size() - 1U, 1U);
    value.resize(value.size() - 1U);
  }
}

#if defined(COGITO_TESTING)
thread_local std::map<std::string, SecretString> g_mock_secrets;
#endif

#if defined(_WIN32)

class NativeHandle {
 public:
  NativeHandle() = default;
  explicit NativeHandle(HANDLE handle) noexcept : handle_(handle) {}
  ~NativeHandle() { Reset(); }

  NativeHandle(const NativeHandle&) = delete;
  NativeHandle& operator=(const NativeHandle&) = delete;

  NativeHandle(NativeHandle&& other) noexcept : handle_(other.Release()) {}
  NativeHandle& operator=(NativeHandle&& other) noexcept {
    if (this != &other) {
      Reset();
      handle_ = other.Release();
    }
    return *this;
  }

  HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE Release() noexcept {
    const HANDLE released = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return released;
  }

  void Reset() noexcept {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
    handle_ = INVALID_HANDLE_VALUE;
  }

  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class LocalSecurityDescriptor {
 public:
  explicit LocalSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor) noexcept
      : descriptor_(descriptor) {}
  ~LocalSecurityDescriptor() {
    if (descriptor_ != nullptr) {
      LocalFree(descriptor_);
    }
  }

  LocalSecurityDescriptor(const LocalSecurityDescriptor&) = delete;
  LocalSecurityDescriptor& operator=(const LocalSecurityDescriptor&) = delete;

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};

Result<std::wstring> Utf8ToWide(std::string_view value) {
  if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX) ||
      !IsValidUtf8(value)) {
    return SecretError("secret backend identifier is not valid UTF-8");
  }
  const int input_size = static_cast<int>(value.size());
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           input_size, nullptr, 0);
  if (required <= 0) {
    return SecretError("secret backend identifier is not valid UTF-8");
  }
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            input_size, result.data(), required);
  if (converted != required) {
    return SecretError("secret backend identifier conversion failed");
  }
  return result;
}

bool IsAllowedSid(PSID candidate, PSID current_user, PSID system_sid,
                  PSID administrators_sid) noexcept {
  return candidate != nullptr && IsValidSid(candidate) != FALSE &&
         (EqualSid(candidate, current_user) != FALSE ||
          EqualSid(candidate, system_sid) != FALSE ||
          EqualSid(candidate, administrators_sid) != FALSE);
}

Error ValidateWindowsDacl(HANDLE file) {
  HANDLE raw_token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
    return ForbiddenError("unable to inspect the current process token");
  }
  NativeHandle token(raw_token);

  DWORD token_bytes = 0U;
  GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &token_bytes);
  if (token_bytes == 0U || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return ForbiddenError("unable to inspect the current process identity");
  }
  std::vector<unsigned char> token_storage(token_bytes);
  if (GetTokenInformation(token.get(), TokenUser, token_storage.data(), token_bytes,
                          &token_bytes) == FALSE) {
    return ForbiddenError("unable to inspect the current process identity");
  }
  auto* token_user = reinterpret_cast<TOKEN_USER*>(token_storage.data());
  PSID current_user = token_user->User.Sid;
  if (current_user == nullptr || IsValidSid(current_user) == FALSE) {
    return ForbiddenError("the current process user SID is invalid");
  }

  alignas(void*) std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_storage{};
  alignas(void*) std::array<unsigned char, SECURITY_MAX_SID_SIZE> admin_storage{};
  DWORD system_size = static_cast<DWORD>(system_storage.size());
  DWORD admin_size = static_cast<DWORD>(admin_storage.size());
  PSID system_sid = system_storage.data();
  PSID administrators_sid = admin_storage.data();
  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid, &system_size) == FALSE ||
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators_sid,
                         &admin_size) == FALSE) {
    return ForbiddenError("unable to construct the trusted Windows SIDs");
  }

  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
  const DWORD security_status = GetSecurityInfo(
      file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &owner, nullptr, &dacl, nullptr, &raw_descriptor);
  if (security_status != ERROR_SUCCESS || raw_descriptor == nullptr) {
    if (raw_descriptor != nullptr) {
      LocalFree(raw_descriptor);
    }
    return ForbiddenError("unable to inspect the secret file DACL");
  }
  LocalSecurityDescriptor descriptor(raw_descriptor);

  if (!IsAllowedSid(owner, current_user, system_sid, administrators_sid)) {
    return ForbiddenError("secret file owner is not trusted");
  }
  if (dacl == nullptr) {
    return ForbiddenError("secret file has a NULL DACL");
  }

  ACL_SIZE_INFORMATION acl_info{};
  if (GetAclInformation(dacl, &acl_info, sizeof(acl_info), AclSizeInformation) == FALSE) {
    return ForbiddenError("unable to enumerate the secret file DACL");
  }

  constexpr DWORD kReadRights = FILE_READ_DATA | GENERIC_READ | GENERIC_ALL;
  for (DWORD index = 0U; index < acl_info.AceCount; ++index) {
    void* raw_ace = nullptr;
    if (GetAce(dacl, index, &raw_ace) == FALSE || raw_ace == nullptr) {
      return ForbiddenError("unable to enumerate the secret file DACL");
    }
    const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
      const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
      if ((ace->Mask & kReadRights) != 0U &&
          !IsAllowedSid(const_cast<DWORD*>(&ace->SidStart), current_user, system_sid,
                        administrators_sid)) {
        return ForbiddenError("secret file grants read access to an untrusted SID");
      }
    } else if (header->AceType == ACCESS_ALLOWED_COMPOUND_ACE_TYPE ||
               header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
               header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
               header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE) {
      return ForbiddenError("secret file contains an unsupported access-allow ACE");
    }
  }
  return Error::Ok();
}

Error WindowsOpenError(DWORD code) {
  if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION) {
    return ForbiddenError("access to the secret file was denied");
  }
  return SecretError("unable to open the secret file");
}

Result<NativeHandle> OpenValidatedSecretFile(const std::string& path) {
  auto wide_path = Utf8ToWide(path);
  if (!wide_path) {
    return wide_path.error();
  }
  const HANDLE raw_file = CreateFileW(wide_path.value().c_str(), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (raw_file == INVALID_HANDLE_VALUE) {
    return WindowsOpenError(GetLastError());
  }
  NativeHandle file(raw_file);

  BY_HANDLE_FILE_INFORMATION info{};
  if (GetFileInformationByHandle(file.get(), &info) == FALSE) {
    return ForbiddenError("unable to inspect the opened secret file");
  }
  if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
      0U) {
    return ForbiddenError("secret file is not a regular non-reparse file");
  }
  if (Error error = ValidateWindowsDacl(file.get()); error) {
    return error;
  }
  return Result<NativeHandle>{std::move(file)};
}

Result<SecretString> ReadValidatedSecretFile(NativeHandle& file) {
  LARGE_INTEGER size{};
  if (GetFileSizeEx(file.get(), &size) == FALSE || size.QuadPart < 0) {
    return SecretError("unable to determine the secret file size");
  }
  if (size.QuadPart == 0) {
    return SecretError("secret file is empty");
  }
  if (static_cast<unsigned long long>(size.QuadPart) > kMaxSecretBytes) {
    return TooLargeError("secret file exceeds its byte limit");
  }

  std::string value(kMaxSecretBytes + 1U, '\0');
  std::size_t total = 0U;
  while (total < value.size()) {
    DWORD count = 0U;
    const DWORD remaining = static_cast<DWORD>(value.size() - total);
    if (ReadFile(file.get(), value.data() + total, remaining, &count, nullptr) == FALSE) {
      CleanseString(value);
      return SecretError("unable to read the secret file");
    }
    if (count == 0U) {
      break;
    }
    total += count;
  }
  if (total > kMaxSecretBytes) {
    CleanseString(value);
    return TooLargeError("secret file exceeds its byte limit");
  }
  value.resize(total);
  if (value.empty()) {
    return SecretError("secret file is empty");
  }
  TrimOneTrailingNewline(value);
  if (value.empty()) {
    return SecretError("secret file contains no secret bytes");
  }
  return SecretString(std::move(value));
}

class CredentialGuard {
 public:
  explicit CredentialGuard(PCREDENTIALW credential) noexcept : credential_(credential) {}
  ~CredentialGuard() {
    if (credential_ != nullptr) {
      if (credential_->CredentialBlob != nullptr && credential_->CredentialBlobSize > 0U) {
        OPENSSL_cleanse(credential_->CredentialBlob, credential_->CredentialBlobSize);
      }
      CredFree(credential_);
    }
  }

  CredentialGuard(const CredentialGuard&) = delete;
  CredentialGuard& operator=(const CredentialGuard&) = delete;

 private:
  PCREDENTIALW credential_ = nullptr;
};

Result<SecretString> ResolveWindowsCredential(std::string_view target) {
  if (target.empty() || target.size() > kMaxBackendNameBytes || ContainsNul(target) ||
      !IsValidUtf8(target)) {
    return SecretError("Windows credential target is invalid");
  }
  auto wide_target = Utf8ToWide(target);
  if (!wide_target) {
    return wide_target.error();
  }

  PCREDENTIALW raw_credential = nullptr;
  if (CredReadW(wide_target.value().c_str(), CRED_TYPE_GENERIC, 0U,
                &raw_credential) == FALSE || raw_credential == nullptr) {
    return SecretError("Windows credential was not found");
  }
  CredentialGuard credential(raw_credential);
  const std::size_t size = raw_credential->CredentialBlobSize;
  if (size == 0U || raw_credential->CredentialBlob == nullptr) {
    return SecretError("Windows credential is empty");
  }
  if (size > kMaxSecretBytes) {
    return TooLargeError("Windows credential exceeds its byte limit");
  }
  const std::string_view blob(
      reinterpret_cast<const char*>(raw_credential->CredentialBlob), size);
  if (!IsValidUtf8(blob)) {
    return SecretError("Windows credential is not valid UTF-8");
  }
  return SecretString(std::string(blob));
}

#else

class NativeHandle {
 public:
  NativeHandle() = default;
  explicit NativeHandle(int descriptor) noexcept : descriptor_(descriptor) {}
  ~NativeHandle() { Reset(); }

  NativeHandle(const NativeHandle&) = delete;
  NativeHandle& operator=(const NativeHandle&) = delete;

  NativeHandle(NativeHandle&& other) noexcept : descriptor_(other.Release()) {}
  NativeHandle& operator=(NativeHandle&& other) noexcept {
    if (this != &other) {
      Reset();
      descriptor_ = other.Release();
    }
    return *this;
  }

  int get() const noexcept { return descriptor_; }

 private:
  int Release() noexcept {
    const int released = descriptor_;
    descriptor_ = -1;
    return released;
  }

  void Reset() noexcept {
    if (descriptor_ >= 0) {
      static_cast<void>(close(descriptor_));
    }
    descriptor_ = -1;
  }

  int descriptor_ = -1;
};

Error PosixOpenError(int code) {
  if (code == ELOOP || code == EACCES || code == EPERM || code == ENXIO ||
      code == ENODEV || code == EISDIR) {
    return ForbiddenError("access to the secret file was denied");
  }
  return SecretError("unable to open the secret file");
}

Result<NativeHandle> OpenValidatedSecretFile(const std::string& path) {
  const int descriptor = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (descriptor < 0) {
    return PosixOpenError(errno);
  }
  NativeHandle file(descriptor);

  struct stat status {};
  if (fstat(file.get(), &status) != 0) {
    return ForbiddenError("unable to inspect the opened secret file");
  }
  if (!S_ISREG(status.st_mode)) {
    return ForbiddenError("secret file is not a regular file");
  }
  if (status.st_uid != getuid() && status.st_uid != 0U) {
    return ForbiddenError("secret file owner is not trusted");
  }
  const mode_t permission_bits = status.st_mode & static_cast<mode_t>(0777);
  if (permission_bits != static_cast<mode_t>(0600) &&
      permission_bits != static_cast<mode_t>(0400)) {
    return ForbiddenError("secret file permissions must be 0600 or 0400");
  }
  return Result<NativeHandle>{std::move(file)};
}

Result<SecretString> ReadValidatedSecretFile(NativeHandle& file) {
  struct stat status {};
  if (fstat(file.get(), &status) != 0 || status.st_size < 0) {
    return SecretError("unable to determine the secret file size");
  }
  if (status.st_size == 0) {
    return SecretError("secret file is empty");
  }
  if (static_cast<std::uintmax_t>(status.st_size) > kMaxSecretBytes) {
    return TooLargeError("secret file exceeds its byte limit");
  }

  std::string value(kMaxSecretBytes + 1U, '\0');
  std::size_t total = 0U;
  while (total < value.size()) {
    const ssize_t count = read(file.get(), value.data() + total, value.size() - total);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      CleanseString(value);
      return SecretError("unable to read the secret file");
    }
    if (count == 0) {
      break;
    }
    total += static_cast<std::size_t>(count);
  }
  if (total > kMaxSecretBytes) {
    CleanseString(value);
    return TooLargeError("secret file exceeds its byte limit");
  }
  value.resize(total);
  if (value.empty()) {
    return SecretError("secret file is empty");
  }
  TrimOneTrailingNewline(value);
  if (value.empty()) {
    return SecretError("secret file contains no secret bytes");
  }
  return SecretString(std::move(value));
}

#endif

Result<SecretString> ResolveEnvironmentSecret(std::string_view name) {
  if (!IsValidEnvironmentName(name)) {
    return SecretError("environment secret name is invalid");
  }
  const std::string name_storage(name);
  const char* raw_value = std::getenv(name_storage.c_str());
  if (raw_value == nullptr || raw_value[0] == '\0') {
    return SecretError("environment secret is unset or empty");
  }
  std::size_t size = 0U;
  while (size <= kMaxSecretBytes && raw_value[size] != '\0') {
    ++size;
  }
  if (size > kMaxSecretBytes) {
    return TooLargeError("environment secret exceeds its byte limit");
  }
  return SecretString(std::string(raw_value, size));
}

}  // namespace

std::string_view SecretRef::scheme() const noexcept {
  const std::size_t separator = uri.find(':');
  if (separator == std::string::npos) {
    return {};
  }
  return std::string_view(uri).substr(0U, separator);
}

std::string_view SecretRef::location() const noexcept {
  const std::size_t separator = uri.find(':');
  if (separator == std::string::npos) {
    return {};
  }
  return std::string_view(uri).substr(separator + 1U);
}

Result<SecretString> ResolveSecret(const SecretRef& ref) {
  if (Error error = ValidateSecretUri(ref); error) {
    return error;
  }

#if defined(COGITO_TESTING)
  const auto mock = g_mock_secrets.find(ref.uri);
  if (mock != g_mock_secrets.end()) {
    return SecretString(std::string(mock->second.Expose()));
  }
#endif

  const std::string_view scheme = ref.scheme();
  const std::string_view location = ref.location();
  if (scheme == "env") {
    return ResolveEnvironmentSecret(location);
  }
  if (scheme == "file") {
    if (!IsAbsoluteSecretPath(location)) {
      return SecretError("secret file path must be absolute");
    }
    auto file = OpenValidatedSecretFile(std::string(location));
    if (!file) {
      return file.error();
    }
    return ReadValidatedSecretFile(file.value());
  }
  if (scheme == "wincred") {
    if (location.empty() || location.size() > kMaxBackendNameBytes ||
        ContainsNul(location) || !IsValidUtf8(location)) {
      return SecretError("Windows credential target is invalid");
    }
#if defined(_WIN32)
    return ResolveWindowsCredential(location);
#else
    return SecretError("Windows credential backend is unavailable");
#endif
  }
  return SecretError("keyring backend is unsupported in core v1");
}

Error CheckSecretFilePermissions(const std::string& path) {
  if (!IsAbsoluteSecretPath(path)) {
    return ForbiddenError("secret file path must be absolute");
  }
  auto file = OpenValidatedSecretFile(path);
  if (!file) {
    return ForbiddenError("secret file permission validation failed");
  }
  return Error::Ok();
}

#if defined(COGITO_TESTING)
namespace testing {

void SecretTestSeam::SetMockSecret(std::string_view uri,
                                   std::string_view secret_value) {
  g_mock_secrets.insert_or_assign(std::string(uri),
                                  SecretString(std::string(secret_value)));
}

void SecretTestSeam::ClearMockSecrets() { g_mock_secrets.clear(); }

}  // namespace testing
#endif

}  // namespace cogito
