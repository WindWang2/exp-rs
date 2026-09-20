#!/usr/bin/env python3
"""dev_common.py — shared helpers for the scripts/dev agent-development tools.

Every tool in scripts/dev/ shells out to real CLIs (git, gh) and every one of
them must keep working when those CLIs are degraded (offline, missing gh, or
another agent fetching the same clone concurrently). This module centralises
the three cross-cutting concerns:

  * run/run_git: subprocess execution with bounded retries for READ-ONLY git
    queries (parallel agents fetching the same clone can make origin/* refs
    momentarily unresolvable; writes are never retried).
  * gh(): optional GitHub queries that degrade to a not-executed record
    instead of an exception when the CLI or the network is unavailable.
  * output/exit conventions: human text by default, --json for machine
    consumers, and fixed exit codes: 0 success (possibly degraded),
    2 refused (fail-closed precondition), 75 busy (lock held), 1 genuine
    failure.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

EXIT_OK = 0
EXIT_FAILURE = 1
EXIT_REFUSED = 2
EXIT_BUSY = 75  # EX_TEMPFAIL — lock held by another process

# git subcommands that only read repository state. Under parallel agents a
# concurrent `git fetch --prune` can transiently re-resolve these refs; a short
# retry rides that out. Anything that mutates is excluded and fails closed.
_GIT_READONLY_HEADS = {
    "rev-parse", "log", "show", "status", "worktree", "branch", "diff",
    "for-each-ref", "ls-remote", "ls-files", "cat-file", "symbolic-ref",
    "merge-base", "rev-list", "describe", "config", "check-ignore",
}


class ToolError(Exception):
    """Fail-closed error carrying the process exit code and a reason."""

    def __init__(self, message: str, exit_code: int = EXIT_REFUSED) -> None:
        super().__init__(message)
        self.exit_code = exit_code


def run(argv: list[str], cwd: Path | None = None, timeout: float | None = 120.0,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    """Run argv, capturing output. Never raises on non-zero exit."""
    return subprocess.run(
        argv, cwd=str(cwd) if cwd else None, timeout=timeout,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", env=env,
    )


def run_streamed(argv: list[str], env: dict[str, str] | None = None
                 ) -> subprocess.CompletedProcess:
    """Run argv with the caller's stdout/stderr inherited.

    Used by the resource guard so build output reaches the agent's terminal
    live instead of being buffered and dropped.
    """
    return subprocess.run(argv, env=env)


def run_git(git_dir: Path | None, args: list[str], retries: int = 3,
            timeout: float | None = 120.0) -> subprocess.CompletedProcess:
    """Run a git command.

    Read-only commands retry up to `retries` times (concurrent-fetch
    transient). Mutating commands run exactly once and their failure is
    reported verbatim — a half-applied write must not be retried.
    """
    argv = ["git"]
    if git_dir is not None:
        argv += ["-C", str(git_dir)]
    argv += args
    attempts = retries if args and args[0] in _GIT_READONLY_HEADS else 1
    last: subprocess.CompletedProcess | None = None
    for attempt in range(1, attempts + 1):
        try:
            proc = subprocess.run(
                argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, encoding="utf-8", errors="replace", timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            if attempt == attempts:
                raise
            time.sleep(0.25 * attempt)
            continue
        last = proc
        if proc.returncode == 0:
            return proc
        if attempt < attempts:
            time.sleep(0.25 * attempt)
    assert last is not None
    return last


def git_ok(git_dir: Path | None, args: list[str], retries: int = 3) -> str:
    """Run a read-only git command, returning stdout or raising ToolError."""
    proc = run_git(git_dir, args, retries=retries)
    if proc.returncode != 0:
        raise ToolError(
            f"git {' '.join(args)} failed (rc={proc.returncode}): "
            f"{proc.stderr.strip() or proc.stdout.strip()}",
            EXIT_FAILURE,
        )
    return proc.stdout


def find_repo_root(start: Path | None = None) -> Path:
    """Resolve the repository root (the main checkout or a linked worktree)."""
    proc = run_git(start or Path.cwd(), ["rev-parse", "--show-toplevel"])
    if proc.returncode != 0:
        raise ToolError("not inside a git repository", EXIT_REFUSED)
    return Path(proc.stdout.strip())


def gh_available() -> bool:
    return shutil.which("gh") is not None


def trunk_ref(repo: Path | None) -> str:
    """Best-effort name of origin's default branch (origin/HEAD, else
    origin/master, else origin/main). Repositories differ; assuming
    `master` breaks every fixture and every renamed-default repo."""
    proc = run_git(repo, ["symbolic-ref", "refs/remotes/origin/HEAD"])
    if proc.returncode == 0:
        target = proc.stdout.strip()
        prefix = "refs/remotes/origin/"
        if target.startswith(prefix) and len(target) > len(prefix):
            return target[len(prefix):]
    for candidate in ("origin/master", "origin/main"):
        proc = run_git(repo, ["rev-parse", "--verify", "--quiet", candidate])
        if proc.returncode == 0:
            return candidate
    return "origin/master"


def slug_from_url(url: str) -> str | None:
    """Extract OWNER/REPO from a GitHub remote URL (https or ssh form)."""
    url = url.strip().rstrip("/")
    for prefix in ("https://github.com/", "git@github.com:",
                   "ssh://git@github.com/", "git://github.com/"):
        if url.startswith(prefix):
            slug = url[len(prefix):]
            if slug.endswith(".git"):
                slug = slug[:-4]
            parts = [p for p in slug.split("/") if p]
            return "/".join(parts) if len(parts) == 2 else None
    return None


def repo_slug(repo: Path | None) -> str | None:
    """OWNER/REPO for a repository, or None when the origin is not GitHub."""
    proc = run_git(repo, ["remote", "get-url", "origin"])
    if proc.returncode != 0:
        return None
    return slug_from_url(proc.stdout.strip())


def gh_json(repo: Path | None, args: list[str], timeout: float = 60.0
            ) -> tuple[list | dict | None, str | None]:
    """Query gh, returning (payload, None) or (None, reason-if-degraded).

    Degradation is a recorded fact, not an error: callers embed it in their
    output as not-executed.
    """
    if not gh_available():
        return None, "gh CLI not found on PATH"
    argv = ["gh"]
    slug = repo_slug(repo)
    if slug:
        argv += ["-R", slug]
    argv += args
    try:
        proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8", errors="replace",
                              timeout=timeout, cwd=str(repo) if repo else None)
    except subprocess.TimeoutExpired:
        return None, "gh query timed out"
    if proc.returncode != 0:
        return None, f"gh exited {proc.returncode}: {proc.stderr.strip()[:200]}"
    text = proc.stdout.strip()
    if not text:
        return [], None
    try:
        return json.loads(text), None
    except json.JSONDecodeError as exc:
        return None, f"gh returned non-JSON output: {exc}"


def not_executed(reason: str) -> dict:
    """Machine-readable marker for a capability that could not run."""
    return {"status": "not-executed", "reason": reason}


def emit(payload: dict, as_json: bool, text_lines: list[str] | None = None) -> None:
    """Print the result: JSON when --json, else the human text (or pretty JSON)."""
    if as_json:
        print(json.dumps(payload, indent=2, sort_keys=False))
    elif text_lines:
        print("\n".join(text_lines))
    else:
        print(json.dumps(payload, indent=2, sort_keys=False))


def pid_alive(pid: int) -> bool:
    """Best-effort cross-platform liveness test for a recorded pid."""
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid)
        if not handle:
            return False
        try:
            code = ctypes.c_ulong(STILL_ACTIVE)
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return code.value == STILL_ACTIVE
            return False
        finally:
            kernel32.CloseHandle(handle)
    import signal
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def main(argv: list[str] | None = None) -> int:
    """Self-check: importable module, no behaviour of its own."""
    print(__doc__)
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
