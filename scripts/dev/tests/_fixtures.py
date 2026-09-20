"""Shared hermetic git fixtures for the scripts/dev test suite.

Every fixture is a real git repository inside a temp directory: no network,
no dependency on the exp-rs checkout, no shared state. Fixtures use `main` as
the trunk so the tests never collide with a developer's real `master`.
"""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

TRUNK = "main"


def git(cwd: Path, *args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    return subprocess.run(["git", "-C", str(cwd), *args],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", errors="replace",
                          env=full_env)


def _commit(cwd: Path, message: str, name: str = "Fixture") -> str:
    git(cwd, "add", "-A")
    env = {
        "GIT_AUTHOR_NAME": name, "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
        "GIT_COMMITTER_NAME": name, "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
    }
    proc = git(cwd, "commit", "-m", message, env=env)
    assert proc.returncode == 0, proc.stderr
    return git(cwd, "rev-parse", "HEAD").stdout.strip()


def make_repo(tmp: Path, with_remote: bool = True) -> Path:
    """A repo on `main` with one commit; optionally a bare origin at origin.git."""
    repo = tmp / "repo"
    repo.mkdir(parents=True)
    git(repo, "init", f"--initial-branch={TRUNK}")
    (repo / "README.md").write_text("fixture\n", encoding="utf-8")
    (repo / "src").mkdir()
    (repo / "src" / "app.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
    _commit(repo, "initial")
    if with_remote:
        bare = tmp / "origin.git"
        git(tmp, "init", "--bare", str(bare))
        git(repo, "remote", "add", "origin", str(bare))
        git(repo, "push", "-u", f"origin", TRUNK)
        git(repo, "fetch", "origin")
    return repo


def add_branch(repo: Path, name: str, touch: list[str] | None = None,
               commit_message: str | None = None) -> str:
    """Create branch `name` off main, optionally writing files, and commit."""
    git(repo, "checkout", TRUNK)
    git(repo, "checkout", "-b", name)
    for path in touch or []:
        target = repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f"// {name}\n", encoding="utf-8")
    _commit(repo, commit_message or f"work on {name}")
    return git(repo, "rev-parse", "HEAD").stdout.strip()


def merge_branch(repo: Path, name: str, message: str | None = None) -> None:
    """Merge branch `name` into main (fast-forward when possible) and push."""
    git(repo, "checkout", TRUNK)
    proc = git(repo, "merge", "--no-ff", name, "-m", message or f"merge {name}")
    assert proc.returncode == 0, proc.stderr
    git(repo, "push", "origin", TRUNK)


class TempRepo:
    """Context manager owning a temp directory with a fixture repo."""

    def __init__(self, with_remote: bool = True) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="devtools-fixture-")
        self.root = Path(self._tmp.name)
        self.repo = make_repo(self.root, with_remote=with_remote)

    def __enter__(self) -> "TempRepo":
        return self

    def __exit__(self, *exc: object) -> None:
        self._tmp.cleanup()


DEV_DIR = Path(__file__).resolve().parents[1]


def run_tool(tool: str, args: list[str], cwd: Path, timeout: float = 120.0,
             extra_path: list[str] | None = None) -> subprocess.CompletedProcess:
    """Run one of the scripts/dev tools as a subprocess, like an agent would."""
    import sys
    env = dict(os.environ)
    if extra_path:
        env["PATH"] = os.pathsep.join(extra_path) + os.pathsep + env.get("PATH", "")
    return subprocess.run(
        [sys.executable, str(DEV_DIR / tool), *args],
        cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", env=env, timeout=timeout,
    )
