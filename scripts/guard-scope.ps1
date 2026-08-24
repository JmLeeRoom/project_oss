# Cogito++ Gemini write-scope guard.
# Every write-capable call must expose exact paths owned by Gemini.

$ErrorActionPreference = 'Stop'
$AllowedOwner = 'gemini'

function Write-Decision {
    param([string]$Decision, [string]$Reason)
    $value = @{ decision = $Decision }
    if ($Reason) { $value.reason = $Reason }
    $value | ConvertTo-Json -Compress
    exit 0
}

function Get-NamedValue {
    param($Value, [string[]]$Names)
    if ($null -eq $Value) { return $null }
    foreach ($name in $Names) {
        $property = $Value.PSObject.Properties[$name]
        if ($null -ne $property -and $null -ne $property.Value) { return $property.Value }
    }
    return $null
}

function Test-ReparseChain {
    param([string]$AbsolutePath, [string]$RootPath)

    $cursor = $AbsolutePath
    while (-not (Test-Path -LiteralPath $cursor)) {
        $parent = [IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { return $true }
        $cursor = $parent.FullName
    }

    while ($true) {
        try { $item = Get-Item -LiteralPath $cursor -Force } catch { return $true }
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return $true }
        if ($cursor.Equals($RootPath, [StringComparison]::OrdinalIgnoreCase)) { return $false }
        $parent = [IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { return $true }
        $cursor = $parent.FullName
        if (-not $cursor.StartsWith($RootPath, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
}

$raw = ''
try { $raw = [Console]::In.ReadToEnd() } catch { }
if ([string]::IsNullOrWhiteSpace($raw)) {
    Write-Decision 'deny' '[guard] empty write payload.'
}

try { $payload = $raw | ConvertFrom-Json } catch {
    Write-Decision 'deny' '[guard] invalid write payload.'
}

$toolCall = Get-NamedValue $payload @('toolCall', 'tool_call')
$toolName = [string](Get-NamedValue $toolCall @('name', 'tool_name'))
$arguments = Get-NamedValue $toolCall @('args', 'arguments', 'input')
if ([string]::IsNullOrWhiteSpace($toolName)) {
    $toolName = [string](Get-NamedValue $payload @('tool_name', 'toolName'))
    $arguments = Get-NamedValue $payload @('tool_input', 'toolInput')
}

$writeToolPattern = '^(?i:write|write_file|write_to_file|replace|replace_in_file|replace_file_content|edit|create|create_file|delete|delete_file|remove|remove_file|move|move_file|apply_patch)$'
if ($toolName -notmatch $writeToolPattern) {
    Write-Decision 'deny' '[guard] unrecognized write-capable tool.'
}
if ($toolName -match '^(?i:apply_patch)$') {
    Write-Decision 'deny' '[guard] Gemini apply_patch calls are not exact-path reviewable; use a file-targeted tool.'
}
if ($null -eq $arguments) {
    Write-Decision 'deny' '[guard] write payload has no arguments.'
}

$pathNames = @(
    'TargetFile', 'file_path', 'path', 'target', 'target_file',
    'destination', 'destination_path', 'new_path', 'source', 'source_path'
)
$targets = [Collections.Generic.List[string]]::new()
foreach ($name in $pathNames) {
    $property = $arguments.PSObject.Properties[$name]
    if ($null -eq $property) { continue }
    if ($property.Value -is [string] -and -not [string]::IsNullOrWhiteSpace($property.Value)) {
        $targets.Add([string]$property.Value)
    }
}
if ($targets.Count -eq 0) {
    Write-Decision 'deny' '[guard] write payload has no exact target path.'
}
if ($toolName -match '^(?i:move|move_file)$' -and $targets.Count -lt 2) {
    Write-Decision 'deny' '[guard] move calls must expose both source and destination.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$policyPath = Join-Path $repoRoot 'scripts/ownership-policy.json'
if (-not (Test-Path -LiteralPath $policyPath -PathType Leaf)) {
    Write-Decision 'deny' '[guard] ownership-policy.json is missing.'
}

try {
    $policy = Get-Content -LiteralPath $policyPath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    Write-Decision 'deny' '[guard] ownership-policy.json is invalid.'
}

$ownerNames = @($policy.owners.PSObject.Properties.Name | Sort-Object)
$contract = $policy.guard_contract
if (
    $policy.version -ne 2 -or
    $ownerNames.Count -ne 2 -or
    $ownerNames[0] -ne 'codex' -or
    $ownerNames[1] -ne 'gemini' -or
    $contract.algorithm -ne 'longest_prefix' -or
    $contract.case_sensitive -ne $false -or
    $contract.separator -ne '/' -or
    $contract.fallback_owner -ne 'deny' -or
    -not $policy.rules
) {
    Write-Decision 'deny' '[guard] ownership policy does not match the fail-closed v2 contract.'
}

$rootPath = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')
foreach ($target in $targets) {
    try {
        if ([IO.Path]::IsPathRooted($target)) {
            $targetPath = [IO.Path]::GetFullPath($target)
        } else {
            $targetPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $target))
        }
    } catch {
        Write-Decision 'deny' '[guard] target path is invalid.'
    }

    if ($targetPath.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Decision 'deny' '[guard] repository root is not an exact file target.'
    }
    if (-not $targetPath.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Decision 'deny' '[guard] write target is outside the repository.'
    }
    if (Test-ReparseChain $targetPath $rootPath) {
        Write-Decision 'deny' '[guard] symbolic-link or junction paths are not writable.'
    }

    $relative = $targetPath.Substring($rootPath.Length + 1).Replace('\', '/')
    foreach ($component in $relative.Split('/')) {
        if ([string]::IsNullOrWhiteSpace($component) -or $component.EndsWith('.') -or $component.EndsWith(' ') -or $component.Contains(':')) {
            Write-Decision 'deny' '[guard] target path contains an unsafe component.'
        }
    }

    $owner = [string]$contract.fallback_owner
    $bestLength = -1
    foreach ($rule in $policy.rules) {
        $prefix = ([string]$rule.prefix).Replace('\', '/')
        if ($rule.owner -notin @('gemini', 'codex')) {
            Write-Decision 'deny' '[guard] ownership policy contains an invalid owner.'
        }
        $matches = if ($prefix.EndsWith('/')) {
            $relative.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
        } else {
            $relative.Equals($prefix, [StringComparison]::OrdinalIgnoreCase)
        }
        if ($matches -and $prefix.Length -gt $bestLength) {
            $bestLength = $prefix.Length
            $owner = [string]$rule.owner
        }
    }

    if ($owner -ne $AllowedOwner) {
        Write-Decision 'deny' "[role boundary] '$relative' belongs to $owner; Gemini writes prompts and contracts only."
    }
}

Write-Decision 'allow' $null
