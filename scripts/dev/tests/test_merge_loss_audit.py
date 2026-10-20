"""Tests for merge_loss_audit.py: the 29596c913 silent-revert detector.

Every fixture builds a real two-parent merge whose resolution drops one
parent's EXCLUSIVE change; the audit must name the path and the losing
parent. Clean merges, both-sides conflicts, additions and deletions are
covered so the detector neither misses the incident class nor cries wolf
on legitimate resolutions.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # scripts/dev
sys.path.insert(0, str(_HERE))         # _fixtures lives beside this file

from _fixtures import TRUNK, TempRepo, git, run_tool  # noqa: E402

import os  # noqa: E402  # kept: identity env dict below

_IDENTITY = {
    "GIT_AUTHOR_NAME": "Fixture",
    "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
    "GIT_COMMITTER_NAME": "Fixture",
    "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
}


def write(repo: Path, path: str, text: str) -> None:
    target = repo / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")


def commit(repo: Path, message: str) -> None:
    git(repo, "add", "-A")
    proc = git(repo, "commit", "-m", message, env=_IDENTITY)
    assert proc.returncode == 0, proc.stderr


def diverge_and_merge(repo: Path, resolve: str) -> None:
    """feature modifies fix.cpp exclusively; trunk changes other.cpp; merge.

    `resolve` selects the merge strategy: 'ours' drops the feature change
    (the incident class), 'recursive' keeps it.
    """
    write(repo, "fix.cpp", "// base version\n")
    commit(repo, "work: base version of fix.cpp")
    git(repo, "checkout", "-b", "feature")
    write(repo, "fix.cpp", "// feature fix v2\n")
    commit(repo, "fix: the change that must survive")
    git(repo, "checkout", TRUNK)
    write(repo, "other.cpp", "// unrelated trunk work\n")
    commit(repo, "work: unrelated trunk change")
    proc = git(repo, "merge", "--no-ff", "-s", resolve,
               "feature", "-m", f"merge feature ({resolve})", env=_IDENTITY)
    assert proc.returncode == 0, proc.stderr


class MergeLossAuditTest(unittest.TestCase):
    def test_lost_modification_is_reported(self) -> None:
        with TempRepo() as fx:
            diverge_and_merge(fx.repo, resolve="ours")
            proc = run_tool("merge_loss_audit.py", ["--json"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 1, proc.stderr)
            data = json.loads(proc.stdout)
            hits = [f for f in data["findings"] if f["path"] == "fix.cpp"]
            self.assertEqual(len(hits), 1, data)
            self.assertEqual(hits[0]["change"], "M")
            self.assertEqual(hits[0]["lost_parent"], 2)

    def test_clean_merge_reports_nothing(self) -> None:
        with TempRepo() as fx:
            diverge_and_merge(fx.repo, resolve="recursive")
            # 'recursive' with an exclusive change keeps it; force a real
            # merge commit shape by re-running on trunk (already merged above).
            proc = run_tool("merge_loss_audit.py", ["--json"], cwd=fx.repo)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            self.assertEqual(data["findings"], [])

    def test_lost_addition_is_reported(self) -> None:
        with TempRepo() as fx:
            repo = fx.repo
            git(repo, "checkout", "-b", "feature")
            write(repo, "new_file.cpp", "// added on feature\n")
            commit(repo, "feat: add new_file")
            git(repo, "checkout", TRUNK)
            write(repo, "other.cpp", "// unrelated trunk work\n")
            commit(repo, "work: unrelated trunk change")
            proc = git(repo, "merge", "--no-ff", "-s", "ours", "feature",
                       "-m", "merge feature (ours)", env=_IDENTITY)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            proc = run_tool("merge_loss_audit.py", ["--json"], cwd=repo)
            self.assertEqual(proc.returncode, 1, proc.stderr)
            data = json.loads(proc.stdout)
            hits = [f for f in data["findings"] if f["path"] == "new_file.cpp"]
            self.assertEqual(len(hits), 1, data)
            self.assertEqual(hits[0]["change"], "A")

    def test_lost_deletion_is_reported(self) -> None:
        with TempRepo() as fx:
            repo = fx.repo
            # a file the base has; feature deletes it; trunk stays silent on
            # it; merge resurrects it via -s ours.
            write(repo, "doomed.cpp", "// base content\n")
            commit(repo, "work: add doomed")
            git(repo, "checkout", "-b", "feature")
            (repo / "doomed.cpp").unlink()
            commit(repo, "chore: delete doomed")
            git(repo, "checkout", TRUNK)
            write(repo, "other.cpp", "// unrelated trunk work\n")
            commit(repo, "work: unrelated trunk change")
            proc = git(repo, "merge", "--no-ff", "-s", "ours", "feature",
                       "-m", "merge feature (ours)", env=_IDENTITY)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            proc = run_tool("merge_loss_audit.py", ["--json"], cwd=repo)
            self.assertEqual(proc.returncode, 1, proc.stderr)
            data = json.loads(proc.stdout)
            hits = [f for f in data["findings"] if f["path"] == "doomed.cpp"]
            self.assertEqual(len(hits), 1, data)
            self.assertEqual(hits[0]["change"], "D")

    def test_both_sides_conflict_resolution_is_not_reported(self) -> None:
        with TempRepo() as fx:
            repo = fx.repo
            git(repo, "checkout", "-b", "feature")
            write(repo, "shared.cpp", "// feature version\n")
            commit(repo, "fix: feature side")
            git(repo, "checkout", TRUNK)
            write(repo, "shared.cpp", "// trunk version\n")
            commit(repo, "fix: trunk side")
            # Conflicting both-sides change: resolution may pick either side
            # without the audit crying wolf.
            git(repo, "checkout", TRUNK)
            git(repo, "merge", "--no-ff", "-X", "ours", "feature",
                "-m", "merge feature (take trunk side of the conflict)",
                env=_IDENTITY)
            proc = run_tool("merge_loss_audit.py", ["--json"], cwd=repo)
            data = json.loads(proc.stdout)
            hits = [f for f in data["findings"] if f["path"] == "shared.cpp"]
            self.assertEqual(hits, [], data)

    def test_refuses_outside_a_repository(self) -> None:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            proc = run_tool("merge_loss_audit.py", [], cwd=Path(tmp))
            self.assertEqual(proc.returncode, 2)
            self.assertIn("not inside a git repository", proc.stderr)

    def test_human_output_names_the_losing_parent(self) -> None:
        with TempRepo() as fx:
            diverge_and_merge(fx.repo, resolve="ours")
            proc = run_tool("merge_loss_audit.py", [], cwd=fx.repo)
            self.assertEqual(proc.returncode, 1, proc.stderr)
            self.assertIn("fix.cpp", proc.stdout)
            self.assertIn("audited", proc.stdout)


if __name__ == "__main__":
    unittest.main()
