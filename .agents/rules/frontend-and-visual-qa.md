# Gemini 작업 규약: Prompt, Contract & Product Review

> **역할**: 구현 프롬프트 작성, 아키텍처·API·공개 헤더 계약 관리, 보안·HMI 수용 기준 정의.
> **실행 분담**: Gemini가 계약과 검증 기준을 제공하고, Codex가 프론트엔드를 포함한 구현과 모든 테스트를 수행한다.

## 1. 디렉터리 소유권

Gemini가 작성하는 영역:

- `include/**`: C/C++ 공개 헤더 계약
- `docs/**`: 명세, 요구사항, ADR, G0, 작업 프롬프트
- `config/**`: 정책·보안·설정 스키마
- `.agents/**`: Gemini 규칙과 프롬프트 자산
- 단, `.agents/skills/cogito-stage-owner/**`는 Codex 소유다.

Codex가 작성하는 영역:

- `src/**`, `tests/**`
- `tools/**`: dashboard, mock server, CLI, web host 포함
- `cmake/**`, `bindings/**`, `scripts/**`
- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg*.json`
- `.codex/**`, `AGENTS.md`

계약과 구현이 충돌하면 handoff 문서를 만들지 않는다. Gemini는 계약 또는 프롬프트를 직접 정정하고, Codex는 영향받은 구현과 테스트를 갱신한다.

## 2. Web 보안 수용 기준

- CSP는 `default-src 'none'`, `script-src 'self'`, `style-src 'self'`를 유지한다.
- `style-src 'unsafe-inline'`, 외부 CDN, 인바운드 WebSocket을 허용하지 않는다.
- 이벤트 스트림은 `GET /api/events` SSE를 사용한다.
- 서버 유래 텍스트는 `textContent`로 렌더링하며 `innerHTML` 조립을 금지한다.
- 미승인·위조·만료·거부 경로는 write 호출 0회를 증명해야 한다.

## 3. HMI 수용 기준

- 1024x768에서 Authority Area의 핵심 필드와 승인/거부 동작이 세로 스크롤 없이 보여야 한다.
- 모든 터치 타깃은 최소 56x56px, 승인/거부 버튼 간격은 최소 24px이다.
- 승인은 2단계 확인을 사용한다.
- 모델·RAG 유래 텍스트는 신뢰할 수 없는 외부 데이터로 표시한다.
- 공장 현지시각과 타임존, 안전 계통 비대체 고지를 상시 표시한다.

## 4. Prompt 작성 기준

Gemini의 Codex 작업 프롬프트에는 다음을 포함한다.

- 목표와 비범위
- 권위 있는 명세·ADR·헤더 경로
- 수정 가능한 구현 경로
- 필수 정상·경계·거부 테스트
- 적용 가능한 sanitizer·브라우저·보안 검증
- 완료 시 보고해야 할 명령, 종료코드, 테스트 수, 잔여 위험

구현 세부를 근거 없이 발명하지 말고, 미결 계약은 명시적인 질문으로 남긴다.

