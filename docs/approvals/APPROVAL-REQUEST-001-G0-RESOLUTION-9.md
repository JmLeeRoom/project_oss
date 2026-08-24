# 결재 요청서 001 — G0 자기모순 9건 확정안

| | |
| --- | --- |
| **문서 ID** | `APPROVAL-REQUEST-001-G0-RESOLUTION-9` |
| **결재 대상** | `docs/g0/G0-RESOLUTION-9.md` 의 확정 결정 9건 (①~⑨) + `docs/adr/0001` · `docs/adr/0004` 의 상태 전환 |
| **요청자** | Claude (계약 관리자 · 감사관) |
| **요청일** | 2026-08-24 |
| **결재자** | 아키텍트 (9건 전부) · 안전 책임자 (② 공동) — `docs/g0/G0-RESOLUTION-9.md:689-694` |
| **현재 상태** | `docs/g0/G0-RESOLUTION-9.md:9` = `Proposed — 사람 승인 대기`, `docs/adr/0001-fsm-turn-and-action.md:3` = `Proposed`, `docs/adr/0004-audit-integrity-and-failure.md:3` = `Proposed` |

> **이 문서의 성격** — 이 파일은 규범이 아니다. 결재를 받기 위한 요청서다.
> 이 문서를 쓰면서 명세서·체크리스트·ADR·헤더 중 **어떤 것도 수정하지 않았다.**
> 원본 9건의 전문은 `docs/g0/G0-RESOLUTION-9.md`(698줄)에 있으며, 이 문서는 그 요약이다.
> 요약과 원본이 다르면 **원본이 이긴다.**

---

## 0. 한 장 요약

- G0 33건 중 **9건**의 자기모순 확정안이 작성되어 승인을 기다리고 있다. 승인 0건, 미착수 24건이다.
- 이 9건 중 **3건(④ ② ⑦)은 되돌림이 불가능**하다. 나중에 바꾸면 기존 감사 체인 검증·저장된 승인 기록·골든 벡터 24개가 전부 무효가 된다(`docs/adr/0004-audit-integrity-and-failure.md:202-208`).
- 이 승인 하나가 **Claude 의 명세 정정**, **Codex 의 S0 제품 코드 착수**, **G0 Exit Gate**(`Cogito++_개발_작업체크리스트.md:116`) 를 동시에 막고 있다.
- ⚠️ **승인해도 ADR 두 건은 `Accepted` 가 되지 않는다.** 이유는 §2 에 있다. 이것이 요약문의 지시와 다른 부분이다.

---

## 1. 결재 대상 — 권고 순서 `④ → ② → ⑦ → 나머지`

권고 순서의 근거: `docs/g0/G0-RESOLUTION-9.md:683`.
④ 를 가장 먼저 두는 이유는 `:675` — *"모든 projection 이 CCJ 를 쓴다. ④ 확정 없이 ⑦ 벡터를 만들 수 없다."*

### 1-A. 🔴 되돌림 불가 3건 — 먼저 결재

이 3건은 코드가 굳은 뒤에 바꾸면 **마이그레이션 경로가 없다**(`docs/adr/0004-audit-integrity-and-failure.md:204` — *"기존 `audit.db` 의 체인 검증이 전부 실패한다. 마이그레이션 경로가 없다"*).

| 순 | 항목 | 결정 요지 (1줄) | 승인자 | 되돌림 | 승인하지 않으면 막히는 것 |
| --- | --- | --- | --- | --- | --- |
| **1** | **④ G0-23**<br>CCJ 지수 표기 | 골든표가 권위다. 서술 규칙을 C99 `%g`(지수 최소 2자리)로 정정하고, 골든표 자체의 오류 1행(`9007199254740994.0`)도 함께 정정한다 | 아키텍트 | **불가** | 다른 8건 전부. CCJ 출력이 모든 digest 의 입력이므로 ④ 없이 ②·⑦ 의 고정 벡터를 만들 수 없다(`:675`). `Cogito++_구현명세서.md:167` 과 `:184` 가 서로 모순인 채로 남는다 |
| **2** | **② G0-05**<br>`operation_digest` 신설 | `indeterminate` 잠금 키를 `action_digest` 에서 분리해 `cogito-operation-v1` 도메인의 새 digest 로 옮긴다. 멱등 키는 그대로 둔다 | 아키텍트 **+ 안전 책임자** | **불가** | `Cogito++_구현명세서.md:1722` 의 안전 잠금이 **한 턴만 살고 사라지는 상태**로 남는다(`docs/g0/G0-RESOLUTION-9.md:120`). 불변식 9(`CLAUDE.md:79`)의 실효성 문제 |
| **3** | **⑦ G0-26**<br>도메인 태그 9개 + projection | 도메인 태그를 8→9개로 확정하고, 9개 digest 각각의 projection(넣는 필드·순서·제외 항목)을 고정한다 | 아키텍트 | **불가** | `include/cogito/digest.hpp` 작성 불가 → Codex 의 `src/digest.cpp` 착수 불가. 현재 `Cogito++_구현명세서.md:485-494` 는 태그 8개뿐이고 `:2495` 의 고정 벡터도 3개뿐이다 |

> **왜 이 3건만 분리했는가** — 되돌림 가능한 6건은 나중에 바꿔도 코드와 테스트를 고치면 된다.
> 이 3건은 바뀌면 **이미 기록된 감사 데이터와 승인 이력이 재검증 불가**가 된다.
> 지금은 `src/` 가 존재하지 않으므로(§2 참조) 되돌림 비용이 가장 싼 시점이다.

### 1-B. 되돌림 가능 6건 — 위 3건과 함께 또는 이어서 결재

| 순 | 항목 | 결정 요지 (1줄) | 승인자 | 되돌림 | 승인하지 않으면 막히는 것 |
| --- | --- | --- | --- | --- | --- |
| 4 | **① G0-01**<br>C ABI 버전 | ABI **v1.1 단일 기준**(`MAJOR=1 MINOR=1`). v1.0 은 릴리스된 적이 없으므로 호환 계층을 두지 않는다 | 아키텍트 | 가능 | `include/cogito/cogito.h` 작성 불가. 명세에 `cogito_run_turn` 이 두 개 있다(`Cogito++_구현명세서.md:2079` 인자 3개 / `:2128` 인자 4개). `:1997` = `MINOR 0`, `:2153` = `MINOR = 1` |
| 5 | **③ G0-09**<br>`Result<void>` · 예외 정책 | 무값 성공은 `Error` 로 통일. `Result<void>` 특수화는 제네릭 전용. 예외·OOM·`noexcept` 범위 확정 | 아키텍트 | 가능 | `Cogito++_구현명세서.md:290` §4-1 이 규범 없이 관례만 있는 상태로 남는다. ⑥ 의 `invoker.hpp` 예외 규칙이 여기 종속(`docs/g0/G0-RESOLUTION-9.md:677`) |
| 6 | **⑤ G0-24**<br>FSM 보편 규칙 R0~R4 | Cancel/AuditError 적용 대상 집합을 R0~R4 로 완전 정의. `Idle` 은 R1/R2 대상이 아니다 | 아키텍트 | 가능 | `Cogito++_구현명세서.md:974` 의 R2 에 `Idle` 이 남아 있어, `turn_begin` 없는 턴에 `turn_end` 가 생기는 경로가 규범으로 남는다(불변식 12). **추가로**: 커밋된 `include/cogito/fsm.hpp` 는 이미 R0~R4 를 반영했는데 명세는 아니어서, Codex 가 어느 쪽을 봐도 위반이 된다(`docs/STATUS-AUDIT-2026-08-24.md:145-153`) |
| 7 | **⑥ G0-25**<br>누락 헤더 3종 계약 | `invoker.hpp` · `ops_log.hpp` · `context_compactor.hpp` 의 계약을 확정한다 | 아키텍트 | 가능 | *"Codex 는 이 상태로 `.cpp` 를 쓸 수 없다"*(`docs/g0/G0-RESOLUTION-9.md:405`) |
| 8 | **⑧ G0-29**<br>모드 × effect 상한 | 모드를 숫자로 비교하지 않는다. effect 상한으로 사상한 뒤 최솟값을 취한다 | 아키텍트 | 가능 | `Cogito++_구현명세서.md:2170` 의 `min(config.mode, requested_mode)` 가 **정확히 반대로 동작**한다(`ReadOnly` 요청이 `Default` 로 넓어짐). Gate 5단계 구현 불가 |
| 9 | **⑨ G0-31**<br>승인 재진입 카운터 | 증가 시점 = `AwaitApproval→Gate`(`Event::Approved`), 상한 1, `Observe` 도달 시 erase / `turn_end` 시 clear | 아키텍트 | 가능 | `Cogito++_구현명세서.md:1555` 가 `gate_reentry_count` 를 **읽기만 하고** 증가·초기화 시점이 어디에도 없다. 구현자가 초기 `Ask` 에서 증가시키면 정상 승인 흐름이 끊긴다 |

**9건이 서로 모순되지 않는지는 교차 확인되어 있다** — `docs/g0/G0-RESOLUTION-9.md:668-683` 8쌍 점검, 결론 *"모순 없음"*.

---

## 2. 🔴 중요 — 이 승인은 ADR 을 `Accepted` 로 만들지 않는다

앞선 요약문은 *"승인 후 ADR 0001·0004 를 Accepted 로 전환"* 이라고 안내했다. **그대로 하면 안 된다.**

### 2-1. ADR-0004 는 스스로 Accepted 조건을 걸어놨고, 그 조건은 지금 실행 자체가 불가능하다

`docs/adr/0004-audit-integrity-and-failure.md:233`:

> **S1 Exit Gate** — 위 `canonical/*` 가 **3개 컴파일러에서 바이트 동일**해야 이 ADR 이 `Accepted` 가 된다.

그 테스트를 돌릴 수 없다. 실측 결과:

```
$ ls -d src CMakeLists.txt vcpkg.json vcpkg-configuration.json cmake
ls: cannot access 'src': No such file or directory
ls: cannot access 'CMakeLists.txt': No such file or directory
ls: cannot access 'vcpkg.json': No such file or directory
ls: cannot access 'vcpkg-configuration.json': No such file or directory
ls: cannot access 'cmake': No such file or directory

$ find . -name "*.cpp" -o -name "*.cmake"     (.git 제외)
(출력 없음 — 0건)
```

빌드 시스템도 소스도 없다. 3-컴파일러 비교는 S1 에 가서야 가능하다.
추가로 ADR-0004 자신이 미결 1건을 남겨뒀다 — `:239` *"D4 (a)/(b) 중 무엇을 규범으로 할지는 3-컴파일러 비교 결과가 나온 뒤"*.
`:96-99` 도 같은 취지로 *"판정 전에는 어느 쪽도 확정으로 표기하지 않는다"* 고 못 박았다.

### 2-2. ADR-0001 은 미결 2건이 남아 있다

`docs/adr/0001-fsm-turn-and-action.md:158-164`:

| # | 미결 | 왜 지금 정할 수 없는가 | 결정 필요 |
| --- | --- | --- | --- |
| M1 | **`kVerdictTtlNs` 의 구체값** | Verdict TTL 이 승인 대기(`approval_timeout_ms`)보다 짧으면 **승인해도 항상 만료**된다. 명세 어디에도 구체값이 없다 | **아키텍트 결정 필요 — 미결** |
| M2 | **`Observe` 에서의 `AuditError` 처리 순서** | `tool_result` 커밋이 실패해 턴이 `Failed` 로 끝나는데 **설비 write 는 이미 일어난 뒤**다. "성공한 write 가 있으나 결과 기록 실패" 상태를 감사에 어떻게 남길지 규범이 없다. ADR-0004`:243-245` 의 미결과 동일 사안 | **아키텍트 + 안전 책임자 결정 필요 — 미결** |

> Claude 는 이 두 값을 추정으로 채우지 않았다. `CLAUDE.md:41-44` 가 지목한 실패 유형이다.

### 2-3. 따라서 요청하는 상태 전환은 이것이다

```
docs/g0/G0-RESOLUTION-9.md   Proposed  ->  Approved (9건, 2026-__-__)
docs/adr/0001-…              Proposed  ->  Proposed → (승인됨, 미결 M1·M2 해소 대기)
docs/adr/0004-…              Proposed  ->  Proposed → (승인됨, S1 Exit Gate 대기)
```

**이 중간 상태의 의미** — 결정 내용은 확정이고 Claude 는 명세를 정정하며 Codex 는 S0 에 착수할 수 있다.
다만 ADR 문서의 `Accepted` 도장은 자기가 건 조건이 실측으로 충족된 뒤에 찍힌다.
`Accepted` 로 미리 표기하면 **돌리지 않은 테스트를 통과했다고 쓰는 것**이 된다(`CLAUDE.md:47`).

---

## 3. 결재란

각 항목에 하나만 표시하고 서명한다. **④ ② ⑦ 만 먼저 결재하고 나머지를 보류해도 된다** — 그렇게 해도 Claude 는 ④②⑦ 범위의 명세 정정에 착수할 수 있다.

### 3-A. 되돌림 불가 3건

| 항목 | 판정 | 승인자 | 서명 | 일자 |
| --- | --- | --- | --- | --- |
| **④ G0-23** CCJ 지수 규칙 + 골든표 1행 정정 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **② G0-05** `operation_digest` 신설 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **② G0-05** (공동 결재) | `[ ] 승인  [ ] 반려  [ ] 보류` | **안전 책임자** | | |
| **⑦ G0-26** 도메인 태그 9개 + projection | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |

### 3-B. 되돌림 가능 6건

| 항목 | 판정 | 승인자 | 서명 | 일자 |
| --- | --- | --- | --- | --- |
| **① G0-01** ABI v1.1 단일 기준 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **③ G0-09** `Result<void>` · 예외 정책 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **⑤ G0-24** FSM R0~R4 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **⑥ G0-25** 헤더 3종 계약 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **⑧ G0-29** 모드 × effect 상한 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| **⑨ G0-31** 재진입 카운터 | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |

### 3-C. ADR 상태 전환 (§2)

| 항목 | 판정 | 승인자 | 서명 | 일자 |
| --- | --- | --- | --- | --- |
| ADR-0001 → `Proposed → (승인됨, 미결 M1·M2 해소 대기)` | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |
| ADR-0004 → `Proposed → (승인됨, S1 Exit Gate 대기)` | `[ ] 승인  [ ] 반려  [ ] 보류` | 아키텍트 | | |

### 3-D. 미결 2건 — 값을 적어 주십시오 (선택. 지금 비워둬도 위 결재는 유효하다)

| # | 미결 | 결정값 | 승인자 | 서명 | 일자 |
| --- | --- | --- | --- | --- | --- |
| M1 | `kVerdictTtlNs` | ______________ ns | 아키텍트 | | |
| M2 | `Observe` 의 `AuditError` 처리 순서 / "write 성공 + 기록 실패" 감사 표현 | 별지 또는 후속 ADR | 아키텍트 + 안전 책임자 | | |

**반려·보류 사유 (해당 시)**

```
항목:
사유:
대안 지시:
```

---

## 4. 승인 직후 Claude 가 하는 일

승인 서명이 들어오면 Claude 는 **`Cogito++_구현명세서.md`** 를 정정한다.
체크리스트가 아니다 — `Cogito++_개발_작업체크리스트.md:112` 자신이 *"결정 결과를 `Cogito++_구현명세서.md` 후속 버전 또는 승인된 ADR에 역반영했다"* 라고 규정한다.
(체크리스트는 §2-2 G0 표의 상태·owner·링크만 갱신 대상이다. 두 작업은 분리한다.)

`docs/g0/G0-RESOLUTION-9.md` 의 `[명세 수정 diff]` 가 실제로 지목한 절 전체 목록:

| 결정 | 정정 대상 절 | 현행 위치 (실측) |
| --- | --- | --- |
| **④** | §3-1 CCJ-5 서술 규칙 | `Cogito++_구현명세서.md:167` |
| **④** | §3-1-a 골든표 1행 | `:184` |
| **②** | §6-4 `indeterminate` 잠금 키 | `:1722` |
| **②** | §4-12 `indeterminate_lock_` 주석 | `:1259` |
| **②** | §6-1 `ComputeOperationDigest` 신설 | §6-1 = `:1401` |
| **②** | §3-4 `indeterminate_lockdown` 설명 | `:283`, `:338` |
| **⑦** | §4-3 `namespace domain` 8개 → 9개 | `:485-494` |
| **⑦** | §6-1 projection 9개 + LP 인코딩 명문화 | `:1401` 이하 |
| **⑦** | §10-2 `canonical/digest_vectors` 3개 → 9개 | `:2495` |
| **①** | §8-2 제목 · v1.0 경고문 · `COGITO_ABI_VERSION_MINOR` · `cogito_run_turn` 시그니처 | `:1970`, `:1972`, `:1997`, `:2079` |
| **①** | §8-4 제목 및 *"v1.0을 대체하며"* 문장 삭제 | `:2151`, `:2153` |
| **①** | §8-2 enum 에 `COGITO_ERR_WRONG_THREAD = 26` 편입 | 현재 `:2213` (§8-4 에만 서술) |
| **③** | §4-1 `result.hpp` — `Result<void>` 특수화 + 규칙 4개 | `:290` |
| **⑤** | §4-10 보편 규칙 주석 블록 R1/R2/R3 → R0~R4 | `:973-974` |
| **⑤** | §5 `Fsm::ResolveUniversal` | `:1324`, `:1331` |
| **⑤** | §4-10 `ResetForNextTurn()` → `ResetForTestOnly()` | `:1003` |
| **⑥** | §4 계약 전문에 `ops_log.hpp` · `context_compactor.hpp` 절 신설, `ToolCallContext` 를 §4-12 에서 분리 | `:1092` (§4-12) |
| **⑧** | §4-7 `identity.hpp` 주석 | `:703` |
| **⑧** | §8-4 [S-1] `min(config.mode, requested_mode)` | `:2170` |
| **⑨** | §6-2 Gate 7단계 판정 순서 | `:1555` |
| **⑨** | §4-8 `ApprovalStore::FindUsable` | `:796` |
| **⑨** | §4-12 `gate_reentry_` 증가·초기화 주석 | `:1257` |

이어서 하는 일:

1. `docs/g0/G0-RESOLUTION-9.md:9` 의 상태 문자열을 결재 결과로 갱신하고, 항목별 승인일·승인자를 기록한다.
2. `docs/adr/0001-…:3` · `docs/adr/0004-…:3` 을 §2-3 의 중간 상태 문자열로 갱신한다(`Accepted` 로 쓰지 않는다).
3. `Cogito++_개발_작업체크리스트.md` §2-2 G0 표에 9건의 상태·owner·결정일·ADR 링크를 채운다.
4. `include/cogito/digest.hpp` **한 파일**에 도메인 태그 9개 + projection 함수 9개 + LP 인코딩 규약을 넣는다.
   `projection.hpp` 로 분리하지 않는다 — `docs/g0/G0-RESOLUTION-9.md:167` · `:493` 이 `digest.hpp` 를 지목했고, `:495` 가 *"projection 은 단일 serializer 로 구현하고 호출부에서 필드를 조립하지 않는다"* 고 확정했다.
5. `include/cogito/fsm.hpp` 상단의 선반영 고지를 승인 근거 표기로 교체한다(현재 이 헤더는 미승인 ⑤ 를 선반영한 상태다 — `docs/STATUS-AUDIT-2026-08-24.md:145-153`).
6. Codex 인계서를 낸다 — `src/canonical_json.cpp`(④), `src/digest.cpp`(②⑦), `src/fsm.cpp`·`src/agent_loop.cpp`(⑤⑨), `src/abi/cogito_abi.cpp`(①). 이 경로들은 Codex 소유이므로 Claude 는 쓰지 않는다.

**승인 없이 Claude 가 미리 하지 않는 일** (`docs/STATUS-AUDIT-2026-08-24.md:249-256`):
명세 본문 정정, ADR 의 `Accepted` 표기, 체크리스트 본문을 diff 대상으로 삼는 것, `projection.hpp` 신설 — 4건 전부 결재 이후다.

---

## 5. 승인 기록 형식 규약 (제안 — 이 결재로 함께 확정)

`docs/approvals/` 는 이 문서와 함께 신설됐다. 기존 규약이 없으므로 아래는 **제안**이며, 반려 시 지시대로 바꾼다.

| 항목 | 규약 |
| --- | --- |
| **경로** | `docs/approvals/` (Claude 소유 — `CLAUDE.md:27`) |
| **파일명** | `APPROVAL-REQUEST-<NNN>-<대상슬러그>.md`. `NNN` 은 001부터 순차, 재사용하지 않는다 |
| **1파일 = 1결재 단위** | 결재자가 한 번에 판단할 수 있는 범위로 끊는다. 결재 후에도 **파일을 지우지 않는다** — 서명본이 감사 증거다 |
| **상태 문자열** | `Pending` / `Approved` / `Partially Approved` / `Rejected` / `Withdrawn`. 문서 상단 메타 표의 `현재 상태` 행에 기재하고, 항목별 판정은 §3 결재란이 권위 |
| **ADR 상태 문자열** | `Proposed` / `Proposed → (승인됨, <해소 대기 조건>)` / `Accepted` / `Superseded by ADR-NNNN`. 중간 상태는 **대기 조건을 반드시 함께 적는다** |
| **서명 형식** | 결재란에 `역할 / 이름 / 일자(YYYY-MM-DD)`. 전자 결재·이메일 승인이면 그 사실과 참조 ID 를 서명 칸에 적는다 |
| **연결** | 승인된 결재는 `docs/traceability.md` 에 `요구사항 → 명세 절 → ADR → 결재요청 ID → 작업 ID → 테스트 ID` 로 연결한다(`Cogito++_개발_작업체크리스트.md:114`) |
| **AI 의 쓰기 권한** | Claude 는 요청서 작성과 결재 결과 전사만 한다. **결재란의 판정·서명은 사람만 기입한다** |

**미결** — 전자 결재 시스템 연동 여부, 결재자 실명 표기 정책(개인정보), `Partially Approved` 상태에서 부분 착수 허용 범위. **제품 책임자 결정 필요.**

---

## 부록. 근거 인용 색인

| 주장 | 출처 |
| --- | --- |
| 9건 요약 + 되돌림 여부 | `docs/g0/G0-RESOLUTION-9.md:19-31` |
| 승인 순서 권고 `④ → ② → ⑦ → 나머지` | `docs/g0/G0-RESOLUTION-9.md:683` |
| 항목별 승인자 지정 | `docs/g0/G0-RESOLUTION-9.md:689-694` |
| 9건 상호 정합성 점검 | `docs/g0/G0-RESOLUTION-9.md:668-683` |
| ADR-0004 의 Accepted 조건 = 3-컴파일러 바이트 동일 | `docs/adr/0004-audit-integrity-and-failure.md:233` |
| ADR-0004 되돌림 불가 사유 | `docs/adr/0004-audit-integrity-and-failure.md:9-11`, `:202-208` |
| ADR-0004 미결 3건 | `docs/adr/0004-audit-integrity-and-failure.md:237-245` |
| ADR-0001 미결 2건 | `docs/adr/0001-fsm-turn-and-action.md:158-164` |
| ADR-0001 되돌림 가능성 | `docs/adr/0001-fsm-turn-and-action.md:9` |
| 정정 대상은 구현명세서 (체크리스트 아님) | `Cogito++_개발_작업체크리스트.md:112` |
| G0 Exit Gate → S0 착수 조건 | `Cogito++_개발_작업체크리스트.md:116` |
| 결재 주체 4역 | `Cogito++_개발_작업체크리스트.md:114` |
| 하면 안 되는 일 4건 | `docs/STATUS-AUDIT-2026-08-24.md:249-256` |
| 커밋된 `fsm.hpp` 와 명세의 불일치 | `docs/STATUS-AUDIT-2026-08-24.md:145-153` |
| `src/`·빌드 파일 부재 | 본 문서 §2-1 의 명령 출력 (2026-08-24 실측) |
| 문서 우선순위 | `CLAUDE.md:59-62` |
| 안전 불변식 9 (재시도 금지·indeterminate) | `CLAUDE.md:79` |
| 표기 금지 목록 | `Cogito++_구현명세서.md:3048-3066` (부록 A) |

**이 결재로 닫히지 않는 것** — G0 33건 중 나머지 24건.
그중 `G0-13`(llama.cpp 공급망) · `G0-14`(OPC UA 실설비값) · `G0-17`(인증원) · `G0-22`(CI·서명 주체) · `G0-16`(릴리스 범위)는 *"AI 가 정할 수 없다"* (`docs/g0/G0-RESOLUTION-9.md:697-698`). 별도 결재 요청서로 올린다.
