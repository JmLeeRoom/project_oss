# Cogito++ — Antigravity PreToolUse 소유권 가드
#
# ★ 이 스크립트는 scripts/ownership-policy.json 을 '읽는다'. 규칙을 복제하지 않는다.
#   이전 버전은 4개 패턴(src/, include/, tests/core/, CMakeLists.txt)만 하드코딩해서
#   docs/**, config/**, .claude/**, Cogito++_*.md 를 전부 allow 했다.
#   실측 결과 Antigravity 가 명세서·ADR·Claude 훅 설정까지 덮어쓸 수 있었다.
#   세 에이전트의 가드가 각자 규칙을 복제하면 반드시 갈라진다 — 정책 파일 하나만 본다.
#
# 입력 : Antigravity PreToolUse payload (stdin JSON)
#          {"toolCall":{"name":"write_to_file","args":{"TargetFile":"..."}}}
#        ※ Claude Code 는 스키마가 다르다(tool_name / tool_input.file_path).
#          정책만 공유하고 어댑터는 도구별로 둔다. Claude 쪽은 .claude/hooks/cc_guard.py.
# 출력 : {"decision":"deny","reason":"..."} 또는 {"decision":"allow"}

$ErrorActionPreference = 'Stop'
$AllowedOwners = @('antigravity', 'shared')

function Write-Decision {
    param([string]$Decision, [string]$Reason)
    $o = @{ decision = $Decision }
    if ($Reason) { $o['reason'] = $Reason }
    ($o | ConvertTo-Json -Compress)
    exit 0
}

# ── 입력 파싱 ────────────────────────────────────────────────────────────────
$raw = $null
try { $raw = [Console]::In.ReadToEnd() } catch { }
if (-not $raw) { Write-Decision 'allow' $null }

$inputJson = $null
try { $inputJson = $raw | ConvertFrom-Json } catch { Write-Decision 'allow' $null }

$toolName = $null
$target = $null
if ($inputJson.toolCall) {
    $toolName = $inputJson.toolCall.name
    if ($inputJson.toolCall.args) { $target = $inputJson.toolCall.args.TargetFile }
}
if (-not $target) { Write-Decision 'allow' $null }

# 쓰기 계열 도구만 검사한다
if ($toolName -and ($toolName -notmatch 'write|replace|edit|create|delete|remove')) {
    Write-Decision 'allow' $null
}

# ── 정책 로드 (fail-closed) ──────────────────────────────────────────────────
$repoRoot = Split-Path -Parent $PSScriptRoot
$policyPath = Join-Path $repoRoot 'scripts/ownership-policy.json'

if (-not (Test-Path $policyPath)) {
    Write-Decision 'deny' ("[가드 오류] scripts/ownership-policy.json 을 찾을 수 없습니다. " +
        "소유권 경계를 확인할 수 없으므로 fail-closed 합니다. 정책 파일을 복구하십시오.")
}

$policy = $null
try {
    $policy = (Get-Content -Path $policyPath -Raw -Encoding UTF8) | ConvertFrom-Json
} catch {
    Write-Decision 'deny' ("[가드 오류] ownership-policy.json 파싱 실패 — 경계를 확인할 수 없으므로 fail-closed 합니다.")
}
if (-not $policy.rules) {
    Write-Decision 'deny' "[가드 오류] ownership-policy.json 에 rules 가 없습니다. fail-closed 합니다."
}

# ── 경로 정규화 (저장소 루트 기준 상대경로, 구분자 '/') ──────────────────────
$normalized = $target -replace '\\', '/'
$rootNorm = ($repoRoot -replace '\\', '/').TrimEnd('/')
$rel = $null

if ($normalized.ToLower().StartsWith($rootNorm.ToLower() + '/')) {
    $rel = $normalized.Substring($rootNorm.Length + 1)
} elseif ([System.IO.Path]::IsPathRooted($normalized)) {
    Write-Decision 'allow' $null       # 저장소 밖 (임시 디렉터리 등)
} else {
    $rel = $normalized
    while ($rel.StartsWith('./')) { $rel = $rel.Substring(2) }
}

# ── 가장 긴 prefix 우선 ──────────────────────────────────────────────────────
$owner = 'shared'
$bestLen = -1
foreach ($rule in $policy.rules) {
    $p = $rule.prefix -replace '\\', '/'
    if (($rel -eq $p) -or ($rel.StartsWith($p))) {
        if ($p.Length -gt $bestLen) {
            $bestLen = $p.Length
            $owner = $rule.owner
        }
    }
}

if ($AllowedOwners -contains $owner) { Write-Decision 'allow' $null }

Write-Decision 'deny' ("[역할 경계] '$rel' 의 소유자는 '$owner' 입니다. " +
    "Antigravity 는 tools/web_dashboard/**, tools/mock_server/**, tests/web/**, .agents/** 만 씁니다. " +
    "파일을 고치지 말고 체크리스트 1-2 형식의 이슈로 $owner 에게 인계하십시오. " +
    "경계 자체를 바꿔야 한다면 scripts/ownership-policy.json 을 사람 승인 아래 먼저 수정합니다.")
