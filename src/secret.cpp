// SPDX-License-Identifier: Apache-2.0

#include "cogito/config.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/crypto.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <accctrl.h>
#include <aclapi.h>
#include <wincred.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cogito {
namespace {

constexpr std::size_t kMaxSecretUriBytes = 1024U;
constexpr std::size_t kMaxSecretLocationBytes = 1024U;
constexpr std::size_t kMaxEnvironmentNameBytes = 256U;
#if defined(_WIN32)
constexpr std::size_t kMaxWincredTargetBytes = 256U;
#endif
constexpr std::size_t kMaxSecretBytes = 65536U;
constexpr std::size_t kReadChunkBytes = 4096U;

Error SecretError(const char* message) {
  return Error{Errc::SecretError, {}, message};
}

Error ForbiddenError(const char* message) {
  return Error{Errc::Forbidden, {}, message};
}

Error TooLargeError(const char* message) {
  return Error{Errc::TooLarge, reason::kInputTooLarge, message};
}

bool IsAsciiAlpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(char value) noexcept { return value >= '0' && value <= '9'; }

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

bool IsValidEnvironmentName(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxEnvironmentNameBytes ||
      (!IsAsciiAlpha(name.front()) && name.front() != '_')) {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char value) {
    return IsAsciiAlpha(value) || IsAsciiDigit(value) || value == '_';
  });
}

#if defined(_WIN32)
bool IsValidUtf8(std::string_view text) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t offset = 0U;
  while (offset < text.size()) {
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
    if (continuation_count > text.size() - offset - 1U) {
      return false;
    }
    for (std::size_t i = 1U; i <= continuation_count; ++i) {
      const unsigned char continuation = bytes[offset + i];
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
#endif

class SensitiveBuffer {
 public:
  SensitiveBuffer() { bytes_.reserve(kMaxSecretBytes + 1U); }
  ~SensitiveBuffer() { Cleanse(); }

  SensitiveBuffer(const SensitiveBuffer&) = delete;
  SensitiveBuffer& operator=(const SensitiveBuffer&) = delete;

  std::string& bytes() noexcept { return bytes_; }

  SecretString IntoSecret() { return SecretString{std::move(bytes_)}; }

 private:
  void Cleanse() noexcept {
    if (!bytes_.empty()) {
      OPENSSL_cleanse(bytes_.data(), bytes_.size());
      bytes_.clear();
    }
  }

  std::string bytes_;
};

class SensitiveChunk {
 public:
  ~SensitiveChunk() { OPENSSL_cleanse(bytes.data(), bytes.size()); }

  SensitiveChunk(const SensitiveChunk&) = delete;
  SensitiveChunk& operator=(const SensitiveChunk&) = delete;
  SensitiveChunk() = default;

  std::array<char, kReadChunkBytes> bytes{};
};

Error NormalizeFileSecret(std::string& secret) noexcept {
  if (secret.empty()) {
    return SecretError("the secret file is empty");
  }

  std::size_t removed = 0U;
  if (secret.size() >= 2U && secret[secret.size() - 2U] == '\r' &&
      secret.back() == '\n') {
    removed = 2U;
  } else if (secret.back() == '\n') {
    removed = 1U;
  }
  if (removed != 0U) {
    const std::size_t new_size = secret.size() - removed;
    OPENSSL_cleanse(secret.data() + new_size, removed);
    secret.resize(new_size);
  }
  if (secret.empty()) {
    return SecretError("the normalized secret file is empty");
  }
  return Error::Ok();
}

#if defined(COGITO_TESTING)
thread_local std::map<std::string, SecretString> g_mock_secrets;
#endif

#if defined(_WIN32)

bool IsAbsoluteSecretPath(std::string_view path) noexcept {
  if (path.empty() || path.size() > kMaxSecretLocationBytes ||
      path.find('\0') != std::string_view::npos) {
    return false;
  }
  const bool drive_absolute =
      path.size() >= 3U && IsAsciiAlpha(path[0]) && path[1] == ':' && path[2] == '\\';
  if (drive_absolute) {
    return true;
  }
  if (path.size() < 5U || path[0] != '\\' || path[1] != '\\' || path[2] == '.' ||
      path[2] == '?') {
    return false;
  }
  const std::size_t server_end = path.find('\\', 2U);
  return server_end != std::string_view::npos && server_end > 2U &&
         server_end + 1U < path.size();
}

Result<std::wstring> Utf8ToWide(std::string_view text) {
  if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      !IsValidUtf8(text)) {
    return SecretError("a Windows secret identifier is not valid UTF-8");
  }
  const int input_size = static_cast<int>(text.size());
  const int required =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
  if (required <= 0) {
    return SecretError("a Windows secret identifier could not be converted");
  }
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size,
                          result.data(), required) != required) {
    return SecretError("a Windows secret identifier could not be converted");
  }
  return result;
}

std::wstring ToExtendedFilePath(std::wstring path) {
  if (path.size() < static_cast<std::size_t>(MAX_PATH)) {
    return path;
  }
  if (path.size() >= 2U && path[0] == L'\\' && path[1] == L'\\') {
    return L"\\\\?\\UNC\\" + path.substr(2U);
  }
  return L"\\\\?\\" + path;
}

class UniqueHandle {
 public:
  explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
  ~UniqueHandle() {
    if (valid()) {
      CloseHandle(handle_);
    }
  }

  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;

  UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
      if (valid()) {
        CloseHandle(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
  }

  bool valid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }
  HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_;
};

class LocalSecurityDescriptor {
 public:
  ~LocalSecurityDescriptor() {
    if (descriptor_ != nullptr) {
      LocalFree(descriptor_);
    }
  }

  LocalSecurityDescriptor(const LocalSecurityDescriptor&) = delete;
  LocalSecurityDescriptor& operator=(const LocalSecurityDescriptor&) = delete;
  LocalSecurityDescriptor() = default;

  PSECURITY_DESCRIPTOR* out() noexcept { return &descriptor_; }

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};

bool IsWhitelistedSid(PSID sid, PSID current_user, PSID system_sid,
                      PSID administrators_sid) noexcept {
  return EqualSid(sid, current_user) != FALSE || EqualSid(sid, system_sid) != FALSE ||
         EqualSid(sid, administrators_sid) != FALSE;
}

struct AllowedAceView {
  bool is_allowed = false;
  ACCESS_MASK mask = 0U;
  PSID sid = nullptr;
};

bool ParseAllowedAce(ACE_HEADER* header, AllowedAceView& result) noexcept {
  result = AllowedAceView{};
  std::size_t sid_offset = 0U;
  switch (header->AceType) {
    case ACCESS_ALLOWED_ACE_TYPE:
      result.is_allowed = true;
      sid_offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
      break;
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
      result.is_allowed = true;
      sid_offset = offsetof(ACCESS_ALLOWED_CALLBACK_ACE, SidStart);
      break;
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE: {
      result.is_allowed = true;
      constexpr std::size_t kObjectFlagsOffset = sizeof(ACE_HEADER) + sizeof(ACCESS_MASK);
      if (header->AceSize < kObjectFlagsOffset + sizeof(DWORD)) {
        return false;
      }
      DWORD flags = 0U;
      std::memcpy(&flags, reinterpret_cast<const unsigned char*>(header) + kObjectFlagsOffset,
                  sizeof(flags));
      sid_offset = kObjectFlagsOffset + sizeof(DWORD);
      if ((flags & ACE_OBJECT_TYPE_PRESENT) != 0U) {
        sid_offset += sizeof(GUID);
      }
      if ((flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) != 0U) {
        sid_offset += sizeof(GUID);
      }
      break;
    }
    default:
      return true;
  }

  constexpr std::size_t kMaskOffset = sizeof(ACE_HEADER);
  constexpr std::size_t kMinimumSidBytes = offsetof(SID, SubAuthority);
  if (header->AceSize < kMaskOffset + sizeof(ACCESS_MASK) ||
      header->AceSize < sid_offset + kMinimumSidBytes) {
    return false;
  }
  std::memcpy(&result.mask, reinterpret_cast<const unsigned char*>(header) + kMaskOffset,
              sizeof(result.mask));
  result.sid = reinterpret_cast<unsigned char*>(header) + sid_offset;
  if (IsValidSid(result.sid) == FALSE) {
    return false;
  }
  const DWORD sid_length = GetLengthSid(result.sid);
  return sid_length <= static_cast<DWORD>(header->AceSize - sid_offset);
}

Error ValidateWindowsDacl(HANDLE file) {
  UniqueHandle token;
  HANDLE raw_token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
    return ForbiddenError("the current Windows security identity could not be read");
  }
  token = UniqueHandle(raw_token);

  DWORD token_bytes = 0U;
  static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &token_bytes));
  if (token_bytes == 0U || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return ForbiddenError("the current Windows security identity could not be read");
  }
  const std::size_t token_units =
      (static_cast<std::size_t>(token_bytes) + sizeof(std::max_align_t) - 1U) /
      sizeof(std::max_align_t);
  std::vector<std::max_align_t> token_buffer(token_units);
  if (GetTokenInformation(token.get(), TokenUser, token_buffer.data(), token_bytes,
                          &token_bytes) == FALSE) {
    return ForbiddenError("the current Windows security identity could not be read");
  }
  const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());
  PSID current_user = token_user->User.Sid;
  if (IsValidSid(current_user) == FALSE) {
    return ForbiddenError("the current Windows security identity is invalid");
  }

  alignas(SID) std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_storage{};
  alignas(SID) std::array<unsigned char, SECURITY_MAX_SID_SIZE>
      administrators_storage{};
  DWORD system_size = static_cast<DWORD>(system_storage.size());
  DWORD administrators_size = static_cast<DWORD>(administrators_storage.size());
  PSID system_sid = system_storage.data();
  PSID administrators_sid = administrators_storage.data();
  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid, &system_size) == FALSE ||
      CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators_sid,
                         &administrators_size) == FALSE) {
    return ForbiddenError("the Windows security whitelist could not be created");
  }

  PSID owner = nullptr;
  PACL dacl = nullptr;
  LocalSecurityDescriptor security_descriptor;
  const DWORD status = GetSecurityInfo(file, SE_FILE_OBJECT,
                                       OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                       &owner, nullptr, &dacl, nullptr,
                                       security_descriptor.out());
  if (status != ERROR_SUCCESS || owner == nullptr || dacl == nullptr ||
      IsValidSid(owner) == FALSE ||
      !IsWhitelistedSid(owner, current_user, system_sid, administrators_sid)) {
    return ForbiddenError("the secret file owner or DACL is not allowed");
  }

  ACL_SIZE_INFORMATION acl_information{};
  if (GetAclInformation(dacl, &acl_information, sizeof(acl_information),
                        AclSizeInformation) == FALSE) {
    return ForbiddenError("the secret file DACL could not be inspected");
  }
  constexpr ACCESS_MASK kReadGrantMask = FILE_READ_DATA | GENERIC_READ | GENERIC_ALL;
  for (DWORD index = 0U; index < acl_information.AceCount; ++index) {
    void* raw_ace = nullptr;
    if (GetAce(dacl, index, &raw_ace) == FALSE || raw_ace == nullptr) {
      return ForbiddenError("the secret file DACL contains an invalid ACE");
    }
    AllowedAceView ace;
    if (!ParseAllowedAce(static_cast<ACE_HEADER*>(raw_ace), ace)) {
      return ForbiddenError("the secret file DACL contains an invalid allow ACE");
    }
    if (ace.is_allowed && (ace.mask & kReadGrantMask) != 0U &&
        !IsWhitelistedSid(ace.sid, current_user, system_sid, administrators_sid)) {
      return ForbiddenError("the secret file grants read access to an untrusted trustee");
    }
  }
  return Error::Ok();
}

Result<UniqueHandle> OpenValidatedSecretFile(std::string_view path) {
  auto converted = Utf8ToWide(path);
  if (!converted) {
    return converted.error();
  }
  const std::wstring wide_path = ToExtendedFilePath(std::move(converted).take());
  UniqueHandle file(CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid()) {
    const DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
        error == ERROR_CANT_ACCESS_FILE) {
      return ForbiddenError("the secret file cannot be opened safely");
    }
    return SecretError("the secret file could not be opened");
  }

  if (GetFileType(file.get()) != FILE_TYPE_DISK) {
    return ForbiddenError("the secret path is not a disk file");
  }
  BY_HANDLE_FILE_INFORMATION information{};
  if (GetFileInformationByHandle(file.get(), &information) == FALSE) {
    return SecretError("the secret file metadata could not be read");
  }
  if ((information.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
    return ForbiddenError("the secret path is not a regular non-reparse file");
  }
  const Error dacl_error = ValidateWindowsDacl(file.get());
  if (dacl_error) {
    return dacl_error;
  }
  return file;
}

Result<SecretString> ReadValidatedSecretFile(UniqueHandle file) {
  LARGE_INTEGER file_size{};
  if (GetFileSizeEx(file.get(), &file_size) == FALSE || file_size.QuadPart < 0) {
    return SecretError("the secret file size could not be read");
  }
  if (file_size.QuadPart == 0) {
    return SecretError("the secret file is empty");
  }
  if (static_cast<unsigned long long>(file_size.QuadPart) > kMaxSecretBytes) {
    return TooLargeError("the secret file exceeds 65536 bytes");
  }

  SensitiveBuffer secret;
  SensitiveChunk chunk;
  for (;;) {
    const std::size_t remaining = kMaxSecretBytes + 1U - secret.bytes().size();
    const DWORD requested = static_cast<DWORD>(std::min(remaining, chunk.bytes.size()));
    DWORD bytes_read = 0U;
    if (ReadFile(file.get(), chunk.bytes.data(), requested, &bytes_read, nullptr) == FALSE) {
      return SecretError("the secret file could not be read");
    }
    if (bytes_read == 0U) {
      break;
    }
    secret.bytes().append(chunk.bytes.data(), static_cast<std::size_t>(bytes_read));
    if (secret.bytes().size() > kMaxSecretBytes) {
      return TooLargeError("the secret file exceeds 65536 bytes");
    }
  }
  const Error normalization = NormalizeFileSecret(secret.bytes());
  if (normalization) {
    return normalization;
  }
  return secret.IntoSecret();
}

class CredentialGuard {
 public:
  explicit CredentialGuard(PCREDENTIALW credential) noexcept : credential_(credential) {}
  ~CredentialGuard() {
    if (credential_ != nullptr) {
      if (credential_->CredentialBlob != nullptr && credential_->CredentialBlobSize != 0U) {
        OPENSSL_cleanse(credential_->CredentialBlob, credential_->CredentialBlobSize);
      }
      CredFree(credential_);
    }
  }

  CredentialGuard(const CredentialGuard&) = delete;
  CredentialGuard& operator=(const CredentialGuard&) = delete;

 private:
  PCREDENTIALW credential_;
};

Result<SecretString> ResolveWincred(std::string_view target) {
  if (target.empty() || target.size() > kMaxWincredTargetBytes || !IsValidUtf8(target)) {
    return SecretError("the Windows credential target is invalid");
  }
  auto converted = Utf8ToWide(target);
  if (!converted) {
    return converted.error();
  }

  PCREDENTIALW credential = nullptr;
  if (CredReadW(converted.value().c_str(), CRED_TYPE_GENERIC, 0U, &credential) == FALSE ||
      credential == nullptr) {
    return SecretError("the Windows credential could not be resolved");
  }
  CredentialGuard guard(credential);
  const std::size_t size = static_cast<std::size_t>(credential->CredentialBlobSize);
  if (size == 0U || credential->CredentialBlob == nullptr) {
    return SecretError("the Windows credential is empty");
  }
  if (size > kMaxSecretBytes) {
    return TooLargeError("the Windows credential exceeds 65536 bytes");
  }
  const std::string_view value(
      reinterpret_cast<const char*>(credential->CredentialBlob), size);
  if (!IsValidUtf8(value)) {
    return SecretError("the Windows credential is not valid UTF-8");
  }
  return SecretString{std::string(value)};
}

#else

bool IsAbsoluteSecretPath(std::string_view path) noexcept {
  return !path.empty() && path.size() <= kMaxSecretLocationBytes && path.front() == '/' &&
         path.find('\0') == std::string_view::npos;
}

class UniqueFd {
 public:
  explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}
  ~UniqueFd() {
    if (fd_ >= 0) {
      static_cast<void>(close(fd_));
    }
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        static_cast<void>(close(fd_));
      }
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const noexcept { return fd_; }

 private:
  int fd_;
};

Result<UniqueFd> OpenValidatedSecretFile(std::string_view path) {
#if !defined(O_NOFOLLOW) || !defined(O_CLOEXEC)
  static_cast<void>(path);
  return ForbiddenError("the platform lacks secure secret-file open flags");
#else
  const int fd = open(std::string(path).c_str(),
                      O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) {
    if (errno == ELOOP || errno == EACCES || errno == EPERM) {
      return ForbiddenError("the secret file cannot be opened safely");
    }
    return SecretError("the secret file could not be opened");
  }
  UniqueFd file(fd);

  struct stat status {};
  if (fstat(file.get(), &status) != 0) {
    return SecretError("the secret file metadata could not be read");
  }
  if (!S_ISREG(status.st_mode)) {
    return ForbiddenError("the secret path is not a regular file");
  }
  if (status.st_uid != getuid() && status.st_uid != 0U) {
    return ForbiddenError("the secret file owner is not allowed");
  }
  const mode_t permissions = status.st_mode &
                             static_cast<mode_t>(S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID |
                                                 S_ISGID | S_ISVTX);
  if (permissions != static_cast<mode_t>(S_IRUSR | S_IWUSR) &&
      permissions != static_cast<mode_t>(S_IRUSR)) {
    return ForbiddenError("the secret file permissions are not 0600 or 0400");
  }
  return file;
#endif
}

Result<SecretString> ReadValidatedSecretFile(UniqueFd file) {
  struct stat status {};
  if (fstat(file.get(), &status) != 0 || status.st_size < 0) {
    return SecretError("the secret file size could not be read");
  }
  if (status.st_size == 0) {
    return SecretError("the secret file is empty");
  }
  if (static_cast<std::uintmax_t>(status.st_size) > kMaxSecretBytes) {
    return TooLargeError("the secret file exceeds 65536 bytes");
  }

  SensitiveBuffer secret;
  SensitiveChunk chunk;
  for (;;) {
    const std::size_t remaining = kMaxSecretBytes + 1U - secret.bytes().size();
    const std::size_t requested = std::min(remaining, chunk.bytes.size());
    const ssize_t bytes_read = read(file.get(), chunk.bytes.data(), requested);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return SecretError("the secret file could not be read");
    }
    if (bytes_read == 0) {
      break;
    }
    secret.bytes().append(chunk.bytes.data(), static_cast<std::size_t>(bytes_read));
    if (secret.bytes().size() > kMaxSecretBytes) {
      return TooLargeError("the secret file exceeds 65536 bytes");
    }
  }
  const Error normalization = NormalizeFileSecret(secret.bytes());
  if (normalization) {
    return normalization;
  }
  return secret.IntoSecret();
}

#endif

Result<SecretString> ResolveEnvironment(std::string_view name) {
  if (!IsValidEnvironmentName(name)) {
    return SecretError("the environment variable name is invalid");
  }
  const std::string name_string(name);
  const char* const value = std::getenv(name_string.c_str());
  if (value == nullptr || value[0] == '\0') {
    return SecretError("the environment secret is not set");
  }
  std::size_t size = 0U;
  while (size <= kMaxSecretBytes && value[size] != '\0') {
    ++size;
  }
  if (size > kMaxSecretBytes) {
    return TooLargeError("the environment secret exceeds 65536 bytes");
  }
  return SecretString{std::string(value, size)};
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
  try {
    if (!IsValidSecretUri(ref.uri)) {
      return SecretError("the secret reference URI is invalid");
    }

#if defined(COGITO_TESTING)
    const auto mock = g_mock_secrets.find(ref.uri);
    if (mock != g_mock_secrets.end()) {
      return SecretString{std::string(mock->second.Expose())};
    }
#endif

    const std::string_view scheme = ref.scheme();
    const std::string_view location = ref.location();
    if (scheme == "env") {
      return ResolveEnvironment(location);
    }
    if (scheme == "file") {
      if (!IsAbsoluteSecretPath(location)) {
        return SecretError("the secret file path is not an allowed absolute path");
      }
      auto opened = OpenValidatedSecretFile(location);
      if (!opened) {
        return opened.error();
      }
      return ReadValidatedSecretFile(std::move(opened).take());
    }
    if (scheme == "wincred") {
#if defined(_WIN32)
      return ResolveWincred(location);
#else
      static_cast<void>(location);
      return SecretError("Windows Credential Manager is unavailable on this platform");
#endif
    }
    return SecretError("the keyring secret backend is unsupported in core v1");
  } catch (const std::bad_alloc&) {
    return SecretError("out of memory while resolving a secret");
  } catch (...) {
    return SecretError("unexpected secret resolution failure");
  }
}

Error CheckSecretFilePermissions(const std::string& path) {
  try {
    if (!IsAbsoluteSecretPath(path)) {
      return ForbiddenError("the secret file path is not an allowed absolute path");
    }
    auto opened = OpenValidatedSecretFile(path);
    if (!opened) {
      return ForbiddenError("the secret file failed the permission check");
    }
    return Error::Ok();
  } catch (...) {
    return ForbiddenError("the secret file permission check failed");
  }
}

#if defined(COGITO_TESTING)
namespace testing {

void SecretTestSeam::SetMockSecret(std::string_view uri, std::string_view secret_value) {
  SecretString replacement{std::string(secret_value)};
  const auto existing = g_mock_secrets.find(std::string(uri));
  if (existing == g_mock_secrets.end()) {
    g_mock_secrets.emplace(std::string(uri), std::move(replacement));
  } else {
    existing->second = std::move(replacement);
  }
}

void SecretTestSeam::ClearMockSecrets() {
  for (auto& entry : g_mock_secrets) {
    entry.second.Clear();
  }
  g_mock_secrets.clear();
}

}  // namespace testing
#endif

}  // namespace cogito
