// SPDX-License-Identifier: Apache-2.0
// Cogito++ — LLM 추론 어댑터 및 모델 상호작용 인터페이스 (InferenceAdapter)
//
// 규범 근거 : Cogito++_구현명세서.md §4-12(:1059-1108), §7-5, 체크리스트 S6-02
// G0 결정   : G0-13 (Provider identity), G0-25 (InferenceRequest/Response)
//
// [불변 추론 원칙]
// 1. 단일 Action 강제: force_single_action=true 플래그를 통해 다중 도구 호출을 거부한다.
// 2. 민감정보 보호: LLM 원문 응답은 기본 저장하지 않고 raw_digest 만 감사한다 (§7-5).
// 3. 완료 사유 검증: FinishReason::Length 또는 FinishReason::Error 에서 불완전 Action 은 실행하지 않는다.
#ifndef COGITO_INFERENCE_HPP
#define COGITO_INFERENCE_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cogito/action.hpp"
#include "cogito/conversation.hpp"
#include "cogito/digest.hpp"
#include "cogito/result.hpp"
#include "cogito/tool.hpp"

namespace cogito {

// ─────────────────────────────────────────────────────────────────────────────
// Usage — 토큰 소비량
// ─────────────────────────────────────────────────────────────────────────────
struct Usage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens() const noexcept { return prompt_tokens + completion_tokens; }
};

// ─────────────────────────────────────────────────────────────────────────────
// FinishReason — 추론 종료 원인
// ─────────────────────────────────────────────────────────────────────────────
enum class FinishReason : std::uint8_t {
  Stop      = 1,   // 정상 종료 (텍스트 생성 완료)
  ToolCalls = 2,   // 도구 호출 요청
  Length    = 3,   // 토큰 한도 도달로 인한 절단
  Error     = 4,   // 프로바이더 또는 네트워크 오류
  Cancelled = 5,   // 사용자 취소
};

inline constexpr std::string_view ToString(FinishReason reason) noexcept {
  switch (reason) {
    case FinishReason::Stop:      return "stop";
    case FinishReason::ToolCalls: return "tool_calls";
    case FinishReason::Length:    return "length";
    case FinishReason::Error:     return "error";
    case FinishReason::Cancelled: return "cancelled";
  }
  return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// CancelToken — 비동기 작업 취소 토큰
// ─────────────────────────────────────────────────────────────────────────────
struct CancelToken {
  const std::atomic<bool>* flag = nullptr;

  bool IsCancelled() const noexcept {
    return flag != nullptr && flag->load(std::memory_order_relaxed);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// ProviderIdentity — 모델 및 프로바이더 정체성 메타데이터 (G0-13)
// ─────────────────────────────────────────────────────────────────────────────
struct ProviderIdentity {
  std::string provider_id;          // "fake-provider" | "openai-http" | "llamacpp"
  std::string model_id;             // 모델 식별자 (예: "qwen3-8b-instruct")
  Digest      model_digest{};       // 모델 가중치 SHA-256
  Digest      chat_template_digest{};
  Digest      tool_schema_digest{};
  std::string provider_build;       // 빌드/버전 정보
};

// ─────────────────────────────────────────────────────────────────────────────
// InferenceRequest — 모델 추론 요청 입력
// ─────────────────────────────────────────────────────────────────────────────
struct InferenceRequest {
  const std::vector<Message>*        messages = nullptr; // 대화 메시지 시퀀스
  const std::vector<ToolDescriptor>* tools = nullptr;    // 사용 가능 도구 목록 (이름순)
  int          max_tokens = 1024;                        // 최대 출력 토큰 수
  float        temperature = 0.0f;                       // 생성 온도 (결정론=0.0)
  std::uint64_t seed = 0;                                // 난수 시드
  bool         force_single_action = true;               // 단일 Action 강제 플래그
  bool         constrain_to_schema = true;               // GBNF/JSON 스키마 제약
  std::int64_t deadline_ns = 0;                          // 단조 데드라인 시각
  CancelToken  cancel{};                                 // 취소 토큰
};

// ─────────────────────────────────────────────────────────────────────────────
// InferenceResponse — 모델 추론 결과
// ─────────────────────────────────────────────────────────────────────────────
struct InferenceResponse {
  std::string                text;                       // 어시스턴트 텍스트 응답
  std::vector<ActionRequest> actions;                    // 제안된 Action (정상 시 0 또는 1개)
  Usage                      usage;                      // 토큰 사용량
  FinishReason               finish = FinishReason::Error;
  std::string                response_id;                // 응답 고유 ID
  Digest                     raw_digest{};               // 원문 SHA-256 다이제스트 (§7-5)
  ProviderIdentity           identity;                   // 응답 제공자 정보
};

// ─────────────────────────────────────────────────────────────────────────────
// InferenceAdapter — 추론 프로바이더 추상 인터페이스
// ─────────────────────────────────────────────────────────────────────────────
class InferenceAdapter {
 public:
  virtual ~InferenceAdapter() = default;

  // 추론 완료 수행
  virtual Result<InferenceResponse> Complete(const InferenceRequest& req) = 0;

  // 텍스트 토큰 수 추정
  virtual int EstimateTokens(const std::string& text) const = 0;

  // 프로바이더 정체성 조회
  virtual const ProviderIdentity& identity() const noexcept = 0;

  virtual bool supports_grammar_constraint() const noexcept { return false; }
  virtual bool supports_single_action_enforcement() const noexcept { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// FakeProvider — 단위 및 회귀 테스트용 스크립트 기반 가짜 프로바이더 (S6-02)
// ─────────────────────────────────────────────────────────────────────────────
class FakeProvider : public InferenceAdapter {
 public:
  FakeProvider(ProviderIdentity identity = ProviderIdentity{
                   "fake-provider", "fake-model-v1", Digest::Zero(),
                   Digest::Zero(), Digest::Zero(), "fake-build-1.0"})
      : identity_(std::move(identity)) {}

  // 큐에 사전 준비된 응답 추가
  void PushResponse(InferenceResponse response) {
    responses_.push_back(std::move(response));
  }

  // 큐에 사전 준비된 에러 추가
  void PushError(Error error) {
    errors_.push_back(std::move(error));
  }

  Result<InferenceResponse> Complete(const InferenceRequest& req) override;

  int EstimateTokens(const std::string& text) const override {
    return static_cast<int>((text.size() + 3) / 4);
  }

  const ProviderIdentity& identity() const noexcept override {
    return identity_;
  }

  std::size_t call_count() const noexcept { return call_count_; }
  void Reset() noexcept {
    responses_.clear();
    errors_.clear();
    call_count_ = 0;
  }

 private:
  ProviderIdentity                identity_;
  std::vector<InferenceResponse>  responses_;
  std::vector<Error>              errors_;
  std::size_t                     call_count_ = 0;
};

}  // namespace cogito

#endif  // COGITO_INFERENCE_HPP