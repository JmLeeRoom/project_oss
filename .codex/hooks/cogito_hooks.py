from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


SCHEMA_VERSION = 3
TICKET_RE = re.compile(r"^S(?:[0-9]|1[01])-(?:0[1-9]|[1-9][0-9])$")
OID_RE = re.compile(r"^[0-9a-fA-F]{40}(?:[0-9a-fA-F]{24})?$")
VALID_PHASES = {"idle", "preflight", "implementation", "verification", "complete", "blocked"}
VALID_CONTRACT_REVIEWS = {"not_run", "pass", "needs_claude", "blocked"}
VALID_VERIFIERS = {"not_run", "pass", "fail", "incomplete_evidence"}
VALID_STAGE_RESULTS = {"not_evaluated", "pass", "fail", "incomplete_evidence"}
PROTECTED_BRANCHES = {"main", "master"}
STATE_FIELDS = {
    "$schema",
    "schema_version",
    "active_ticket",
    "baseline_head_oid",
    "phase",
    "g0_gate",
    "contract_review",
    "contract_reference",
    "allowed_write_paths",
    "shared_write",
    "verification",
    "publication",
    "safety",
}
SHARED_WRITE_FIELDS = {"approved", "paths", "reference"}
VERIFICATION_FIELDS = {
    "ticket",
    "commands",
    "evidence_dir",
    "manifest_file",
    "verdict_file",
    "verifier",
    "stage_exit_gate",
    "head_oid",
    "workspace_fingerprint",
}
PUBLICATION_FIELDS = {
    "commit_authorized",
    "push_authorized",
    "merge_authorized",
    "pull_request_authorized",
    "release_authorized",
    "reference",
}
SAFETY_FIELDS = {"destructive_git_authorized", "destructive_files_authorized", "reference"}
PLANNED_RUNNERS = {"cmake", "ctest", "ninja", "msbuild", "devenv", "vcpkg"}
DANGEROUS_GIT_LONG_OPTIONS = (
    "--output",
    "--output-directory",
    "--open-files-in-pager",
    "--paginate",
    "--ext-diff",
    "--filters",
    "--textconv",
    "--exec",
    "--edit",
    "--edit-todo",
    "--interactive",
    "--patch",
    "--template",
    "--reedit-message",
    "--strategy",
    "--strategy-option",
    "--upload-pack",
    "--receive-pack",
    "--gpg-sign",
    "--sign",
    "--local-user",
    "--help",
    "--web",
    "--config",
    "--config-env",
    "--exec-path",
    "--git-dir",
    "--work-tree",
    "--namespace",
    "--super-prefix",
)
GIT_QUERY_TIMEOUT_SECONDS = 3
GIT_STATUS_TIMEOUT_SECONDS = 8
STOP_GIT_TIMEOUT_CEILING_SECONDS = (
    2 * GIT_QUERY_TIMEOUT_SECONDS + 4 * GIT_STATUS_TIMEOUT_SECONDS
)
WINDOWS_DEVICE_NAMES = {
    "con",
    "prn",
    "aux",
    "nul",
    *(f"com{index}" for index in range(1, 10)),
    *(f"lpt{index}" for index in range(1, 10)),
}
GUARDED_PATHSPECS = (
    ".agents",
    ".claude",
    ".codex",
    "include",
    "docs",
    "config",
    "src",
    "tests",
    "cmake",
    "tools/cli",
    "tools/web_host",
    "tools/web_dashboard",
    "tools/mock_server",
    "bindings",
    "scripts",
    "README.md",
    "TASK_PROMPT_*",
    "AGENTS.md",
    "CLAUDE.md",
    "Cogito++_*",
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
    "vcpkg-configuration.json",
)

OWNERSHIP_POLICY_PATH = Path(__file__).resolve().parents[2] / "scripts" / "ownership-policy.json"


def _load_ownership_policy() -> tuple[list[tuple[str, str]], str, tuple[str, ...]]:
    try:
        policy = json.loads(OWNERSHIP_POLICY_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot load ownership policy: {exc}") from exc

    if not isinstance(policy, dict):
        raise RuntimeError("ownership policy root must be an object")
    contract = policy.get("guard_contract")
    expected_contract = {
        "algorithm": "longest_prefix",
        "case_sensitive": False,
        "separator": "/",
        "directory_prefix_suffix": "/",
        "special_root_prefixes": ["Cogito++_"],
        "fallback_owner": "shared",
    }
    if contract != expected_contract:
        raise RuntimeError("ownership policy guard_contract is missing or unsupported")
    raw_rules = policy.get("rules")
    if not isinstance(raw_rules, list) or not raw_rules:
        raise RuntimeError("ownership policy rules must be a non-empty array")

    rules: list[tuple[str, str]] = []
    seen: set[str] = set()
    for index, rule in enumerate(raw_rules, start=1):
        if not isinstance(rule, dict):
            raise RuntimeError(f"ownership policy rule {index} must be an object")
        prefix = rule.get("prefix")
        owner = rule.get("owner")
        guard_owner = rule.get("guard_owner", owner)
        if (
            not isinstance(prefix, str)
            or not prefix
            or "\\" in prefix
            or prefix.startswith("/")
            or ".." in PurePosixPath(prefix).parts
        ):
            raise RuntimeError(f"ownership policy rule {index} has an invalid prefix")
        prefix_key = prefix.casefold()
        if prefix_key in seen:
            raise RuntimeError(f"ownership policy contains duplicate prefix {prefix!r}")
        seen.add(prefix_key)
        if not isinstance(owner, str) or owner not in {"claude", "codex", "antigravity"}:
            raise RuntimeError(f"ownership policy rule {index} has an invalid owner")
        if not isinstance(guard_owner, str) or guard_owner not in {
            "claude",
            "antigravity",
            "codex-control",
            "codex-product",
        }:
            raise RuntimeError(f"ownership policy rule {index} has an invalid guard_owner")
        if owner == "codex" and not str(guard_owner).startswith("codex-"):
            raise RuntimeError(f"ownership policy rule {index} must classify Codex control or product scope")
        if owner != "codex" and guard_owner != owner:
            raise RuntimeError(f"ownership policy rule {index} changes a non-Codex owner")
        rules.append((prefix, guard_owner))

    return (
        sorted(rules, key=lambda item: len(item[0]), reverse=True),
        contract["fallback_owner"],
        tuple(contract["special_root_prefixes"]),
    )


def _load_ownership_policy_fail_closed() -> tuple[
    list[tuple[str, str]], str, tuple[str, ...], str | None
]:
    try:
        rules, fallback, special_root_prefixes = _load_ownership_policy()
        return rules, fallback, special_root_prefixes, None
    except Exception as exc:
        # A broken guard configuration must not prevent the hook process from
        # returning an explicit deny/block decision. Catch ordinary loader and
        # validation defects here, retain a deterministic fallback, and expose
        # the error to every lifecycle decision.
        return [], "shared", (), f"{type(exc).__name__}: {exc}"


(
    OWNERSHIP_RULES,
    OWNERSHIP_FALLBACK,
    OWNERSHIP_SPECIAL_ROOT_PREFIXES,
    OWNERSHIP_POLICY_ERROR,
) = _load_ownership_policy_fail_closed()

PATCH_HEADER_RE = re.compile(r"^\*\*\* (Add|Update|Delete) File:\s*(.+?)\s*$", re.MULTILINE)
PATCH_MOVE_RE = re.compile(r"^\*\*\* Move to:\s*(.+?)\s*$", re.MULTILINE)


class StateError(ValueError):
    pass


def default_state() -> dict[str, Any]:
    return {
        "$schema": "./stage-state.schema.json",
        "schema_version": SCHEMA_VERSION,
        "active_ticket": None,
        "baseline_head_oid": None,
        "phase": "idle",
        "g0_gate": "not_passed",
        "contract_review": "not_run",
        "contract_reference": None,
        "allowed_write_paths": [],
        "shared_write": {"approved": False, "paths": [], "reference": None},
        "verification": {
            "ticket": None,
            "commands": [],
            "evidence_dir": None,
            "manifest_file": None,
            "verdict_file": None,
            "verifier": "not_run",
            "stage_exit_gate": "not_evaluated",
            "head_oid": None,
            "workspace_fingerprint": None,
        },
        "publication": {
            "commit_authorized": False,
            "push_authorized": False,
            "merge_authorized": False,
            "pull_request_authorized": False,
            "release_authorized": False,
            "reference": None,
        },
        "safety": {
            "destructive_git_authorized": False,
            "destructive_files_authorized": False,
            "reference": None,
        },
    }


def _is_mapping(value: Any) -> bool:
    return isinstance(value, dict)


def _validate_exact_keys(
    value: dict[str, Any], field: str, expected: set[str], errors: list[str]
) -> None:
    actual = set(value)
    missing = sorted(expected - actual)
    unknown = sorted(actual - expected)
    if missing:
        errors.append(f"{field} is missing required keys: {', '.join(missing)}")
    if unknown:
        errors.append(f"{field} has unknown keys: {', '.join(unknown)}")


def normalize_relative_path(raw: Any, *, keep_directory_hint: bool = False) -> str:
    if not isinstance(raw, (str, Path)):
        raise ValueError("path must be a string")
    original = str(raw).strip().replace("\\", "/")
    if not original or "\x00" in original:
        raise ValueError("path is empty or contains NUL")
    if original.startswith("/") or original.startswith("//") or re.match(r"^[A-Za-z]:", original):
        raise ValueError("absolute paths are outside the repository boundary")
    parts: list[str] = []
    for part in PurePosixPath(original).parts:
        if part in {"", "."}:
            continue
        if part == "..":
            raise ValueError("parent traversal is outside the repository boundary")
        if part.endswith((".", " ")):
            raise ValueError("Windows trailing-dot and trailing-space aliases are forbidden")
        if ":" in part:
            raise ValueError("Windows alternate data streams and colon aliases are forbidden")
        device_stem = part.split(".", 1)[0].casefold()
        if device_stem in WINDOWS_DEVICE_NAMES:
            raise ValueError("Windows device-name aliases are forbidden")
        parts.append(part)
    if not parts:
        raise ValueError("path resolves to the repository root")
    normalized = "/".join(parts)
    if keep_directory_hint and original.endswith("/"):
        normalized += "/"
    return normalized


def _path_key(path: str) -> str:
    # Cogito++ is operated on Windows and its ownership contract is therefore
    # case-insensitive even when the same guard is exercised from another OS.
    return path.casefold()


def path_is_within(path: str, boundary: str) -> bool:
    path_key = _path_key(path.rstrip("/"))
    boundary_key = _path_key(boundary.rstrip("/"))
    if path_key == boundary_key:
        return True
    return boundary.endswith("/") and path_key.startswith(boundary_key + "/")


def owner_for_path(path: str) -> str:
    key = _path_key(path)
    for prefix, owner in OWNERSHIP_RULES:
        prefix_key = _path_key(prefix)
        if prefix.endswith("/"):
            if key.startswith(prefix_key):
                return owner
        elif key == prefix_key or (
            prefix in OWNERSHIP_SPECIAL_ROOT_PREFIXES and key.startswith(prefix_key)
        ):
            return owner
    return OWNERSHIP_FALLBACK


def _normalized_path_list(value: Any, field: str) -> tuple[list[str], list[str]]:
    if not isinstance(value, list):
        return [], [f"{field} must be an array"]
    normalized: list[str] = []
    seen: set[str] = set()
    errors: list[str] = []
    for item in value:
        try:
            path = normalize_relative_path(item, keep_directory_hint=True)
        except ValueError as exc:
            errors.append(f"{field}: {exc}")
            continue
        key = _path_key(path)
        if key in seen:
            errors.append(f"{field} contains duplicate path {path}")
        else:
            seen.add(key)
            normalized.append(path)
    return normalized, errors


def validate_state(state: Any) -> list[str]:
    if not _is_mapping(state):
        return ["stage state must be a JSON object"]
    errors: list[str] = []
    _validate_exact_keys(state, "stage state", STATE_FIELDS, errors)
    if state.get("$schema") != "./stage-state.schema.json":
        errors.append("$schema must be ./stage-state.schema.json")
    if state.get("schema_version") != SCHEMA_VERSION:
        errors.append(f"schema_version must be {SCHEMA_VERSION}")

    ticket = state.get("active_ticket")
    baseline_head_oid = state.get("baseline_head_oid")
    phase = state.get("phase")
    if not isinstance(phase, str) or phase not in VALID_PHASES:
        errors.append(f"phase must be one of {sorted(VALID_PHASES)}")
    if ticket is not None and (not isinstance(ticket, str) or not TICKET_RE.fullmatch(ticket)):
        errors.append("active_ticket must be null or an exact S0-01 through S11-xx identifier")
    if phase == "idle" and ticket is not None:
        errors.append("idle state cannot retain an active_ticket")
    if phase != "idle" and ticket is None:
        errors.append("a non-idle phase requires active_ticket")
    if phase == "idle" and baseline_head_oid is not None:
        errors.append("idle state cannot retain a baseline_head_oid")
    if phase != "idle" and (
        not isinstance(baseline_head_oid, str) or not OID_RE.fullmatch(baseline_head_oid)
    ):
        errors.append("a non-idle phase requires a 40- or 64-hex baseline_head_oid")
    g0_gate = state.get("g0_gate")
    if not isinstance(g0_gate, str) or g0_gate not in {"not_passed", "passed"}:
        errors.append("g0_gate must be not_passed or passed")
    contract_review = state.get("contract_review")
    if not isinstance(contract_review, str) or contract_review not in VALID_CONTRACT_REVIEWS:
        errors.append("contract_review has an invalid value")
    contract_reference = state.get("contract_reference")
    if contract_reference is not None and (
        not isinstance(contract_reference, str) or not contract_reference.strip()
    ):
        errors.append("contract_reference must be null or a string")

    allowlist, allowlist_errors = _normalized_path_list(state.get("allowed_write_paths"), "allowed_write_paths")
    errors.extend(allowlist_errors)
    if phase == "idle" and allowlist:
        errors.append("idle state must have an empty allowed_write_paths list")
    if isinstance(phase, str) and phase in {"implementation", "verification", "complete"}:
        if state.get("g0_gate") != "passed":
            errors.append(f"{phase} requires g0_gate=passed")
        if state.get("contract_review") != "pass":
            errors.append(f"{phase} requires contract_review=pass")
        if not isinstance(state.get("contract_reference"), str) or not state.get("contract_reference", "").strip():
            errors.append(f"{phase} requires a non-empty contract_reference")
        if not allowlist:
            errors.append(f"{phase} requires a non-empty allowed_write_paths list")

    shared = state.get("shared_write")
    if not _is_mapping(shared):
        errors.append("shared_write must be an object")
    else:
        _validate_exact_keys(shared, "shared_write", SHARED_WRITE_FIELDS, errors)
        shared_paths, shared_errors = _normalized_path_list(shared.get("paths"), "shared_write.paths")
        errors.extend(shared_errors)
        if not isinstance(shared.get("approved"), bool):
            errors.append("shared_write.approved must be boolean")
        if shared.get("approved"):
            if not shared_paths:
                errors.append("approved shared_write requires exact paths")
            if not isinstance(shared.get("reference"), str) or not shared.get("reference", "").strip():
                errors.append("approved shared_write requires a human approval reference")
            for shared_path in shared_paths:
                if not _path_allowed(shared_path, allowlist):
                    errors.append(
                        f"shared_write.paths entry {shared_path} must also be inside allowed_write_paths"
                    )
        elif shared_paths or shared.get("reference") is not None:
            errors.append("unapproved shared_write must have empty paths and null reference")

    verification = state.get("verification")
    if not _is_mapping(verification):
        errors.append("verification must be an object")
    else:
        _validate_exact_keys(verification, "verification", VERIFICATION_FIELDS, errors)
        verification_ticket = verification.get("ticket")
        if verification_ticket is not None and (
            not isinstance(verification_ticket, str) or not TICKET_RE.fullmatch(verification_ticket)
        ):
            errors.append("verification.ticket must be null or an exact ticket identifier")
        commands = verification.get("commands")
        if not isinstance(commands, list) or any(not isinstance(command, str) or not command.strip() for command in commands):
            errors.append("verification.commands must be an array of non-empty strings")
        verifier = verification.get("verifier")
        if not isinstance(verifier, str) or verifier not in VALID_VERIFIERS:
            errors.append("verification.verifier has an invalid value")
        stage_exit_gate = verification.get("stage_exit_gate")
        if not isinstance(stage_exit_gate, str) or stage_exit_gate not in VALID_STAGE_RESULTS:
            errors.append("verification.stage_exit_gate has an invalid value")
        for field in ("evidence_dir", "manifest_file", "verdict_file"):
            value = verification.get(field)
            if value is not None:
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"verification.{field} must be null or a non-empty repository path")
                else:
                    try:
                        normalize_relative_path(value, keep_directory_hint=field == "evidence_dir")
                    except ValueError as exc:
                        errors.append(f"verification.{field}: {exc}")
        head_oid = verification.get("head_oid")
        if head_oid is not None and (not isinstance(head_oid, str) or not head_oid.strip()):
            errors.append("verification.head_oid must be null or a non-empty string")
        workspace_fingerprint = verification.get("workspace_fingerprint")
        if workspace_fingerprint is not None and (
            not isinstance(workspace_fingerprint, str)
            or not re.fullmatch(r"sha256:[0-9a-f]{64}", workspace_fingerprint)
        ):
            errors.append("verification.workspace_fingerprint must be null or sha256:<64-lower-hex>")
        if isinstance(phase, str) and phase in {"preflight", "implementation", "verification", "complete"}:
            if verification.get("ticket") != ticket:
                errors.append(f"{phase} requires verification.ticket to match active_ticket")
            if not commands:
                errors.append(f"{phase} requires planned verification.commands")
            for field in ("evidence_dir", "manifest_file", "verdict_file"):
                value = verification.get(field)
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"{phase} requires a planned verification.{field}")

    publication = state.get("publication")
    publication_flags = (
        "commit_authorized",
        "push_authorized",
        "merge_authorized",
        "pull_request_authorized",
        "release_authorized",
    )
    if not _is_mapping(publication):
        errors.append("publication must be an object")
    else:
        _validate_exact_keys(publication, "publication", PUBLICATION_FIELDS, errors)
        if any(not isinstance(publication.get(flag), bool) for flag in publication_flags):
            errors.append("publication authorization fields must be boolean")
        if any(publication.get(flag) for flag in publication_flags):
            if not isinstance(publication.get("reference"), str) or not publication.get("reference", "").strip():
                errors.append("publication authorization requires an explicit user reference")
        elif publication.get("reference") is not None:
            errors.append("publication.reference must be null when no publication is authorized")

    safety = state.get("safety")
    if not _is_mapping(safety):
        errors.append("safety must be an object")
    else:
        _validate_exact_keys(safety, "safety", SAFETY_FIELDS, errors)
        flags = ("destructive_git_authorized", "destructive_files_authorized")
        if any(not isinstance(safety.get(flag), bool) for flag in flags):
            errors.append("safety authorization fields must be boolean")
        if any(safety.get(flag) for flag in flags):
            if not isinstance(safety.get("reference"), str) or not safety.get("reference", "").strip():
                errors.append("destructive authorization requires an explicit user reference")
        elif safety.get("reference") is not None:
            errors.append("safety.reference must be null when no destructive action is authorized")
    return errors


def load_state(root: Path) -> dict[str, Any]:
    path = root / ".codex" / "stage-state.json"
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
        errors = validate_state(state)
    except Exception as exc:
        raise StateError(f"cannot load {path}: {exc}") from exc
    if errors:
        raise StateError("; ".join(errors))
    return state


def deny(reason: str) -> dict[str, Any]:
    return {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }


def extract_patch_entries(patch: str) -> list[tuple[str, str]]:
    entries = [(action.lower(), path) for action, path in PATCH_HEADER_RE.findall(patch or "")]
    entries.extend(("move", path) for path in PATCH_MOVE_RE.findall(patch or ""))
    return entries


def extract_patch_paths(patch: str) -> list[str]:
    return [path for _, path in extract_patch_entries(patch)]


def _ticket_branch_matches(branch: str | None, ticket: str | None) -> bool:
    if not branch or not ticket:
        return False
    prefix = f"codex/{ticket.casefold()}-"
    branch_key = branch.casefold()
    return branch_key.startswith(prefix) and len(branch_key) > len(prefix)


def _path_allowed(path: str, boundaries: Iterable[str]) -> bool:
    return any(path_is_within(path, boundary) for boundary in boundaries)


def _path_write_decision(path: str, root: Path, state: dict[str, Any], branch: str | None) -> dict[str, Any]:
    try:
        resolved = _safe_resolve(root, path)
        resolved_relative = resolved.relative_to(root.resolve(strict=False)).as_posix()
    except (OSError, StateError, ValueError) as exc:
        return deny(f"{path} has an unsafe canonical target: {exc}.")
    if _path_key(resolved_relative) != _path_key(path.rstrip("/")):
        return deny(f"{path} resolves through a filesystem alias, symlink, or junction; use its canonical path.")
    owner = owner_for_path(path)
    branch_key = (branch or "").casefold()
    if branch_key in PROTECTED_BRANCHES:
        if _path_key(path) == ".codex/stage-state.json":
            return {}
        return deny("Writes on main/master are blocked; only preflight stage-state metadata may be recorded there.")
    if not branch:
        return deny("Git branch discovery failed; writes fail closed until the repository branch is known.")
    if not branch_key.startswith("codex/"):
        return deny("Codex writes require a codex/* branch in its dedicated worktree.")

    if owner == "codex-control":
        return {}
    if owner == "claude":
        return deny(f"{path} is Claude-owned contract or documentation scope.")
    if owner == "antigravity":
        return deny(f"{path} is Antigravity-owned dashboard, browser, or control scope.")

    errors = validate_state(state)
    if errors:
        return deny("Invalid stage state blocks non-control writes: " + "; ".join(errors))
    ticket = state.get("active_ticket")
    if not ticket or state.get("phase") == "idle":
        return deny("Select exactly one approved ticket and complete contract preflight before non-control writes.")
    if not _ticket_branch_matches(branch, ticket):
        expected = f"codex/{str(ticket or 'Sx-xx').casefold()}-<slug>"
        return deny(f"The active ticket requires branch {expected}.")

    if owner == "shared":
        shared = state["shared_write"]
        if not shared["approved"] or not _path_allowed(path, shared["paths"]):
            return deny(f"{path} is shared scope and lacks an exact human-approved shared_write path.")
        if not _path_allowed(path, state["allowed_write_paths"]):
            return deny(f"{path} is outside the active ticket allowed_write_paths list.")
        if state["phase"] not in {"implementation", "verification"}:
            return deny("Shared evidence or tooling writes require implementation or verification phase.")
        return {}

    if state["phase"] != "implementation":
        return deny("Product writes require phase=implementation.")
    if state["g0_gate"] != "passed" or state["contract_review"] != "pass":
        return deny("Product writes require approved G0 and contract_auditor PASS.")
    if not _path_allowed(path, state["allowed_write_paths"]):
        return deny(f"{path} is outside the active ticket allowed_write_paths list.")
    return {}


def apply_patch_decision(
    patch: str, root: Path, state: dict[str, Any], branch: str | None
) -> dict[str, Any]:
    entries = extract_patch_entries(patch)
    if not entries:
        return deny("apply_patch did not expose an explicit repository-relative file path; refusing fail-open behavior.")
    for action, raw_path in entries:
        try:
            path = normalize_relative_path(raw_path)
        except ValueError as exc:
            return deny(f"Invalid patch path {raw_path!r}: {exc}.")
        if action in {"delete", "move"} and not state.get("safety", {}).get(
            "destructive_files_authorized", False
        ):
            return deny("File deletion or move requires an explicit destructive_files authorization reference.")
        result = _path_write_decision(path, root, state, branch)
        if result:
            return result
    return {}


GIT_INVOCATION_RE = re.compile(
    r"(?is)(?:^|(?<=[;&|\r\n{}()]))\s*(?:(?:then|do|else)\s+)?"
    r"(?:(?:&|command|sudo)\s+|env(?:\.exe)?(?:\s+\w+=\S+)*\s+)*"
    r"(?:(?:[\"'](?:[^\"'\r\n]*[/\\])?git(?:\.exe|\.cmd)?[\"'])|"
    r"(?:[^\s;&|\"']*[/\\])?git(?:\.exe|\.cmd)?)"
    r"(?=\s|$)(?P<body>[^;&|\r\n{}()]*)"
)


def _git_invocation_bodies(command: str) -> list[str]:
    # Join explicit Bash and PowerShell line continuations before scanning.
    joined = re.sub(r"(?:`|\\)\r?\n\s*", " ", command)
    return [match.group("body") for match in GIT_INVOCATION_RE.finditer(joined)]


SHELL_TOKEN_RE = re.compile(r"""(?:"(?:\\.|[^"])*"|'(?:''|[^'])*'|[^\s]+)""")


def _shell_tokens(value: str) -> list[str]:
    tokens: list[str] = []
    for match in SHELL_TOKEN_RE.finditer(value):
        token = match.group(0)
        if len(token) >= 2 and token[0] == token[-1] and token[0] in {"'", '"'}:
            token = token[1:-1]
        tokens.append(token)
    return tokens


def _long_option_matches(token: str, full_option: str) -> bool:
    candidate = token.split("=", 1)[0]
    return (
        candidate.startswith("--")
        and len(candidate) > 2
        and full_option.startswith(candidate)
    )


def _has_long_option(tokens: Iterable[str], options: Iterable[str]) -> bool:
    return any(
        _long_option_matches(token, option)
        for token in tokens
        for option in options
    )


def _git_subcommand_and_suffix(body: str) -> tuple[str | None, list[str]]:
    tokens = _shell_tokens(body)
    value_options = {
        "-c",
        "-C",
        "--config-env",
        "--exec-path",
        "--git-dir",
        "--work-tree",
        "--namespace",
        "--super-prefix",
    }
    value_prefixes = tuple(option.casefold() + "=" for option in value_options if option.startswith("--"))
    index = 0
    while index < len(tokens):
        token = tokens[index]
        lowered = token.casefold()
        if token in value_options or lowered in {option.casefold() for option in value_options}:
            index += 2
            continue
        if lowered.startswith(value_prefixes) or token.startswith("-"):
            index += 1
            continue
        return lowered, tokens[index + 1 :]
    return None, []


def _git_has_command_scoped_config(body: str) -> bool:
    tokens = _shell_tokens(body)
    index = 0
    while index < len(tokens):
        token = tokens[index]
        lowered = token.casefold()
        if token == "-c" or (token.startswith("-c") and token != "-C"):
            return True
        if lowered in {"--config", "--config-env"} or lowered.startswith(
            ("--config=", "--config-env=")
        ):
            return True
        if token == "-C" or lowered in {
            "--exec-path",
            "--git-dir",
            "--work-tree",
            "--namespace",
            "--super-prefix",
        }:
            index += 2
            continue
        if token.startswith("-"):
            index += 1
            continue
        return False
    return False


def _git_global_options_are_safe(body: str) -> bool:
    tokens = _shell_tokens(body)
    for token in tokens:
        if token == "--no-pager":
            continue
        return not token.startswith("-")
    return False


def _git_has_global_no_pager(body: str) -> bool:
    found = False
    for token in _shell_tokens(body):
        if token == "--no-pager":
            found = True
            continue
        if token.startswith("-"):
            continue
        return found
    return False


def _has_exact_leading_options(suffix: list[str], required: tuple[str, ...]) -> bool:
    return suffix[: len(required)] == list(required)


def _safe_diff_projection(suffix: list[str]) -> bool:
    # Keep these first so no value-taking option can consume either safeguard.
    return _has_exact_leading_options(suffix, ("--no-ext-diff", "--no-textconv"))


def _safe_git_config_query(suffix: list[str]) -> bool:
    if not suffix:
        return False
    query = suffix[0]
    operands = suffix[1:]
    if any(not operand or operand.startswith("-") for operand in operands):
        return False
    if query in {"--list", "-l"}:
        return not operands
    if query in {"--get", "--get-all"}:
        return len(operands) == 1
    if query in {"--get-regexp", "--get-urlmatch"}:
        return len(operands) in {1, 2}
    return False


def _strict_commit_form(suffix: list[str]) -> bool:
    required = ("--no-gpg-sign", "--no-verify")
    if not _has_exact_leading_options(suffix, required):
        return False
    message = suffix[len(required) :]
    if len(message) == 2 and message[0] in {"-m", "--message"}:
        return bool(message[1])
    if len(message) != 1:
        return False
    token = message[0]
    if token.startswith("-m") and len(token) > 2:
        return True
    return token.startswith("--message=") and bool(token.removeprefix("--message="))


def _strict_positional_git_form(
    suffix: list[str],
    required: tuple[str, ...],
    minimum_positionals: int,
    maximum_positionals: int,
) -> bool:
    if not _has_exact_leading_options(suffix, required):
        return False
    positionals = suffix[len(required) :]
    return (
        minimum_positionals <= len(positionals) <= maximum_positionals
        and all(token and not token.startswith("-") for token in positionals)
    )


def _ticket_context_ready(
    state: dict[str, Any], branch: str | None, phases: set[str]
) -> bool:
    ticket = state.get("active_ticket")
    return (
        isinstance(ticket, str)
        and state.get("phase") in phases
        and state.get("g0_gate") == "passed"
        and state.get("contract_review") == "pass"
        and _ticket_branch_matches(branch, ticket)
        and state.get("verification", {}).get("ticket") == ticket
    )


def _verified_completion_context_ready(
    state: dict[str, Any], branch: str | None, *, require_stage_exit: bool = False
) -> bool:
    if not _ticket_context_ready(state, branch, {"complete"}):
        return False
    verification = state.get("verification", {})
    if verification.get("verifier") != "pass":
        return False
    return not require_stage_exit or verification.get("stage_exit_gate") == "pass"


def _git_body_has_word(body: str, value: str) -> bool:
    subcommand, _ = _git_subcommand_and_suffix(body)
    return subcommand == value.casefold()


def _git_bodies_for(command: str, subcommand: str) -> list[str]:
    return [body for body in _git_invocation_bodies(command) if _git_body_has_word(body, subcommand)]


def _has_git_subcommand(command: str, subcommand: str) -> bool:
    return bool(_git_bodies_for(command, subcommand))


def _git_suffix_tokens(body: str, subcommand: str) -> list[str]:
    actual, suffix = _git_subcommand_and_suffix(body)
    return suffix if actual == subcommand.casefold() else []


def _is_exact_planned_command(
    command: str, state: dict[str, Any], branch: str | None
) -> bool:
    if re.search(r"[;&|<>\r\n{}()\[\]*?@#$`'\"\\%^!]", command):
        return False
    tokens = _shell_tokens(command)
    if not tokens or "/" in tokens[0]:
        return False
    executable = tokens[0].casefold()
    for suffix in (".exe", ".cmd"):
        if executable.endswith(suffix):
            executable = executable[: -len(suffix)]
            break
    if executable not in PLANNED_RUNNERS:
        return False
    ticket = state.get("active_ticket")
    if state.get("phase") not in {"implementation", "verification"}:
        return False
    if not isinstance(ticket, str) or not isinstance(branch, str):
        return False
    expected_branch_prefix = f"codex/{ticket.casefold()}-"
    if not (
        branch.casefold().startswith(expected_branch_prefix)
        and len(branch) > len(expected_branch_prefix)
    ):
        return False
    commands = state.get("verification", {}).get("commands", [])
    return isinstance(commands, list) and command in (
        item.strip() for item in commands if isinstance(item, str)
    )


def _is_control_helper_command(command: str) -> bool:
    if re.search(r"[;&|\r\n{}()]|[\x60$]", command):
        return False
    tokens = _shell_tokens(command)
    if not tokens:
        return False
    executable = tokens[0].casefold().removesuffix(".exe")
    if executable not in {"py", "python", "python3"}:
        return False
    script_indexes = [
        index
        for index, token in enumerate(tokens)
        if token.replace("\\", "/").casefold() == ".codex/hooks/cogito_hooks.py"
    ]
    if len(script_indexes) != 1:
        return False
    script_index = script_indexes[0]
    return all(token in {"-3", "-B"} for token in tokens[1:script_index]) and tokens[
        script_index + 1 :
    ] in (["--fingerprint"], ["--validate-state"])


def _single_git_invocation(command: str) -> str | None:
    if re.search(r"[;&|<>\r\n{}()]", command):
        return None
    match = re.fullmatch(r"(?is)\s*git(?:\.exe)?(?=\s|$)(?P<body>.*)", command)
    if match is None:
        return None
    return match.group("body")


def _git_has_env_wrapper(command: str) -> bool:
    for match in GIT_INVOCATION_RE.finditer(command):
        wrapper = command[match.start() : match.start("body")]
        if re.search(r"(?i)(?:^|\s)env(?:\.exe)?(?=\s|$)", wrapper):
            return True
    return False


def _safe_read_only_git(body: str) -> bool:
    read_only = {
        "status",
        "diff",
        "show",
        "log",
        "rev-parse",
        "ls-files",
        "grep",
        "cat-file",
        "name-rev",
        "describe",
        "shortlog",
    }
    blocked_output_options = {
        "--output",
        "--output-directory",
        "--open-files-in-pager",
        "--ext-diff",
        "--filters",
        "--textconv",
    }
    tokens = _shell_tokens(body)
    if not _git_has_global_no_pager(body):
        return False
    if any(
        token.casefold() in blocked_output_options
        or token.casefold().startswith(
            (
                "--output=",
                "--output-directory=",
                "--open-files-in-pager=",
                "--ext-diff=",
                "--filters=",
                "--textconv=",
            )
        )
        for token in tokens
    ):
        return False
    subcommand, suffix = _git_subcommand_and_suffix(body)
    if subcommand == "grep" and any(token == "-O" or token.startswith("-O") for token in suffix):
        return False
    if subcommand in {"diff", "show", "log"} and not _safe_diff_projection(suffix):
        return False
    if subcommand in read_only:
        return True
    if subcommand == "branch":
        mutating_long = {
            "--delete",
            "--move",
            "--copy",
            "--force",
            "--set-upstream-to",
            "--unset-upstream",
            "--edit-description",
            "--track",
            "--no-track",
            "--recurse-submodules",
        }
        mutating_long_prefixes = (
            "--delete=",
            "--move=",
            "--copy=",
            "--force=",
            "--set-upstream-to=",
            "--create-reflog",
            "--track=",
        )
        for token in suffix:
            lowered = token.casefold()
            if lowered in mutating_long or lowered.startswith(mutating_long_prefixes):
                return False
            if _has_long_option((token,), mutating_long):
                return False
            if re.fullmatch(r"-[A-Za-z]*[dDmMcCfFu][A-Za-z]*", token):
                return False
        if not suffix:
            return True
        primary_query_options = {
            "--show-current",
            "--list",
            "-l",
            "--all",
            "-a",
            "--remotes",
            "-r",
            "--contains",
            "--no-contains",
            "--merged",
            "--no-merged",
            "--points-at",
        }
        query_prefixes = tuple(
            f"{option}=" for option in primary_query_options if option.startswith("--")
        )
        return any(
            token.casefold() in primary_query_options
            or token.casefold().startswith(query_prefixes)
            for token in suffix
        )
    if subcommand == "worktree":
        return [token.casefold() for token in suffix[:1]] == ["list"]
    if subcommand == "remote":
        lowered_suffix = [token.casefold() for token in suffix]
        if not lowered_suffix or lowered_suffix in (["-v"], ["--verbose"]):
            return True
        if lowered_suffix[0] != "get-url":
            return False
        options = [token for token in lowered_suffix[1:] if token.startswith("-")]
        positionals = [token for token in suffix[1:] if not token.startswith("-")]
        return all(token in {"--push", "--all"} for token in options) and len(positionals) == 1
    if subcommand == "config":
        return _safe_git_config_query(suffix)
    return False


def _normal_ticket_branch_git(body: str, state: dict[str, Any]) -> bool:
    ticket = state.get("active_ticket")
    if not isinstance(ticket, str):
        return False
    expected = f"codex/{ticket.casefold()}-"
    subcommand, suffix = _git_subcommand_and_suffix(body)
    if subcommand not in {"switch", "checkout", "branch"}:
        return False
    options = [token for token in suffix if token.startswith("-")]
    if subcommand == "branch" and options:
        return False
    if subcommand == "switch" and any(
        token not in {"-c", "--create"} for token in options
    ):
        return False
    if subcommand == "checkout" and any(token not in {"-b"} for token in options):
        return False
    targets = [token for token in suffix if not token.startswith("-")]
    return (
        len(targets) == 1
        and targets[0].casefold().startswith(expected)
        and len(targets[0]) > len(expected)
    )


def _allowed_git_command(
    command: str, state: dict[str, Any], branch: str | None
) -> bool:
    body = _single_git_invocation(command)
    if body is None:
        return False
    if not _git_global_options_are_safe(body):
        return False
    if _safe_read_only_git(body):
        return True
    publication = state.get("publication", {})
    safety = state.get("safety", {})
    if _git_body_has_word(body, "commit"):
        return bool(publication.get("commit_authorized"))
    if _git_body_has_word(body, "push"):
        return bool(publication.get("push_authorized")) and bool(branch)
    if any(
        _git_body_has_word(body, subcommand)
        for subcommand in ("merge", "rebase", "cherry-pick", "pull", "revert", "am")
    ):
        return bool(publication.get("merge_authorized"))
    if _git_body_has_word(body, "tag"):
        return bool(publication.get("release_authorized"))
    if any(
        _git_body_has_word(body, subcommand)
        for subcommand in ("reset", "clean", "restore", "stash")
    ):
        return bool(safety.get("destructive_git_authorized"))
    return _normal_ticket_branch_git(body, state)


def _safe_read_only_shell(command: str) -> bool:
    if re.search(r"[;&|\r\n{}()]|[\x60$]", command):
        return False
    tokens = _shell_tokens(command)
    if not tokens:
        return False
    executable = tokens[0].casefold().removesuffix(".exe").removesuffix(".cmd")
    read_only = {
        "rg",
        "get-content",
        "get-childitem",
        "get-item",
        "get-command",
        "get-filehash",
        "select-string",
        "test-path",
        "resolve-path",
        "measure-object",
        "get-location",
        "where",
        "pwd",
        "ls",
        "dir",
        "tree",
    }
    if executable == "rg" and any(token.casefold().startswith("--pre") for token in tokens[1:]):
        return False
    if executable == "tree" and any(
        token.casefold() == "-o"
        or _long_option_matches(token.casefold(), "--output")
        for token in tokens[1:]
    ):
        return False
    return executable in read_only


def _allowed_gh_command(command: str, state: dict[str, Any]) -> bool:
    if re.search(r"[;&|\r\n{}()]|[\x60$]", command):
        return False
    tokens = [token.casefold() for token in _shell_tokens(command)]
    if len(tokens) < 3 or tokens[0] not in {"gh", "gh.exe"}:
        return False
    publication = state.get("publication", {})
    if tokens[1:3] == ["release", "create"]:
        return bool(publication.get("release_authorized"))
    if tokens[1] == "pr" and tokens[2] in {"create", "merge"}:
        return bool(publication.get("pull_request_authorized"))
    return False


def _contains_disabled_gh_cli(command: str) -> bool:
    # Collapse ordinary Bash/PowerShell/CMD quote and escape concatenation so
    # direct CLI publication cannot be hidden inside an otherwise exact plan.
    collapsed = re.sub(r"[\"'`^]", "", command)
    candidates = (collapsed.replace("\\", ""), collapsed.replace("\\", "/"))
    pattern = re.compile(
        r"(?i)(?:^|[\s;&|(){}\\/])gh(?:\.exe|\.cmd)?(?=\s|[;&|(){}]|$)"
    )
    return any(pattern.search(candidate) for candidate in candidates)


def bash_decision(command: str, state: dict[str, Any], branch: str | None) -> dict[str, Any]:
    if not isinstance(command, str):
        return deny("Shell tool input is missing a string command.")
    normalized = command.strip()
    safety = state.get("safety", {})
    publication = state.get("publication", {})
    git_bodies = _git_invocation_bodies(normalized)

    if _contains_disabled_gh_cli(normalized):
        return deny(
            "GitHub CLI publication is disabled for Cogito++; use the connected GitHub app after an "
            "explicit user request and verified completion."
        )
    if git_bodies and re.search(r"[`$'\"\\%^!*?@\[\]]", normalized):
        return deny(
            "Git commands must be lexically literal; quotes, interpolation, shell escapes, glob patterns, "
            "and expansion characters are blocked before option parsing."
        )
    if any(_git_has_command_scoped_config(body) for body in git_bodies):
        return deny("Command-scoped Git configuration is opaque to safety checks and is blocked.")
    if any(
        _has_long_option(_shell_tokens(body), DANGEROUS_GIT_LONG_OPTIONS)
        for body in git_bodies
    ):
        return deny(
            "Git output, helper, editor, signing, strategy, help, and path-override options are blocked; "
            "abbreviated long options are treated the same as their full form."
        )
    if _git_has_env_wrapper(normalized):
        return deny("Environment-wrapped Git commands are opaque to helper and pager safety checks.")
    if any(not _git_global_options_are_safe(body) for body in git_bodies):
        return deny("Git global options other than --no-pager are blocked by the shell guard.")
    for body in git_bodies:
        subcommand, suffix = _git_subcommand_and_suffix(body)
        if subcommand in {"diff", "show", "log"} and (
            not _git_has_global_no_pager(body) or not _safe_diff_projection(suffix)
        ):
            return deny(
                f"git {subcommand} requires global --no-pager plus exact --no-ext-diff and "
                "--no-textconv safeguards."
            )
        if subcommand == "grep" and any(
            token == "-O" or token.startswith("-O") for token in suffix
        ):
            return deny("git grep pager execution is blocked.")

    if git_bodies:
        body = _single_git_invocation(normalized)
        if body is None:
            return deny("Git inspection must be one direct bare git/git.exe invocation.")
        if not _safe_read_only_git(body):
            return deny(
                "All Git mutations and publication are human-owned because repository hooks and configured "
                "helpers can execute outside the reviewed file allowlist."
            )
        return {}

    if re.search(r"(?<![<>=])>{1,2}(?![=])", normalized) or "<<" in normalized:
        return deny("Shell redirection is not allowed for repository work; use captured tool output and apply_patch.")

    opaque_evaluator = re.search(
        r"(?is)(?<![\w.-])(?:"
        r"cmd(?:\.exe)?\b[^;&|\r\n]*/(?:c|k)\b|"
        r"(?:powershell|pwsh)(?:\.exe)?\b[^;&|\r\n]*(?:-command|-encodedcommand)\b|"
        r"(?:py|python(?:\d+(?:\.\d+)?)?|node|ruby|perl|bash|sh|zsh)(?:\.exe)?\b"
        r"[^;&|\r\n]*(?:\s-(?:c|e)\b|--eval\b)|"
        r"invoke-expression\b|\biex\b|invoke-command\b|start-process\b"
        r")",
        normalized,
    )
    if opaque_evaluator:
        return deny("Inline or nested shell/interpreter execution is opaque to ownership checks and is blocked.")

    write_capable_runner = re.search(
        r"(?is)(?<![\w.-])(?:(?:powershell|pwsh)(?:\.exe)?\b[^;&|\r\n]*\s-(?:file|f)\b|"
        r"(?:py|python(?:\d+(?:\.\d+)?)?|node|ruby|perl|bash|sh|zsh)(?:\.exe)?\b|"
        r"(?:wscript|cscript|mshta|rundll32|regsvr32)(?:\.exe)?\b|"
        r"(?:cmake|ctest|ninja|msbuild|devenv|dotnet|cargo|npm|pnpm|yarn|vcpkg)(?:\.exe|\.cmd)?\b|"
        r"(?:&\s*)?(?:[\"'][^\"'\r\n]+\.(?:ps1|py|js|mjs|cjs|rb|pl|sh|bat|cmd|vbs|hta)[\"']|"
        r"[^\s;&|]+\.(?:ps1|py|js|mjs|cjs|rb|pl|sh|bat|cmd|vbs|hta)(?![\w.-]))"
        r")",
        normalized,
    )
    if (
        write_capable_runner
        and not _is_exact_planned_command(normalized, state, branch)
        and not _is_control_helper_command(normalized)
    ):
        return deny(
            "Script, build, test, and package runners must exactly match a command recorded during ticket preflight."
        )

    dotnet_file_api = re.search(
        r"(?i)(?:\[(?:system\.)?io\.(?:file|directory|fileinfo|directoryinfo|filestream|streamwriter)\]"
        r"|::(?:write|append|delete|move|copy|create|open|openwrite|replace|setattributes))",
        normalized,
    )
    if dotnet_file_api:
        return deny("Direct .NET filesystem APIs are blocked; use apply_patch for repository edits.")

    download_to_file = re.search(
        r"(?is)(?<![\w.-])(?:invoke-webrequest|iwr|curl|wget)(?:\.exe)?\b"
        r"[^;&|\r\n]*(?:-outfile\b|(?:^|\s)-(?:o|O)\s)",
        normalized,
    )
    if download_to_file:
        return deny("Shell downloads to files bypass repository ownership checks and are blocked.")

    destructive_git = (
        _has_git_subcommand(normalized, "reset")
        or (_has_git_subcommand(normalized, "clean") and re.search(r"(?i)(?:\s|^)-[^\s]*f", normalized))
        or (_has_git_subcommand(normalized, "checkout") and " -- " in f" {normalized} ")
        or (
            _has_git_subcommand(normalized, "checkout")
            and re.search(r"(?:^|\s)(?:-f|-B|--force)(?:\s|$)", normalized)
        )
        or (
            _has_git_subcommand(normalized, "switch")
            and re.search(
                r"(?:^|\s)(?:-f|-C|--force|--force-create|--discard-changes)(?:\s|$)",
                normalized,
            )
        )
        or _has_git_subcommand(normalized, "restore")
        or _has_git_subcommand(normalized, "stash")
        or (
            _has_git_subcommand(normalized, "commit")
            and any(
                _has_long_option(_git_suffix_tokens(body, "commit"), ("--amend",))
                for body in _git_bodies_for(normalized, "commit")
            )
        )
        or (
            _has_git_subcommand(normalized, "tag")
            and any(
                any(
                    token in {"-d", "-f"}
                    or re.fullmatch(r"-[A-Za-z]*[df][A-Za-z]*", token)
                    for token in _git_suffix_tokens(body, "tag")
                )
                or _has_long_option(
                    _git_suffix_tokens(body, "tag"), ("--delete", "--force")
                )
                for body in _git_bodies_for(normalized, "tag")
            )
        )
        or (
            _has_git_subcommand(normalized, "branch")
            and re.search(r"(?i)(?:^|\s)(?:-[dDfFmM]|--delete|--force|--move)(?:\s|$)", normalized)
        )
        or (_has_git_subcommand(normalized, "worktree") and re.search(r"(?i)\b(remove|prune)\b", normalized))
    )
    if destructive_git and not safety.get("destructive_git_authorized", False):
        return deny("Potentially destructive Git operation is not authorized by the user.")

    direct_file_mutation = re.search(
        r"(?i)(?<![\w.-])(?:set-content|add-content|clear-content|out-file|set-item|set-itemproperty|"
        r"new-item|new-itemproperty|copy-item|move-item|rename-item|remove-item|remove-itemproperty|"
        r"expand-archive|compress-archive|set-acl|tee-object|tee|del|erase|rmdir|rd|rm|mv|cp|"
        r"copy|move|ren|rename|truncate|touch|dd|install|mkdir|md|xcopy|robocopy|mklink|"
        r"rsync|fsutil|certutil|attrib|icacls|takeown|tar|7z|sc|ni|ri|mi|cpi|rni|si|sp|sac)"
        r"(?:\.exe)?(?![\w.-])",
        normalized,
    )
    in_place_editor = re.search(
        r"(?is)(?<![\w.-])sed(?:\.exe)?\b[^;&|\r\n]*(?:^|\s)"
        r"(?:-i(?:\S*)?|--in-place(?:=\S*)?)(?:\s|$)",
        normalized,
    )
    if direct_file_mutation or in_place_editor:
        if re.search(r"(?i)(?:remove-item|\bdel\b|\berase\b|\brmdir\b|(?:^|\s)rm\b)", normalized):
            if not safety.get("destructive_files_authorized", False):
                return deny("Filesystem deletion is not authorized by the user.")
        return deny("Use apply_patch for repository file edits so ownership and allowlist checks can inspect exact paths.")
    if any(_has_git_subcommand(normalized, sub) for sub in ("rm", "mv")):
        return deny("git rm and git mv bypass apply_patch path and deletion checks and are blocked.")
    if _has_git_subcommand(normalized, "apply") or re.search(r"(?i)(?:^|[;&|]\s*)patch(?:\.exe)?\b", normalized):
        return deny("Use apply_patch instead of an opaque shell patch command.")

    unsupported_git_mutation = next(
        (
            sub
            for sub in (
                "update-ref",
                "symbolic-ref",
                "filter-branch",
                "replace",
                "notes",
                "checkout-index",
                "read-tree",
                "commit-tree",
                "mktag",
                "fast-import",
            )
            if _has_git_subcommand(normalized, sub)
        ),
        None,
    )
    if unsupported_git_mutation:
        return deny(f"git {unsupported_git_mutation} mutates refs outside the reviewed workflow and is blocked.")

    protected_ref_rewrite = (
        (
            _has_git_subcommand(normalized, "branch")
            and re.search(r"(?i)(?:^|\s)(?:-[fM]|--force|--move)(?:\s|$)", normalized)
        )
        or (
            _has_git_subcommand(normalized, "switch")
            and re.search(r"(?i)(?:^|\s)(?:-C|--force-create)(?:\s|$)", normalized)
        )
        or (
            _has_git_subcommand(normalized, "checkout")
            and re.search(r"(?i)(?:^|\s)-B(?:\s|$)", normalized)
        )
    ) and re.search(r"(?i)(?:^|\s)(?:refs/heads/)?(?:main|master)(?:\s|$)", normalized)
    if protected_ref_rewrite:
        return deny("Rewriting the protected main/master ref is forbidden.")

    commit_bodies = _git_bodies_for(normalized, "commit")
    if commit_bodies:
        if not _ticket_context_ready(
            state, branch, {"implementation", "verification", "complete"}
        ):
            return deny(
                "git commit requires an approved active ticket and its exact codex/<ticket>-<slug> branch."
            )
        if not publication.get("commit_authorized", False):
            return deny("git commit is not authorized; the user must explicitly request it.")
        for body in commit_bodies:
            suffix = _git_suffix_tokens(body, "commit")
            if not _strict_commit_form(suffix):
                return deny(
                    "Authorized commits require the exact noninteractive form: --no-gpg-sign --no-verify "
                    "followed by one -m/--message value; pathspecs and all other options are blocked."
                )
    push_bodies = _git_bodies_for(normalized, "push")
    if push_bodies:
        push_text = " ".join(push_bodies)
        if not _verified_completion_context_ready(state, branch):
            return deny(
                "git push requires a verified complete ticket on its exact codex/<ticket>-<slug> branch."
            )
        if re.search(r"(?i)(?:^|\s)(?:--mirror|--all|--delete|-d|--prune)(?:\s|$)", push_text):
            return deny("Mirror, all-ref, delete, and prune pushes are forbidden.")
        if re.search(
            r"(?i)(?:^|\s)(?:--force(?:-with-lease)?(?:=\S*)?|-[A-Za-z]*f[A-Za-z]*)(?:\s|$)",
            push_text,
        ) or re.search(
            r"(?:^|\s)\+\S+", push_text
        ):
            return deny("Force-push is forbidden.")
        if re.search(
            r"(?i)(?:^|\s)(?:\+?[^\s:]*:)?(?:refs/heads/)?(?:main|master)(?:\s|$)", push_text
        ) or re.search(r"(?i):(?:refs/heads/)?(?:main|master)(?:\s|$)", push_text):
            return deny("Direct push to main/master is forbidden.")
        if re.search(r"(?i)(?:^|\s)--(?:follow-)?tags(?:\s|$)", push_text) and not publication.get(
            "release_authorized", False
        ):
            return deny("Pushing tags requires explicit human release authority.")
        for body in push_bodies:
            suffix = _git_suffix_tokens(body, "push")
            allowed_options = {
                "-u",
                "--set-upstream",
                "-q",
                "--quiet",
                "-v",
                "--verbose",
                "--dry-run",
                "--porcelain",
            }
            options = [token for token in suffix if token.startswith("-")]
            if any(token not in allowed_options for token in options):
                return deny("git push contains an option outside the reviewed single-branch form.")
            positionals = [token for token in suffix if not token.startswith("-")]
            if len(positionals) != 2:
                return deny(
                    "git push requires one explicit remote and one explicit single-branch refspec."
                )
            remote, refspec = positionals
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", remote):
                return deny("git push requires a configured remote name, not a URL or remote helper.")
            if refspec.startswith(":"):
                return deny("A push refspec with an empty source deletes the remote ref and is forbidden.")
            if refspec.count(":") > 1:
                return deny("git push refspec must name one source and one destination.")
            if ":" in refspec:
                source, destination = refspec.split(":", 1)
            else:
                source = destination = refspec
            branch_ref = f"refs/heads/{branch}"
            if source not in {branch, branch_ref, "HEAD"} or destination not in {
                branch,
                branch_ref,
            }:
                return deny("git push refspec must target the current reviewed ticket branch exactly.")
        if not publication.get("push_authorized", False):
            return deny("git push is not authorized; the user must explicitly request it.")
    if any(
        _has_git_subcommand(normalized, sub)
        for sub in ("merge", "rebase", "cherry-pick", "pull", "revert", "am")
    ):
        if not _ticket_context_ready(state, branch, {"implementation", "verification"}):
            return deny(
                "Git integration requires an approved active ticket on its exact codex/<ticket>-<slug> branch."
            )
        if not publication.get("merge_authorized", False):
            return deny(
                "Merge, rebase, cherry-pick, pull, revert, and am require explicit user authorization."
            )
        integration_shapes = {
            "merge": (("--no-edit", "--no-verify", "--no-gpg-sign"), 1, 1),
            "pull": (("--no-edit", "--no-verify", "--no-gpg-sign"), 2, 2),
            "rebase": (("--no-verify", "--no-gpg-sign"), 1, 1),
            "cherry-pick": (("--no-gpg-sign",), 1, 1),
            "revert": (("--no-edit", "--no-gpg-sign"), 1, 1),
        }
        if _git_bodies_for(normalized, "am"):
            return deny("git am executes an opaque patch-and-hook workflow and is not enabled for Codex.")
        for subcommand, (required, minimum, maximum) in integration_shapes.items():
            for body in _git_bodies_for(normalized, subcommand):
                suffix = _git_suffix_tokens(body, subcommand)
                if not _strict_positional_git_form(suffix, required, minimum, maximum):
                    return deny(
                        f"Authorized git {subcommand} must use the exact canonical noninteractive safeguards "
                        "followed only by its reviewed positional target(s)."
                    )
                if subcommand == "pull":
                    remote = suffix[len(required)]
                    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", remote):
                        return deny("git pull requires a configured remote name, not a URL or remote helper.")
    release_create = re.search(r"(?i)\bgh(?:\.exe)?\s+release\s+create\b", normalized)
    tag_bodies = _git_bodies_for(normalized, "tag")
    if tag_bodies or release_create:
        if not _verified_completion_context_ready(state, branch, require_stage_exit=True):
            return deny(
                "Tags and releases require a verified complete ticket and stage Exit Gate PASS on its exact branch."
            )
        if not publication.get("release_authorized", False):
            return deny("Tags and releases require explicit human release authority.")
        for body in tag_bodies:
            suffix = _git_suffix_tokens(body, "tag")
            if not _strict_positional_git_form(suffix, ("--no-sign",), 1, 1):
                return deny(
                    "Authorized Codex tags must use exact lightweight form --no-sign <tag> at verified HEAD; "
                    "annotated, signed, forced, deleted, and editor-driven forms are blocked."
                )
    if re.search(r"(?i)\bgh(?:\.exe)?\s+pr\s+(?:create|merge)\b", normalized):
        if not publication.get("pull_request_authorized", False):
            return deny("Creating or merging a pull request requires an explicit user request.")
    if _is_control_helper_command(normalized):
        return {}
    if git_bodies:
        if _allowed_git_command(normalized, state, branch):
            return {}
        return deny("Git commands must match a safe read form or an explicitly authorized operation.")
    if re.search(r"(?i)(?:^|[;&|\r\n{}()]\s*)gh(?:\.exe)?(?=\s|$)", normalized):
        if _allowed_gh_command(normalized, state):
            return {}
        return deny("GitHub CLI commands are not enabled by a verification-plan entry.")
    if _is_exact_planned_command(normalized, state, branch):
        return {}
    if _safe_read_only_shell(normalized):
        return {}
    return deny(
        "Unplanned shell commands are denied; use a simple read-only inspection, apply_patch, "
        "or an exact command approved in the ticket verification plan."
    )


def _direct_write_paths(tool_input: Any) -> list[str]:
    if not _is_mapping(tool_input):
        return []
    paths: list[str] = []
    for key in ("file_path", "path", "target_path", "destination", "dest"):
        value = tool_input.get(key)
        if isinstance(value, str):
            paths.append(value)
        elif isinstance(value, list):
            paths.extend(item for item in value if isinstance(item, str))
    return paths


def pre_tool_decision(
    payload: dict[str, Any], root: Path, state: dict[str, Any], branch: str | None
) -> dict[str, Any]:
    if OWNERSHIP_POLICY_ERROR:
        return deny(f"Cogito++ ownership policy is unavailable: {OWNERSHIP_POLICY_ERROR}")
    if payload.get("agent_id") or payload.get("agent_type") or payload.get("is_subagent"):
        return deny("Subagents are read-only auditors; only the root agent may invoke write-capable tools.")
    tool_name = str(payload.get("tool_name", ""))
    tool_input = payload.get("tool_input")
    if tool_name.casefold() in {"bash", "shell_command", "exec_command"}:
        command = tool_input.get("command") if _is_mapping(tool_input) else None
        result = bash_decision(command, state, branch)
        if result or not isinstance(command, str):
            return result
        if _has_git_subcommand(command, "commit"):
            return _staged_commit_decision(root, state, branch)
        if any(_has_git_subcommand(command, sub) for sub in ("push", "tag")) or re.search(
            r"(?i)\bgh(?:\.exe)?\s+(?:release\s+create|pr\s+(?:create|merge))\b", command
        ):
            try:
                validate_evidence(root, state)
            except (OSError, StateError, ValueError) as exc:
                return deny(f"Publication evidence is incomplete or stale: {exc}.")
        return {}
    if tool_name.casefold() in {"apply_patch", "edit", "write"}:
        if _is_mapping(tool_input) and isinstance(tool_input.get("command"), str):
            return apply_patch_decision(tool_input["command"], root, state, branch)
        paths = _direct_write_paths(tool_input)
        if not paths:
            return deny("Write tool input did not expose an inspectable repository-relative path.")
        for raw_path in paths:
            try:
                path = normalize_relative_path(raw_path)
            except ValueError as exc:
                return deny(f"Invalid write path {raw_path!r}: {exc}.")
            result = _path_write_decision(path, root, state, branch)
            if result:
                return result
    return {}


def _run_git(root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), *args],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=GIT_QUERY_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip()


def current_branch(root: Path) -> str | None:
    value = _run_git(root, "branch", "--show-current")
    return value or None


def git_head(root: Path) -> str:
    return _run_git(root, "rev-parse", "HEAD") or "NO_GIT_HEAD"


def _run_git_bytes(root: Path, *args: str, timeout: int = GIT_STATUS_TIMEOUT_SECONDS) -> bytes:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), *args],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError):
        raise StateError(f"git {' '.join(args[:2])} failed; completion checks fail closed")
    return result.stdout


def _porcelain_paths(data: bytes) -> list[str]:
    records = data.decode("utf-8", errors="surrogateescape").split("\x00")
    paths: list[str] = []
    index = 0
    while index < len(records):
        record = records[index]
        index += 1
        if not record:
            continue
        status = record[:2]
        raw_path = record[3:] if len(record) >= 4 else ""
        if raw_path:
            try:
                paths.append(normalize_relative_path(raw_path))
            except ValueError:
                paths.append(raw_path.replace("\\", "/"))
        if any(code in status for code in ("R", "C")) and index < len(records):
            second = records[index]
            index += 1
            if second:
                try:
                    paths.append(normalize_relative_path(second))
                except ValueError:
                    paths.append(second.replace("\\", "/"))
    return paths


def _nul_paths(data: bytes) -> list[str]:
    return [
        value
        for value in data.decode("utf-8", errors="surrogateescape").split("\x00")
        if value
    ]


def _staged_paths(root: Path) -> list[str]:
    raw_paths = _nul_paths(
        _run_git_bytes(
            root,
            "diff",
            "--cached",
            "--name-only",
            "-z",
            "--no-renames",
            "--no-ext-diff",
            "--no-textconv",
            "--",
        )
    )
    normalized: list[str] = []
    for path in raw_paths:
        try:
            normalized.append(normalize_relative_path(path))
        except ValueError as exc:
            raise StateError(f"staged path is unsafe: {path!r}: {exc}") from exc
    return sorted(set(normalized), key=str.casefold)


def _staged_commit_decision(
    root: Path, state: dict[str, Any], branch: str | None
) -> dict[str, Any]:
    try:
        paths = _staged_paths(root)
    except StateError as exc:
        return deny(f"Cannot prove staged commit scope: {exc}.")
    if not paths:
        return deny("Authorized git commit requires at least one staged path in the active ticket scope.")
    for path in paths:
        owner = owner_for_path(path)
        if owner not in {"codex-product", "shared"}:
            return deny(f"Staged path {path} is outside Codex ticket product scope ({owner}).")
        if not _path_allowed(path, state.get("allowed_write_paths", [])):
            return deny(f"Staged path {path} is outside the active ticket allowed_write_paths list.")
        result = _path_write_decision(path, root, state, branch)
        if result:
            return result
    return {}


def git_changed_paths(root: Path, baseline_head_oid: str | None = None) -> list[str]:
    paths = _porcelain_paths(
        _run_git_bytes(root, "status", "--porcelain=v1", "-z", "--untracked-files=all")
    )
    if baseline_head_oid:
        _run_git_bytes(root, "cat-file", "-e", f"{baseline_head_oid}^{{commit}}")
        paths.extend(
            _nul_paths(
                _run_git_bytes(
                    root,
                    "diff",
                    "--no-renames",
                    "--name-only",
                    "-z",
                    baseline_head_oid,
                    "--",
                )
            )
        )
    paths.extend(
        _nul_paths(
            _run_git_bytes(
                root,
                "ls-files",
                "--others",
                "--ignored",
                "--exclude-standard",
                "-z",
                "--",
                *GUARDED_PATHSPECS,
            )
        )
    )
    normalized: list[str] = []
    for path in paths:
        try:
            normalized.append(normalize_relative_path(path))
        except ValueError:
            normalized.append(path.replace("\\", "/"))
    return sorted(set(normalized), key=str.casefold)


def _safe_resolve(root: Path, relative: str) -> Path:
    normalized = normalize_relative_path(relative)
    candidate = (root / Path(*PurePosixPath(normalized).parts)).resolve(strict=False)
    root_resolved = root.resolve(strict=False)
    try:
        candidate.relative_to(root_resolved)
    except ValueError as exc:
        raise StateError(f"{relative} resolves outside the repository") from exc
    return candidate


def _scope_files(root: Path, state: dict[str, Any]) -> list[tuple[str, Path | None]]:
    files: dict[str, Path | None] = {}
    shared = state.get("shared_write", {})
    shared_paths = shared.get("paths", []) if shared.get("approved") else []
    evidence_raw = state.get("verification", {}).get("evidence_dir")
    evidence_boundary = (
        normalize_relative_path(evidence_raw).rstrip("/") + "/"
        if isinstance(evidence_raw, str) and evidence_raw.strip()
        else None
    )

    def included(relative: str) -> bool:
        if evidence_boundary and path_is_within(relative, evidence_boundary):
            return False
        owner = owner_for_path(relative)
        return owner == "codex-product" or (
            owner == "shared" and _path_allowed(relative, shared_paths)
        )

    for boundary in state.get("allowed_write_paths", []):
        normalized = normalize_relative_path(boundary, keep_directory_hint=True)
        if (
            evidence_boundary
            and normalized.rstrip("/").casefold()
            == evidence_boundary.rstrip("/").casefold()
        ):
            continue
        if not normalized.endswith("/") and not included(normalized):
            continue
        candidate = _safe_resolve(root, normalized.rstrip("/"))
        if candidate.is_symlink():
            raise StateError(f"symlinked allowlist entry is not fingerprinted: {normalized}")
        if candidate.is_file():
            if normalized.endswith("/"):
                raise StateError(f"directory allowlist entry resolves to a file: {normalized}")
            files[normalized.rstrip("/")] = candidate
        elif candidate.is_dir():
            if not normalized.endswith("/"):
                raise StateError(f"file allowlist entry resolves to a directory: {normalized}")
            for item in candidate.rglob("*"):
                if item.is_symlink():
                    raise StateError(f"symlink inside allowlist is not fingerprinted: {item}")
                if item.is_file():
                    rel = item.relative_to(root).as_posix()
                    if included(rel):
                        files[rel] = item
        else:
            files[normalized.rstrip("/")] = None
    return sorted(files.items(), key=lambda item: item[0].casefold())


def _update_digest_frame(digest: Any, record_type: bytes, payload: bytes) -> None:
    if len(record_type) != 1:
        raise ValueError("digest record type must be one byte")
    digest.update(record_type)
    digest.update(len(payload).to_bytes(8, "big"))
    digest.update(payload)


def _framed_scope_digest(
    contract_bytes: bytes, records: Iterable[tuple[str, bytes | None]]
) -> str:
    digest = hashlib.sha256()
    _update_digest_frame(digest, b"C", contract_bytes)
    for relative, content in records:
        _update_digest_frame(digest, b"P", relative.encode("utf-8"))
        if content is None:
            _update_digest_frame(digest, b"M", b"")
        else:
            _update_digest_frame(digest, b"F", content)
    return digest.hexdigest()


def scope_identity(root: Path, state: dict[str, Any]) -> dict[str, str]:
    errors = validate_state(state)
    if errors:
        raise StateError("cannot fingerprint invalid state: " + "; ".join(errors))
    contract = {
        "active_ticket": state.get("active_ticket"),
        "baseline_head_oid": state.get("baseline_head_oid"),
        "g0_gate": state.get("g0_gate"),
        "contract_review": state.get("contract_review"),
        "contract_reference": state.get("contract_reference"),
        "allowed_write_paths": sorted(state.get("allowed_write_paths", []), key=str.casefold),
        "shared_write": {
            "approved": state.get("shared_write", {}).get("approved"),
            "paths": sorted(state.get("shared_write", {}).get("paths", []), key=str.casefold),
            "reference": state.get("shared_write", {}).get("reference"),
        },
        "verification_plan": {
            field: state.get("verification", {}).get(field)
            for field in ("ticket", "commands", "evidence_dir", "manifest_file", "verdict_file")
        },
    }
    contract_bytes = json.dumps(
        contract, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    records = (
        (relative, None if file_path is None else file_path.read_bytes())
        for relative, file_path in _scope_files(root, state)
    )
    return {
        "head_oid": git_head(root),
        "workspace_fingerprint": f"sha256:{_framed_scope_digest(contract_bytes, records)}",
    }


def _read_nonempty_json(path: Path, label: str) -> dict[str, Any]:
    try:
        if not path.is_file() or path.stat().st_size == 0:
            raise StateError(f"{label} is missing or empty: {path}")
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise StateError(f"cannot read {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise StateError(f"{label} must contain a JSON object")
    return value


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return f"sha256:{digest.hexdigest()}"


def _acceptance_ids(
    value: Any,
    label: str,
    *,
    root: Path | None = None,
    evidence_dir: Path | None = None,
    evidence_files: dict[str, Path] | None = None,
) -> list[str]:
    if not isinstance(value, list) or not value:
        raise StateError(f"{label} acceptance mapping is empty")
    identifiers: list[str] = []
    for index, clause in enumerate(value, start=1):
        if not isinstance(clause, dict) or clause.get("status") != "PASS":
            raise StateError(f"{label} acceptance clause {index} is not PASS")
        identifier = clause.get("id")
        if not isinstance(identifier, str) or not identifier.strip():
            raise StateError(f"{label} acceptance clause {index} has no stable id")
        if identifier in identifiers:
            raise StateError(f"{label} acceptance clause id {identifier} is duplicated")
        identifiers.append(identifier)
        if root is not None and evidence_dir is not None:
            evidence = clause.get("evidence")
            if not isinstance(evidence, list) or not evidence:
                raise StateError(f"{label} acceptance clause {index} has no evidence")
            for evidence_path in evidence:
                path = _evidence_file(
                    root, evidence_dir, evidence_path, f"{label} acceptance clause {index}"
                )
                if evidence_files is not None:
                    normalized = normalize_relative_path(evidence_path)
                    evidence_files[normalized] = path
    return identifiers


def _evidence_file(root: Path, evidence_dir: Path, relative: Any, label: str) -> Path:
    if not isinstance(relative, str) or not relative.strip():
        raise StateError(f"{label} must be a non-empty relative path")
    candidate = _safe_resolve(root, relative)
    try:
        candidate.relative_to(evidence_dir)
    except ValueError as exc:
        raise StateError(f"{label} must remain inside evidence_dir") from exc
    if not candidate.is_file() or candidate.stat().st_size == 0:
        raise StateError(f"{label} evidence is missing or empty: {relative}")
    return candidate


def _validate_evidence_digests(
    manifest: dict[str, Any],
    referenced: dict[str, Path],
    *,
    forbidden: set[str],
) -> None:
    if forbidden.intersection(referenced):
        raise StateError("manifest or verdict cannot be self-referenced as acceptance evidence")
    declared = manifest.get("evidence_files")
    if not isinstance(declared, dict) or not declared:
        raise StateError("manifest evidence_files digest mapping is missing")
    normalized_declared: dict[str, str] = {}
    for raw_path, digest in declared.items():
        if not isinstance(raw_path, str) or normalize_relative_path(raw_path) != raw_path:
            raise StateError("manifest evidence_files contains a non-canonical path")
        if raw_path.casefold() in (path.casefold() for path in normalized_declared):
            raise StateError("manifest evidence_files contains a case-insensitive duplicate path")
        if not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
            raise StateError(f"manifest evidence digest is malformed: {raw_path}")
        normalized_declared[raw_path] = digest
    if set(normalized_declared) != set(referenced):
        raise StateError("manifest evidence_files keys differ from command and acceptance evidence")
    for relative, path in referenced.items():
        if normalized_declared[relative] != _sha256_file(path):
            raise StateError(f"evidence digest changed after independent verification: {relative}")


def validate_evidence(root: Path, state: dict[str, Any]) -> None:
    ticket = state["active_ticket"]
    verification = state["verification"]
    if verification.get("verifier") != "pass" or verification.get("ticket") != ticket:
        raise StateError("independent verifier PASS is absent or bound to another ticket")
    if not verification.get("commands"):
        raise StateError("verification.commands is empty")
    evidence_dir_raw = verification.get("evidence_dir")
    if not isinstance(evidence_dir_raw, str) or not path_is_within(
        normalize_relative_path(evidence_dir_raw), "artifacts/verification/"
    ):
        raise StateError("evidence_dir must be under artifacts/verification/")
    evidence_dir = _safe_resolve(root, evidence_dir_raw)
    if not evidence_dir.is_dir():
        raise StateError("evidence_dir does not exist")

    current = scope_identity(root, state)
    if verification.get("head_oid") != current["head_oid"]:
        raise StateError("HEAD changed after independent verification")
    if verification.get("workspace_fingerprint") != current["workspace_fingerprint"]:
        raise StateError("allowlisted product files changed after independent verification")

    manifest_path = _evidence_file(root, evidence_dir, verification.get("manifest_file"), "manifest_file")
    verdict_path = _evidence_file(root, evidence_dir, verification.get("verdict_file"), "verdict_file")
    manifest = _read_nonempty_json(manifest_path, "evidence manifest")
    verdict = _read_nonempty_json(verdict_path, "verifier verdict")

    for document, label in ((manifest, "manifest"), (verdict, "verdict")):
        if document.get("ticket") != ticket:
            raise StateError(f"{label} is bound to another ticket")
        if document.get("head_oid") != current["head_oid"]:
            raise StateError(f"{label} HEAD binding is stale")
        if document.get("workspace_fingerprint") != current["workspace_fingerprint"]:
            raise StateError(f"{label} fingerprint binding is stale")

    manifest_commands = manifest.get("commands")
    if not isinstance(manifest_commands, list) or not manifest_commands:
        raise StateError("manifest commands are missing")
    command_texts: list[str] = []
    referenced_evidence: dict[str, Path] = {}
    for index, command in enumerate(manifest_commands, start=1):
        if not isinstance(command, dict) or not isinstance(command.get("command"), str):
            raise StateError(f"manifest command {index} is malformed")
        if command.get("exit_code") != 0:
            raise StateError(f"manifest command {index} did not exit 0")
        output_relative = command.get("output_file")
        output_path = _evidence_file(
            root, evidence_dir, output_relative, f"command {index} output"
        )
        referenced_evidence[normalize_relative_path(output_relative)] = output_path
        command_texts.append(command["command"])
    if command_texts != verification["commands"]:
        raise StateError("state commands and manifest commands differ")

    manifest_acceptance_ids = _acceptance_ids(
        manifest.get("acceptance"),
        "manifest",
        root=root,
        evidence_dir=evidence_dir,
        evidence_files=referenced_evidence,
    )
    if not isinstance(manifest.get("toolchain"), dict) or not manifest["toolchain"]:
        raise StateError("manifest toolchain evidence is missing")
    if not isinstance(manifest.get("redaction"), dict) or manifest["redaction"].get("status") != "PASS":
        raise StateError("manifest redaction result is not PASS")

    _validate_evidence_digests(
        manifest,
        referenced_evidence,
        forbidden={
            normalize_relative_path(verification["manifest_file"]),
            normalize_relative_path(verification["verdict_file"]),
        },
    )

    if verdict.get("verdict") != "PASS":
        raise StateError("verifier verdict file is not PASS")
    if verdict.get("manifest_sha256") != _sha256_file(manifest_path):
        raise StateError("verifier verdict is not bound to the exact evidence manifest bytes")
    if verdict.get("stage_exit_gate") != verification.get("stage_exit_gate"):
        raise StateError("stage Exit Gate result differs between state and verdict")
    verdict_acceptance_ids = _acceptance_ids(verdict.get("acceptance"), "verdict")
    if verdict_acceptance_ids != manifest_acceptance_ids:
        raise StateError("manifest and verifier verdict acceptance clause ids or order differ")


def completion_decision(
    payload: dict[str, Any],
    root: Path,
    state: dict[str, Any],
    changed_paths: list[str],
    branch: str | None = None,
) -> dict[str, Any]:
    del payload
    if OWNERSHIP_POLICY_ERROR:
        return {
            "decision": "block",
            "reason": f"Cogito++ ownership policy is unavailable: {OWNERSHIP_POLICY_ERROR}",
        }
    try:
        normalized_paths = [normalize_relative_path(path) for path in changed_paths]
    except ValueError as exc:
        return {"decision": "block", "reason": f"Uninspectable changed path: {exc}."}

    errors = validate_state(state)
    if errors:
        return {"decision": "block", "reason": "Invalid stage state: " + "; ".join(errors)}

    cross_owner = [path for path in normalized_paths if owner_for_path(path) in {"claude", "antigravity"}]
    if cross_owner:
        owners = ", ".join(f"{path} ({owner_for_path(path)}-owned)" for path in cross_owner[:8])
        return {"decision": "block", "reason": f"Cross-owner worktree changes must be isolated before completion: {owners}."}

    shared = state.get("shared_write", {}) if isinstance(state, dict) else {}
    changed_shared = [path for path in normalized_paths if owner_for_path(path) == "shared"]
    outside_ticket_shared = [
        path for path in changed_shared if not _path_allowed(path, state.get("allowed_write_paths", []))
    ]
    if outside_ticket_shared:
        return {
            "decision": "block",
            "reason": "Changed shared paths are outside the ticket allowlist: "
            + ", ".join(outside_ticket_shared[:8]),
        }
    unauthorized_shared = [
        path
        for path in changed_shared
        if not shared.get("approved") or not _path_allowed(path, shared.get("paths", []))
    ]
    if unauthorized_shared:
        return {
            "decision": "block",
            "reason": "Shared paths lack exact human scope approval: " + ", ".join(unauthorized_shared[:8]),
        }

    product_paths = [path for path in normalized_paths if owner_for_path(path) == "codex-product"]
    phase = state.get("phase")
    if phase == "idle":
        if product_paths:
            return {"decision": "block", "reason": "Idle state cannot finish with product changes."}
        return {}

    if phase == "preflight":
        if product_paths:
            return {"decision": "block", "reason": "Preflight cannot finish with product changes."}
        return {}

    if phase == "blocked":
        if product_paths and not _ticket_branch_matches(branch, state.get("active_ticket")):
            return {"decision": "block", "reason": "Blocked product work is not on the active ticket branch."}
        outside = [path for path in product_paths if not _path_allowed(path, state["allowed_write_paths"])]
        if outside:
            return {"decision": "block", "reason": "Blocked product paths are outside the ticket allowlist: " + ", ".join(outside)}
        return {}

    if phase in {"implementation", "verification"}:
        return {
            "decision": "block",
            "reason": f"Active ticket phase={phase} cannot finish before independent PASS and phase=complete.",
        }
    if phase != "complete":
        return {"decision": "block", "reason": f"Unsupported completion phase: {phase}."}
    if not _ticket_branch_matches(branch, state.get("active_ticket")):
        return {"decision": "block", "reason": "Product completion is not on the active ticket branch."}
    outside = [path for path in product_paths if not _path_allowed(path, state["allowed_write_paths"])]
    if outside:
        return {"decision": "block", "reason": "Changed product paths are outside the ticket allowlist: " + ", ".join(outside)}
    try:
        validate_evidence(root, state)
    except (OSError, StateError, ValueError) as exc:
        return {"decision": "block", "reason": f"Exit Gate evidence is incomplete or stale: {exc}."}
    return {}


def find_repo_root(start: Path) -> Path:
    candidate = start.resolve(strict=False)
    for path in (candidate, *candidate.parents):
        if (path / ".git").exists() and (path / "AGENTS.md").exists():
            return path
    git_root = _run_git(candidate, "rev-parse", "--show-toplevel")
    if git_root:
        return Path(git_root).resolve(strict=False)
    raise StateError(f"cannot locate Cogito++ repository root from {start}")


def find_repo_root_with_script_fallback(start: Path) -> Path:
    """Resolve the repository even when a Windows host loses the Unicode cwd."""
    try:
        return find_repo_root(start)
    except StateError as original_error:
        try:
            return find_repo_root(Path(__file__).resolve().parent)
        except StateError:
            raise original_error


def _context_output(event: str, context: str) -> dict[str, Any]:
    return {"hookSpecificOutput": {"hookEventName": event, "additionalContext": context}}


def _session_context(state: dict[str, Any], branch: str | None) -> str:
    ticket = state.get("active_ticket") or "none"
    return (
        f"Cogito++ state: ticket={ticket}, phase={state.get('phase')}, branch={branch or 'unknown'}, "
        f"G0={state.get('g0_gate')}, contract_review={state.get('contract_review')}. "
        "The root agent is the sole writer; subagents are read-only auditors. "
        "Implement exactly one approved ticket, keep writes inside the exact allowlist, and do not commit, "
        "push, merge, tag, sign, or open a PR without an explicit user request."
    )


def _emit(value: dict[str, Any]) -> None:
    if value:
        sys.stdout.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")))


def run_hook(payload: dict[str, Any]) -> int:
    event = str(payload.get("hook_event_name") or "")
    if event == "SessionEnd":
        return 0
    cwd = Path(str(payload.get("cwd") or Path.cwd()))
    try:
        # Windows shell pipelines can replace non-ASCII cwd bytes under a
        # legacy console code page. The checked-in script location remains an
        # authoritative repository-local fallback.
        root = find_repo_root_with_script_fallback(cwd)
        state = load_state(root)
    except StateError as exc:
        if event == "PreToolUse":
            _emit(deny(f"Cogito++ control state is unavailable: {exc}"))
            return 0
        if event == "Stop":
            _emit({"decision": "block", "reason": f"Cogito++ control state is unavailable: {exc}"})
            return 0
        _emit({"systemMessage": f"Cogito++ control state is unavailable: {exc}"})
        return 0

    branch = current_branch(root)
    if OWNERSHIP_POLICY_ERROR and event not in {"PreToolUse", "Stop"}:
        _emit({"systemMessage": f"Cogito++ ownership policy is unavailable: {OWNERSHIP_POLICY_ERROR}"})
        return 0
    if event == "SessionStart":
        _emit(_context_output("SessionStart", _session_context(state, branch)))
    elif event == "SubagentStart":
        agent_type = str(payload.get("agent_type") or "unknown")
        context = (
            f"Cogito++ subagent boundary for {agent_type}: work read-only; do not edit files, change state, "
            "commit, push, or repair the implementation. Return evidence and a bounded verdict to the root writer."
        )
        _emit(_context_output("SubagentStart", context))
    elif event == "PreToolUse":
        _emit(pre_tool_decision(payload, root, state, branch))
    elif event == "Stop":
        try:
            changed = git_changed_paths(root, state.get("baseline_head_oid"))
        except StateError as exc:
            _emit({"decision": "block", "reason": str(exc)})
        else:
            _emit(completion_decision(payload, root, state, changed, branch))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Cogito++ Codex lifecycle guard")
    parser.add_argument("--fingerprint", action="store_true", help="print the active ticket scope identity")
    parser.add_argument("--validate-state", action="store_true", help="validate .codex/stage-state.json")
    args = parser.parse_args(argv)
    if args.fingerprint or args.validate_state:
        try:
            if OWNERSHIP_POLICY_ERROR:
                raise StateError(
                    f"Cogito++ ownership policy is unavailable: {OWNERSHIP_POLICY_ERROR}"
                )
            root = find_repo_root_with_script_fallback(Path.cwd())
            state = load_state(root)
            if args.fingerprint:
                _emit(scope_identity(root, state))
            else:
                _emit({"valid": True, "schema_version": state["schema_version"]})
            return 0
        except (OSError, StateError, ValueError) as exc:
            sys.stderr.write(str(exc) + "\n")
            return 1
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, OSError) as exc:
        sys.stderr.write(f"invalid hook input: {exc}\n")
        return 2
    if not isinstance(payload, dict):
        sys.stderr.write("invalid hook input: expected a JSON object\n")
        return 2
    return run_hook(payload)


if __name__ == "__main__":
    raise SystemExit(main())
