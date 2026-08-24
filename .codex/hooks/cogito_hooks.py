from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any


POLICY_PATH = Path(__file__).resolve().parents[2] / "scripts" / "ownership-policy.json"
PROTECTED_BRANCHES = {"main", "master"}
WINDOWS_DEVICE_NAMES = {
    "con", "prn", "aux", "nul",
    *(f"com{index}" for index in range(1, 10)),
    *(f"lpt{index}" for index in range(1, 10)),
}
DIRECT_WRITE_TOOLS = {
    "apply_patch", "edit", "write", "write_file", "write_to_file", "replace", "replace_in_file",
    "replace_file_content", "create", "create_file", "delete", "delete_file",
    "remove", "remove_file", "move", "move_file",
}
SHELL_TOOLS = {"bash", "shell_command", "exec_command"}
PATCH_PATH_RE = re.compile(
    r"^\*\*\*\s+(?:Add|Update|Delete)\s+File:\s*(.+?)\s*$|"
    r"^\*\*\*\s+Move\s+to:\s*(.+?)\s*$",
    re.MULTILINE,
)
MUTATING_SHELL_RE = re.compile(
    r"(?ix)"
    r"(?:^|[;&|]\s*|\s)"
    r"(?:rm|rmdir|del|erase|mv|move|cp|copy|touch|"
    r"remove-item|move-item|copy-item|new-item|set-content|add-content|out-file|"
    r"sed\s+-i|perl\s+-pi|git\s+(?:add|rm|mv))\b"
    r"|(?:^|[^<])>>?"
)
INDIRECT_CODE_RE = re.compile(
    r"(?ix)"
    r"\b(?:python|python3|py|node|npx|perl|ruby|php)\b"
    r"|\b(?:powershell|pwsh)(?:\.exe)?\b[^\r\n]*\s-(?:command|encodedcommand|file)\b"
    r"|\b(?:bash|sh)\b\s+[^-\s]"
    r"|\bcmd(?:\.exe)?\b[^\r\n]*\s/[ck]\b"
    r"|\bcmake\b[^\r\n]*(?:^|\s)-P(?:\s|$)"
    r"|\b(?:curl|wget|invoke-webrequest)\b[^\r\n]*(?:-o\b|--output\b|outfile)"
)
GIT_INJECTION_RE = re.compile(
    r"(?ix)"
    r"\bgit(?:\.exe)?\s+(?:(?:--no-pager|--paginate)\s+)*(?:"
    r"-[cC](?:\s|=|(?=[A-Za-z0-9]))|--config-env(?:\s|=)|--exec-path(?:\s|=)|"
    r"--git-dir(?:\s|=)|--work-tree(?:\s|=)|--namespace(?:\s|=))"
    r"|(?:^|[\s;&|])(?:GIT_[A-Z0-9_]+|PAGER)\s*="
    r"|\bgit\b[^\r\n;&|]*\b(?:commit|merge|rebase|cherry-pick|revert)\b"
    r"[^\r\n;&|]*--no-verify\b"
)
GIT_EXECUTION_RE = re.compile(
    r"(?ix)"
    r"\bgit\b[^\r\n;&|]*(?:--ext-diff\b|--textconv\b|--output(?:\s|=)|"
    r"--open-files-in-pager(?:\s|=)|(?:^|\s)-O(?:\s|\S))"
    r"|\bgit\b[^\r\n;&|]*\bsubmodule\s+foreach\b"
    r"|\bgit\b[^\r\n;&|]*\b(?:filter-branch|difftool|mergetool|credential)\b"
)
DESTRUCTIVE_GIT_RE = re.compile(
    r"(?ix)"
    r"\bgit\b[^\r\n;&|]*\breset\b[^\r\n;&|]*--hard\b"
    r"|\bgit\b[^\r\n;&|]*\bclean\b"
    r"|\bgit\b[^\r\n;&|]*\b(?:restore|checkout)\b[^\r\n;&|]*\s--\s"
    r"|\bgit\b[^\r\n;&|]*\bbranch\b[^\r\n;&|]*\s-D\b"
    r"|\bgit\b[^\r\n;&|]*\btag\b[^\r\n;&|]*\s-d\b"
    r"|\bgit\b[^\r\n;&|]*\bupdate-ref\b"
    r"|\bgit\b[^\r\n;&|]*\bfilter-branch\b"
    r"|\bgit\b[^\r\n;&|]*\bpush\b[^\r\n;&|]*(?:"
    r"--force(?:-with-lease|-if-includes)?\b|--delete\b|--mirror\b|--all\b|"
    r"(?:^|\s)-[A-Za-z]*f[A-Za-z]*(?:\s|$)|\s\+[^\s]+)"
)
PUSH_PROTECTED_RE = re.compile(
    r"(?ix)\bgit\b[^\r\n;&|]*\bpush\b[^\r\n;&|]*(?:"
    r"\b(?:main|master)\b|refs/heads/(?:main|master)\b)"
)


class GuardError(RuntimeError):
    pass


def load_policy(path: Path = POLICY_PATH) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise GuardError(f"cannot load ownership policy: {exc}") from exc

    if not isinstance(value, dict):
        raise GuardError("ownership policy root must be an object")
    if value.get("version") != 2:
        raise GuardError("ownership policy version must be 2")
    owners = value.get("owners")
    if not isinstance(owners, dict) or set(owners) != {"gemini", "codex"}:
        raise GuardError("ownership policy must define exactly Gemini and Codex")
    contract = value.get("guard_contract")
    expected_contract = {
        "algorithm": "longest_prefix",
        "case_sensitive": False,
        "separator": "/",
        "fallback_owner": "deny",
    }
    if contract != expected_contract:
        raise GuardError("ownership guard contract does not match the fail-closed v2 contract")

    rules = value.get("rules")
    if not isinstance(rules, list) or not rules:
        raise GuardError("ownership policy rules must be a non-empty array")

    seen: set[str] = set()
    for index, rule in enumerate(rules, start=1):
        if not isinstance(rule, dict):
            raise GuardError(f"rule {index} must be an object")
        prefix = rule.get("prefix")
        owner = rule.get("owner")
        if not isinstance(prefix, str) or not prefix or "\\" in prefix or prefix.startswith("/"):
            raise GuardError(f"rule {index} has an invalid prefix")
        if ".." in PurePosixPath(prefix).parts:
            raise GuardError(f"rule {index} contains parent traversal")
        if owner not in {"gemini", "codex"}:
            raise GuardError(f"rule {index} has an invalid owner")
        folded = prefix.casefold()
        if folded in seen:
            raise GuardError(f"duplicate ownership prefix: {prefix}")
        seen.add(folded)
    return value


def _validate_component(component: str) -> None:
    if not component or component in {".", ".."}:
        raise GuardError("empty or parent path component")
    if "\x00" in component:
        raise GuardError("NUL is not allowed in a path")
    if component.endswith((" ", ".")):
        raise GuardError("trailing dot or space is not allowed")
    if ":" in component:
        raise GuardError("NTFS alternate data streams are not allowed")
    base = component.split(".", 1)[0].casefold()
    if base in WINDOWS_DEVICE_NAMES:
        raise GuardError("Windows device names are not allowed")


def normalize_repo_path(raw: str, root: Path) -> str | None:
    if not isinstance(raw, str) or not raw.strip():
        raise GuardError("write path is missing")
    text = raw.strip().strip("\"'")
    if "\x00" in text:
        raise GuardError("NUL is not allowed in a path")

    lexical = text.replace("\\", "/")
    if ".." in PurePosixPath(lexical).parts:
        raise GuardError("parent traversal is not allowed")

    candidate = Path(text)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        resolved = candidate.resolve(strict=False)
        relative = resolved.relative_to(root.resolve(strict=False))
    except ValueError:
        return None
    except OSError as exc:
        raise GuardError(f"cannot normalize path: {exc}") from exc

    result = relative.as_posix()
    if not result or result == ".":
        raise GuardError("repository root is not a writable file path")
    if result.casefold() == ".git" or result.casefold().startswith(".git/"):
        raise GuardError(".git internals are not writable through tools")
    for component in PurePosixPath(result).parts:
        _validate_component(component)
    return result


def owner_for_path(path: str, policy: dict[str, Any]) -> str:
    folded = path.casefold()
    owner = str(policy["guard_contract"]["fallback_owner"])
    best = -1
    for rule in policy["rules"]:
        prefix = str(rule["prefix"])
        prefix_folded = prefix.casefold()
        matches = folded.startswith(prefix_folded) if prefix.endswith("/") else folded == prefix_folded
        if matches and len(prefix_folded) > best:
            owner = str(rule["owner"])
            best = len(prefix_folded)
    return owner


def deny(reason: str) -> dict[str, Any]:
    return {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }


def _tool_name(payload: dict[str, Any]) -> str:
    return str(payload.get("tool_name") or payload.get("toolName") or "").casefold()


def _tool_input(payload: dict[str, Any]) -> Any:
    return payload.get("tool_input", payload.get("toolInput", {}))


def extract_write_paths(payload: dict[str, Any]) -> list[str]:
    name = _tool_name(payload)
    value = _tool_input(payload)
    if name == "apply_patch":
        if isinstance(value, dict):
            patch = value.get("patch") or value.get("input") or value.get("text") or ""
        else:
            patch = value
        if not isinstance(patch, str):
            return []
        paths: list[str] = []
        for match in PATCH_PATH_RE.finditer(patch):
            selected = match.group(1) or match.group(2)
            if selected:
                paths.append(selected.strip())
        return paths

    if not isinstance(value, dict):
        return []
    paths = []
    for key in (
        "file_path", "path", "target_file", "TargetFile", "target",
        "source", "source_path", "destination", "destination_path", "new_path",
    ):
        selected = value.get(key)
        if isinstance(selected, str) and selected.strip():
            paths.append(selected)
    return paths


def _command(payload: dict[str, Any]) -> str:
    value = _tool_input(payload)
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        selected = value.get("cmd") or value.get("command") or ""
        return selected if isinstance(selected, str) else ""
    return ""


def _is_subagent(payload: dict[str, Any]) -> bool:
    if payload.get("is_subagent") is True:
        return True
    if payload.get("agent_id") or payload.get("agentId"):
        return True
    agent_type = str(payload.get("agent_type") or payload.get("agentType") or "").casefold()
    return bool(agent_type and agent_type not in {"root", "primary"})


def _run_git(root: Path, *args: str, strip: bool = True) -> str | None:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.strip() if strip else completed.stdout


def current_branch(root: Path) -> str | None:
    return _run_git(root, "branch", "--show-current")


def staged_paths(root: Path) -> list[str]:
    output = _run_git(root, "diff", "--cached", "--name-only", "-z", strip=False)
    if output is None:
        raise GuardError("cannot inspect staged paths")
    return [item for item in output.split("\0") if item]


def _without_null_redirections(command: str) -> str:
    return re.sub(
        r"(?i)(?:\d*>\s*(?:\$null|/dev/null|nul)\b|\d*>\s*&\d)",
        "",
        command,
    ).strip()


def _subagent_read_only(command: str) -> bool:
    view = _without_null_redirections(command)
    if not view or re.search(r"[;&|<>`]", view) or "$" in view:
        return False
    if re.search(r"(?i)\brg\b[^\r\n]*(?:--pre(?:-glob)?\b|--files-with-matches\s*=)", view):
        return False
    if GIT_EXECUTION_RE.search(view):
        return False
    patterns = (
        r"(?:rg|grep)\b.*",
        r"git\s+(?:status|diff|show|log|rev-parse|ls-files|branch\s+--show-current)\b.*",
        r"(?:Get-Content|Select-String|Get-ChildItem|Test-Path|Resolve-Path|Measure-Object)\b.*",
        r"(?:cat|type|ls|dir|pwd|which|where(?:\.exe)?)\b.*",
        r"(?:cmake|ctest|ninja|node|npm|git|clang|gcc|cl|py|python|python3)\s+--version\b.*",
    )
    return any(re.fullmatch(pattern, view, re.IGNORECASE) for pattern in patterns)


def _allowed_code_runner(command: str) -> bool:
    view = _without_null_redirections(command)
    patterns = (
        r"cmake\s+(?:--preset\s+[^\s]+|--build\s+--preset\s+[^\s]+|--version)\s*",
        r"ctest\s+(?:--preset\s+[^\s]+|--version)(?:\s+[^;&|<>`]*)?",
        r"ninja\s+-C\s+(?:build|out)(?:[/\\][^\s;&|<>`]*)?(?:\s+[^;&|<>`]*)?",
        r"(?:py|python|python3)(?:\.exe)?(?:\s+-3)?(?:\s+-[A-Za-z]+)*\s+-m\s+(?:unittest|pytest)\b.*",
        r"pytest\b.*",
        r"node(?:\.exe)?\s+(?:--check|--test)\b.*",
        r"npm(?:\.cmd)?\s+(?:test|run\s+(?:test|build|lint|typecheck|check))(?:\s+--?[A-Za-z0-9_.=-]+)*\s*",
        r"clang-format(?:\.exe)?\s+--dry-run\s+--Werror\b.*",
        r"(?:py|python|python3)(?:\.exe)?(?:\s+-3)?(?:\s+-[A-Za-z]+)*\s+"
        r"\"?(?:(?:[A-Za-z]:)?[^\r\n;&|\"]*[\\/])?\.codex[\\/]hooks[\\/]cogito_hooks\.py\"?\s+"
        r"(?:--validate-policy|--owner\s+[^\r\n;&|]+)\s*",
    )
    return any(re.fullmatch(pattern, view, re.IGNORECASE) for pattern in patterns)


def _split_simple_command(command: str) -> list[str] | None:
    try:
        return shlex.split(command, posix=True)
    except ValueError:
        return None


def _safe_commit_command(command: str) -> bool:
    tokens = _split_simple_command(command)
    if not tokens or len(tokens) < 4 or tokens[0].casefold() not in {"git", "git.exe"}:
        return False
    if tokens[1].casefold() != "commit":
        return False
    saw_message = False
    index = 2
    while index < len(tokens):
        token = tokens[index]
        folded = token.casefold()
        if folded in {"-m", "--message"}:
            index += 1
            if index >= len(tokens):
                return False
            saw_message = True
        elif folded.startswith("-m") and len(token) > 2:
            saw_message = True
        elif folded.startswith("--message=") and len(token) > len("--message="):
            saw_message = True
        elif folded in {"-q", "--quiet", "-v", "--verbose", "--status", "--no-status"}:
            pass
        elif folded == "--cleanup":
            index += 1
            if index >= len(tokens) or tokens[index].casefold() not in {"strip", "whitespace", "verbatim", "scissors", "default"}:
                return False
        elif folded.startswith("--cleanup="):
            if folded.split("=", 1)[1] not in {"strip", "whitespace", "verbatim", "scissors", "default"}:
                return False
        else:
            return False
        index += 1
    return saw_message


def _safe_push_command(command: str) -> bool:
    tokens = _split_simple_command(command)
    if not tokens or tokens[0].casefold() not in {"git", "git.exe"}:
        return False
    if len(tokens) not in {4, 5} or tokens[1].casefold() != "push":
        return False
    index = 2
    if tokens[index].casefold() in {"-u", "--set-upstream"}:
        index += 1
    if len(tokens) != index + 2 or tokens[index].casefold() != "origin":
        return False
    reference = tokens[index + 1]
    if reference.casefold() in PROTECTED_BRANCHES:
        return False
    return bool(re.fullmatch(r"(?:HEAD|[A-Za-z0-9][A-Za-z0-9._/-]*)", reference))


def shell_decision(command: str, root: Path, policy: dict[str, Any], subagent: bool = False) -> dict[str, Any]:
    if not command.strip():
        return deny("shell command is missing")
    mutation_view = _without_null_redirections(command)
    if re.search(r"[\r\n\u2028\u2029;&|<>`()\[\]{}$]", mutation_view):
        return deny("compound commands, shell expressions, expansion, pipelines, and non-null redirection are blocked")
    if GIT_INJECTION_RE.search(command):
        return deny("Git command-scoped configuration, helper injection, and hook bypass are blocked")
    if GIT_EXECUTION_RE.search(command):
        return deny("Git external helpers, pagers, and arbitrary execution are blocked")
    if subagent:
        if _subagent_read_only(command):
            return {}
        return deny("subagents may run only the explicit read-only shell command set")
    if DESTRUCTIVE_GIT_RE.search(command):
        return deny("destructive Git and force/bulk/delete publication are blocked")
    restore = re.search(r"(?i)\bgit\b[^\r\n;&|]*\brestore\b", command)
    if restore and not (
        re.search(r"(?i)(?:^|\s)--staged(?:\s|$)", command)
        and not re.search(r"(?i)(?:^|\s)--worktree(?:\s|$)", command)
    ):
        return deny("Git worktree restore is destructive and blocked")
    if PUSH_PROTECTED_RE.search(command):
        return deny("pushing main or master is blocked")
    git_command = re.match(r"(?i)^\s*git(?:\.exe)?\b", command)
    git_commit = git_command and re.search(r"(?i)\bcommit\b", command)
    git_push = git_command and re.search(r"(?i)\bpush\b", command)
    if git_commit or git_push:
        if git_commit and not _safe_commit_command(command):
            return deny("commit command must be canonical and message-only")
        if git_push and not _safe_push_command(command):
            return deny("push command must name origin and one safe non-protected ref")
        branch = current_branch(root)
        if not branch:
            return deny("cannot inspect the current branch for Git publication")
        if branch.casefold() in PROTECTED_BRANCHES:
            return deny("commit and push from main or master are blocked")
    if re.search(
        r"(?i)\b(?:rm|rmdir|del|erase|remove-item)\b[^\r\n;&|]*"
        r"(?:^|\s)(?:\.|/|\*|~|[a-z]:[\\/])(?:\s|$)",
        command,
    ):
        return deny("broad filesystem deletion is blocked")
    if INDIRECT_CODE_RE.search(command) and not _allowed_code_runner(command):
        return deny("indirect code execution is not ownership-reviewable; use apply_patch or an approved test runner")
    if re.search(r"(?i)\bclang-format(?:\.exe)?\b[^\r\n]*(?:^|\s)-i(?:\s|$)", command):
        return deny("in-place formatting through shell is blocked; use an exact-path write tool")
    if MUTATING_SHELL_RE.search(mutation_view):
        return deny("filesystem mutation through shell is blocked; use an exact-path write tool")
    if git_commit:
        if re.search(
            r"(?i)(?:^|\s)(?:--all|--include|--only|--amend|-[a-z]*[aio][a-z]*)(?:\s|=|$)|\s--\s+",
            command,
        ):
            return deny("commit must use an explicitly reviewed staged scope")
        try:
            foreign = [
                path for path in staged_paths(root)
                if owner_for_path(path.replace("\\", "/"), policy) != "codex"
            ]
        except GuardError as exc:
            return deny(str(exc))
        if foreign:
            return deny("commit contains non-Codex paths: " + ", ".join(foreign[:8]))
        return {}
    if git_push:
        return {}
    if re.fullmatch(r"(?i)\s*git\s+restore\s+--staged\s+[^;&|<>`]+\s*", mutation_view):
        return {}
    if _subagent_read_only(command) or _allowed_code_runner(command):
        return {}
    return deny("shell command is outside the explicit read-only, build, test, and safe Git allowlist")


def pre_tool_decision(payload: dict[str, Any], root: Path, policy: dict[str, Any] | None = None) -> dict[str, Any]:
    try:
        selected_policy = policy or load_policy()
    except GuardError as exc:
        return deny(f"ownership policy unavailable: {exc}")

    name = _tool_name(payload)
    subagent = _is_subagent(payload)
    if name in DIRECT_WRITE_TOOLS:
        if subagent:
            return deny("subagents are read-only")
        branch = current_branch(root)
        if not branch:
            return deny("cannot inspect the current branch for direct write")
        if branch.casefold() in PROTECTED_BRANCHES:
            return deny("direct writes on main or master are blocked")
        paths = extract_write_paths(payload)
        if not paths:
            return deny("write-capable tool did not expose an exact path")
        if name in {"move", "move_file"} and len(paths) < 2:
            return deny("move tool must expose both source and destination")
        for raw in paths:
            try:
                relative = normalize_repo_path(raw, root)
            except GuardError as exc:
                return deny(str(exc))
            if relative is None:
                return deny("write target is outside the repository")
            owner = owner_for_path(relative, selected_policy)
            if owner != "codex":
                return deny(f"{relative} belongs to {owner}; Gemini owns prompts and contracts")
        return {}

    if name in SHELL_TOOLS:
        return shell_decision(_command(payload), root, selected_policy, subagent)
    return {}


def find_repo_root(start: Path) -> Path:
    candidate = start.resolve(strict=False)
    for path in (candidate, *candidate.parents):
        if (path / ".git").exists() and (path / "AGENTS.md").is_file():
            return path
    raise GuardError(f"cannot locate repository root from {start}")


def _context(event: str, text: str) -> dict[str, Any]:
    return {"hookSpecificOutput": {"hookEventName": event, "additionalContext": text}}


def _emit(value: dict[str, Any]) -> None:
    if value:
        sys.stdout.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")))


def run_hook(payload: dict[str, Any]) -> int:
    event = str(payload.get("hook_event_name") or payload.get("hookEventName") or "")
    if event not in {"SessionStart", "SubagentStart", "PreToolUse"}:
        return 0
    try:
        root = find_repo_root(Path(str(payload.get("cwd") or Path.cwd())))
        policy = load_policy()
    except GuardError as exc:
        if event == "PreToolUse":
            _emit(deny(f"guard configuration unavailable: {exc}"))
        else:
            _emit({"systemMessage": f"Cogito++ guard configuration unavailable: {exc}"})
        return 0

    if event == "SessionStart":
        _emit(_context(
            event,
            "Cogito++ 2-Agent model: Gemini owns prompts/specifications/public headers; "
            "Codex owns all implementation and tests, including web. Preserve unrelated changes "
            "and do not publish without an explicit user request.",
        ))
    elif event == "SubagentStart":
        _emit(_context(event, "Subagents are read-only reviewers; the root Codex agent owns all writes."))
    else:
        _emit(pre_tool_decision(payload, root, policy))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Cogito++ lightweight 2-Agent guard")
    parser.add_argument("--validate-policy", action="store_true")
    parser.add_argument("--owner", metavar="PATH")
    args = parser.parse_args(argv)

    try:
        policy = load_policy()
        root = find_repo_root(Path.cwd())
        if args.owner:
            relative = normalize_repo_path(args.owner, root)
            owner = "outside" if relative is None else owner_for_path(relative, policy)
            print(json.dumps({"path": args.owner, "owner": owner}, ensure_ascii=False))
            return 0
        if args.validate_policy:
            print(json.dumps({
                "valid": True,
                "model": "gemini-codex",
                "rules": len(policy["rules"]),
            }, separators=(",", ":")))
            return 0

        raw = sys.stdin.read()
        payload = json.loads(raw) if raw.strip() else {}
        if not isinstance(payload, dict):
            raise GuardError("hook payload must be an object")
        return run_hook(payload)
    except (GuardError, json.JSONDecodeError) as exc:
        print(json.dumps({"valid": False, "error": str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
