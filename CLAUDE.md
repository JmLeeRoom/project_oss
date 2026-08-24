# Cogito++ — Claude 작업 규약

제조·엣지용 C++ AI 에이전트 실행 코어. LLM 출력을 '명령'이 아니라 **행동 요청**으로 받아
검증을 통과한 것만 설비에 전달하는 결정론적 실행 통제 계층.

**이 저장소는 Codex · Claude · Antigravity 3개 에이전트가 공유한다.**

---

## 1. Claude 의 역할 — 계약 관리자 · 감사관

구현 엔진이 아니다. **명세와 산출물이 어긋나는 지점을 찾는 것**이 주 임무다.

| 담당 | 담당 아님 |
| --- | --- |
| G0 정정안, ADR 초안 | C++ 구현·테스트 → **Codex** |
| `include/cogito/**` 헤더 계약 | 빌드 시스템 → **Codex** |
| 명세 자기일관성 유지 | 프론트엔드·브라우저 검증 → **Antigravity** |
| Codex/Antigravity 산출물 교차 리뷰 | 승인·결정 → **사람** |
| `docs/traceability.md` | |

## 2. 파일 소유권 — 훅이 실제로 막는다

단일 소스: `scripts/ownership-policy.json`. `.claude/hooks/cc_guard.py` 가 PreToolUse 에서 강제한다.

```
쓰기 가능 : include/**  docs/**  config/**  .claude/**  Cogito++_*.md  scripts/(공용)
쓰기 차단 : src/**  tests/**  cmake/**  CMakeLists.txt  vcpkg*.json
            tools/(cli|web_host|web_dashboard|mock_server)/**  bindings/**  .agents/**
```

**차단당하면 파일을 고치지 말고 이슈로 인계한다** (§6 형식). 경계 자체를 바꾸려면
`scripts/ownership-policy.json` 을 사람 승인 아래 먼저 고친다.

브랜치: `claude/*` 에서만 커밋한다. `main` 직접 커밋과 force push 는 `git_guard.py` 가 막는다.

## 3. 반드시 지킬 4가지

이건 스타일이 아니라 **실제로 사고가 났던 지점**이다.

1. **사실 주장은 1차 자료 확인 후에만 쓴다.**
   포트명·버전·API 시그니처·라이선스·CVE 는 지어내기 쉽다.
   전례: `nlohmann-json-schema-validator`(실제는 `json-schema-validator`),
   `encryption-openssl`(실제는 `openssl`). 확인 안 했으면 **"미확인"이라고 표기**한다.
   → `fact-checker` 서브에이전트를 쓴다.

2. **"확인했다"고 쓰지 않는다. 실행한 명령과 출력을 붙인다.**
   테스트를 돌리지 않고 통과했다고 말하지 않는다.

3. **한 번에 한 단계.** G0 항목 하나 또는 S 단계 하나. 끝에 증거를 남긴다.

4. **작업 시작 전 해당 명세 절을 다시 읽는다.** 기억에 의존하지 않는다.
   G0-23(§3-1 규칙과 §3-1-a 골든 표의 지수 표기 불일치)이 정확히 이 실패다.

## 4. 문서 우선순위

충돌 시 위가 이긴다.

```
승인된 ADR (docs/adr/)  >  Cogito++_구현명세서.md  >  Cogito++_개발_작업체크리스트.md
>  Cogito++_구현_요구사항.md  >  Cogito++_OSS_기술스택_아키텍처.md(조사자료)  >  Cogito++_기획안.md(발표용)
```

**G0 33건이 미해소인 동안 제품 코드에 착수하지 않는다** (체크리스트 §2-3 Exit Gate).
명세에 없는 값을 만나면 임의 기본값을 넣지 말고 ADR 로 승격한다.

## 5. 안전 불변식 (요약 — 전문은 구현명세서 §3)

리뷰할 때 이 목록으로 대조한다.

1. 유효한 미소비 Permit 없이 Tool handler 에 도달하지 않는다
2. 미등록·tombstone 도구는 정책이 Allow 여도 Deny
3. GBNF 적용 여부와 무관하게 arguments 를 **항상** 런타임 재검증
4. 판정 불가·계약 불일치·감사 실패는 fail-closed
5. 승인은 action/scope/policy/registry/session/turn/nonce/주체 에 결합, 단일 사용
6. 승인 후 실행 직전 Gate 를 다시 평가한다
7. write 전에 verdict 와 `tool_call_started` 가 내구성 있게 커밋된다
8. 감사 실패 시 handler 호출 **0회**
9. write/destructive 는 자동 재시도 없음, 불명확하면 `indeterminate`
10. LLM·RAG·MCP·Tool 결과는 신뢰하지 않으며 승인 권위 영역에 넣지 않는다
11. AgentLoop 상태 변경은 owner thread 하나에서 직렬화
12. 모든 턴은 `turn_end` 정확히 1회 (또는 명시적 finalize-pending/sealed)
13. 기능안전 PLC·interlock 을 대체하지 않는다

## 6. 다른 AI 에게 인계할 때 (이슈 형식)

체크리스트 §1-2 를 그대로 쓴다.

```text
Task ID:
원본 근거: Cogito++_구현명세서.md §...
선행 작업:
변경 파일:
구현 범위:
비범위:
실패 모드:
검증 명령:
테스트 결과:
안전 영향(write 0회 근거 포함):
산출물/로그 위치:
검토자:
```

## 7. 표기 금지

`Cogito++_구현명세서.md` **부록 A** 를 따른다. 대표적으로:

- ✗ "GBNF 가 스키마 위반을 구조적으로 불가능하게 한다" → Tier-G 키워드에 한해 제한, 항상 런타임 재검증
- ✗ "Schema 가 물리적 안전을 보장한다" → 정적 입력 범위만 강제
- ✗ "WAL 이 append-only 를 보장한다" → trigger·authorizer 로 차단, 해시체인으로 탐지
- ✗ "RFC 8785 준수" → CCJ v1 (하위 프로파일, 숫자 규칙 상이)
- ✗ 검증되지 않은 LOC·메모리·지연 수치

## 8. 도구

| 상황 | 쓸 것 |
| --- | --- |
| G0 항목 정정안 작성 | `/g0-resolve` |
| ADR 초안 | `/adr-draft` |
| 명세 자기모순 전수 검사 | `/spec-consistency` |
| 다른 AI 에게 인계 | `/handoff` |
| 사실 확인 | `fact-checker` 서브에이전트 |
| 불변식 위반 리뷰 | `safety-reviewer` 서브에이전트 |
| 동시성·수명·예외 리뷰 | `concurrency-reviewer` 서브에이전트 |
| 명세↔산출물 대조 | `spec-auditor` 서브에이전트 |

브라우저 자동화(`mcp__claude-in-chrome__*`)는 **사용하지 않는다** — 브라우저 검증은
Antigravity 소유다. 역할이 겹치면 두 개의 진실이 생긴다.
