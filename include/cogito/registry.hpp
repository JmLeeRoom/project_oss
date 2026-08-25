// SPDX-License-Identifier: Apache-2.0
// Cogito++ — 도구 레지스트리 (ToolRegistry, ToolProvider, LookupResult)
//
// 규범 근거 : Cogito++_구현명세서.md §4-5(:596-659), 체크리스트 S2-05, S2-06
// G0 결정   : G0-26 (Accepted), G0-27 (Accepted), G0-29 (Accepted)
#ifndef COGITO_REGISTRY_HPP
#define COGITO_REGISTRY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cogito/canonical_json.hpp"
#include "cogito/digest.hpp"
#include "cogito/identity.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"
#include "cogito/tool_schema.hpp"

namespace cogito {

enum class LookupKind : std::uint8_t { Absent, Enabled, Forbidden };

const char* ToString(LookupKind k) noexcept;

struct LookupResult {
  LookupKind             kind = LookupKind::Absent;
  const ToolDescriptor*  desc = nullptr;   // Enabled 또는 Forbidden 일 때 non-null
};

class ToolProvider {
 public:
  virtual ~ToolProvider() = default;
  virtual const char* provider_id() const noexcept = 0;
  virtual Result<std::vector<ToolDescriptor>> Describe() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ToolRegistry — 도구 등록, 컴파일 동결, 모델 노출, 스키마 유효성 검증
//
// [생명주기 및 불변식]
// - Pre-Freeze (부팅 단계):
//     * Register(ToolDescriptor): 도구 추가. 중복 이름 -> Errc::DuplicateKey, 계약 위반 -> Errc::ToolContractViolation.
//     * RegisterFrom(ToolProvider&): 공급자 도구 일괄 등록. provider_id 검증(비어있으면 자동 설정, 기입되어 있으면 일치 강제).
//       배치 내 중복 또는 계약 위반 발견 시 전체 롤백(상태 불변).
//     * Freeze(): 모든 input/output schema 컴파일, grammar_coverage 산출, registry_digest 산출을
//       완전히 스테이징(staging)한 뒤 한 번에 원자적으로 커밋.
//       컴파일/검증 실패 시 Errc::SchemaCompileFailed 반환 및 전체 롤백(frozen=false 및 기존 tools_ 보존).
//     * ExportForModel(mode): Pre-Freeze 상태에서는 빈 벡터 반환.
// - Post-Freeze (운영 단계):
//     * Register / RegisterFrom 호출 금지 -> Errc::TurnSealed 반환 (상태 불변).
//     * Lookup / FindInputSchema / FindOutputSchema / ValidateArguments / ExportForModel 정상 호출 가능.
//     * ValidateArguments: Gate 3단계 유효성 검증 (Absent -> NotRegistered, Forbidden -> Forbidden,
//       위반 -> SchemaViolation, 예산소진 -> PatternBudgetExhausted).
// - 포인터 수명 및 무효화 규칙:
//     * LookupResult::desc 및 FindInput/OutputSchema 반환 포인터는 ToolRegistry 인스턴스가 유효하고
//       수정되지 않는 동안 유효함 (Freeze 이후 레지스트리는 불변이므로 인스턴스 소멸 전까지 영구 유효).
//     * ToolRegistry 가 이동(move)되거나 소멸되면 기존 반환 포인터는 모두 무효화됨.
// ─────────────────────────────────────────────────────────────────────────────
class ToolRegistry {
 public:
  ToolRegistry();
  ~ToolRegistry();

  ToolRegistry(const ToolRegistry&) = delete;
  ToolRegistry& operator=(const ToolRegistry&) = delete;
  ToolRegistry(ToolRegistry&&) noexcept;
  ToolRegistry& operator=(ToolRegistry&&) noexcept;

  // 부팅 단계 전용 등록 API. Freeze() 이후 호출 시 Errc::TurnSealed 반환.
  [[nodiscard]] Error Register(ToolDescriptor d);
  [[nodiscard]] Error RegisterFrom(ToolProvider& p);

  // 스키마 컴파일 + 계약 검사 + registry_digest 계산 (1회 동결). 실패 시 프로세스 시작 실패.
  [[nodiscard]] Error Freeze();
  bool frozen() const noexcept { return frozen_; }

  // 도구 조회: Absent, Enabled, Forbidden(Tombstone) 구분
  LookupResult Lookup(const std::string& name) const noexcept;

  // 컴파일된 입력/출력 스키마 접근자 (ToolInvoker 및 Gate 전용)
  const CompiledSchema* FindInputSchema(const std::string& tool) const noexcept;
  const CompiledSchema* FindOutputSchema(const std::string& tool) const noexcept;

  // 게이트 3단계: 인자 유효성 검증 (Freeze 이후에만 호출 가능, 미동결 시 Errc::Internal)
  [[nodiscard]] Error ValidateArguments(const std::string& tool,
                                        const ccj::Json& args) const;

  // 모델 노출용 스키마 목록 반환 (비공개 handler 제외, name 오름차순 정렬, 모드별 effect 필터링)
  std::vector<ModelToolDeclaration> ExportForModel(ExecutionMode mode) const;

  const Digest& registry_digest() const noexcept { return digest_; }
  const char*   export_order_version() const noexcept { return "name-asc-v1"; }

  std::size_t size() const noexcept { return tools_.size(); }
  bool empty() const noexcept { return tools_.empty(); }

 private:
  std::map<std::string, ToolDescriptor> tools_;   // std::map = 이름 오름차순 보장
  std::map<std::string, std::unique_ptr<CompiledSchema>> input_schemas_;
  std::map<std::string, std::unique_ptr<CompiledSchema>> output_schemas_;
  Digest digest_{};
  bool   frozen_ = false;
};

}  // namespace cogito

#endif  // COGITO_REGISTRY_HPP
