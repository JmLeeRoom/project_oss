# [Claude Task] Cogito++ G0 자기모순 9건 정정안 및 헤더 계약 확정

> **수신자**: Claude (Anthropic Claude 3.5 Sonnet / Claude 3.7)  
> **역할**: Cogito++ 수석 계약 관리자(Chief Contract Architect) 및 감사관  
> **소유 영역**: `include/cogito/**`, `docs/**`, `config/*.schema.json`, `Cogito++_구현명세서.md`  
> **쓰기 금지**: C++ 구현체(`.cpp`) 및 빌드 스크립트 작성 금지 (Codex 소유)

---

## 1. 배경 및 미션

현재 `Cogito++_개발_작업체크리스트.md`의 G0 차단 결정 중, 명세서 내 **자기모순 9건**으로 인해 C++ 제품 코드(S1~) 착수가 차단되어 있습니다.  
당신의 임무는 이 9건의 충돌을 논리적으로 해소하여 **단일 기준(Single Source of Truth)**을 확정하고, 코어가 참조할 완전한 C++17 헤더 계약을 정의하는 것입니다.

---

## 2. 해결 대상 자기모순 9건 상세

다음 9개 항목에 대해 `[현상 모순 분석] -> [확정 결정안] -> [구현명세서 수정 Diff/코드]` 형식으로 작성하십시오.

### ① G0-01: C ABI 버전 표기 일관성
- **모순**: `include/cogito/cogito.h`의 `COGITO_ABI_VERSION_MAJOR`가 v1.0과 v1.1로 혼재.
- **요구사항**: ABI 버전 정책(Major/Minor 규칙)을 명시하고 단일 버전 매크로로 고정.

### ② G0-05: Operation Digest 및 인코딩 규격
- **모순**: `domain::kAction` 도메인 태그와 Length-Prefix(LP) 인코딩 규칙, CCJ 직렬화 간의 필드 순서 불일치.
- **요구사항**: `action_digest` 계산에 들어가는 필드 목록, 정수/문자열 인코딩 규격 확정.

### ③ G0-09: C++17 `Result<void>` 표현식 표준화
- **모순**: `Result<T>` 템플릿에서 반환값이 없는 성공(`void`)을 표현하는 표준 타입 미정의.
- **요구사항**: `std::monostate` 또는 `Result<void>` 특수화 구조체를 통한 타입 안전한 반환 규약 확정.

### ④ G0-23: CCJ 지수 표기 vs 골든 표 충돌
- **모순**: §3-1 CCJ-5에는 "지수 자릿수 선행 0 제거(예: `1.5e-7`)"로 기술되어 있으나, §3-1-a 골든 표에는 `1e-07`로 기재됨.
- **요구사항**: 두 규칙 중 단일 정답을 확정하고 골든 벡터 24개 표를 100% 일치하도록 정정.

### ⑤ G0-24: FSM Cancel 전이 집합 및 R1 규칙
- **모순**: FSM의 `Execute` 상태가 `Cancel` 이벤트를 수신하는지, 아니면 `ToolResultStatus::Cancelled`로 흡수되는지 정의 충돌.
- **요구사항**: 보편 규칙 R1/R2/R3와 FSM 전이표 간의 Cancel 처리 경로를 단일화.

### ⑥ G0-25: 누락된 핵심 헤더 계약 완전화
- **모순**: `invoker.hpp`, `ops_log.hpp`, `context_compactor.hpp`의 헤더 정의가 누락되어 Codex가 구현할 수 없음.
- **요구사항**: 누락된 3개 헤더의 완전한 C++17 인터페이스 및 구조체 선언문 작성.

### ⑦ G0-26: Digest Projection 및 도메인 분리 태그
- **모순**: ActionDigest, PermitDigest, AuditDigest 간 도메인 충돌 방지 태그 명세 누락.
- **요구사항**: `domain::` 네임스페이스 내 8대 도메인 태그와 해시 프로젝션 스키마 확정.

### ⑧ G0-29: ExecutionMode × Role 권한 상한 행렬
- **모순**: `ExecutionMode`(Default/Plan/Edit/ReadOnly)와 `Subject.roles` 간의 권한 축소(Downgrade) 우선순위 불명확.
- **요구사항**: 모드가 역할을 확장할 수 없고 상한만 낮춘다는 불변식을 만족하는 권한 판정 진리표 확정.

### ⑨ G0-31: 승인 재진입 상한 및 Deny 강등 규칙
- **모순**: 동일 Action에 대한 Gate 재진입 시 카운터 증가 및 2회째 `Ask` 발생 시 `Deny` 강등 시점 미확정.
- **요구사항**: 재진입 카운터 상한(최대 1회) 및 원자적 커밋 규약 명문화.

---

## 3. 필수 산출물

1. **G0 9건 정정 보고서** (체크리스트 G0-01~G0-31 대응)
2. **ADR 0001 (Deterministic Execution Gateway)** 초안
3. **ADR 0004 (CCJ v1 & Golden Vectors)** 초안
4. 완전한 헤더 파일 초안:
   - `include/cogito/result.hpp`
   - `include/cogito/ids.hpp`
   - `include/cogito/fsm.hpp`
   - `include/cogito/invoker.hpp`

---

## 4. 완료 기준 (Exit Gate)

- 9건 모두 상호 모순 없이 논리적으로 완결되어 사람이 즉시 승인(Approve)할 수 있는 상태여야 함.
