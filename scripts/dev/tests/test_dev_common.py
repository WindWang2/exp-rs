"""Unit tests for dev_common (helpers shared by all scripts/dev tools)."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from dev_common import not_executed, pid_alive, slug_from_url, trunk_ref  # noqa: E402


class RepoSlugTest(unittest.TestCase):
    def test_https_and_ssh_forms(self) -> None:
        cases = (
            ("https://github.com/Owner/Name.git", "Owner/Name"),
            ("https://github.com/Owner/Name", "Owner/Name"),
            ("git@github.com:Owner/Name.git", "Owner/Name"),
            ("ssh://git@github.com/Owner/Name.git", "Owner/Name"),
            ("git://github.com/Owner/Name.git", "Owner/Name"),
            ("https://gitlab.com/Owner/Name", None),
            ("https://github.com/Owner/Name/", "Owner/Name"),  # trailing slash tolerated
            ("https://github.com/Owner/Name/extra", None),     # extra path is not a slug
            ("", None),
        )
        for url, expected in cases:
            self.assertEqual(slug_from_url(url), expected, url)


class TrunkRefTest(unittest.TestCase):
    def test_trunk_is_an_origin_ref_not_a_local_branch(self) -> None:
        # Regression: returning the bare short name ("main") would make every
        # comparison use the LOCAL trunk, which in a mid-track worktree is the
        # track base, not origin's head.
        from _fixtures import TempRepo
        with TempRepo() as fx:
            self.assertEqual(trunk_ref(fx.repo), "origin/main")

    def test_trunk_reflects_local_vs_origin_divergence(self) -> None:
        from _fixtures import TRUNK, TempRepo, add_branch, git
        with TempRepo() as fx:
            # advance origin while the local trunk stays behind, so local-vs-
            # origin is observably different and a local-trunk comparison
            # would give a different answer
            add_branch(fx.repo, "feature/x", touch=["x.txt"], commit_message="c1: x")
            git(fx.repo, "checkout", TRUNK)
            git(fx.repo, "merge", "--no-ff", "feature/x", "-m", "merge feature/x")
            git(fx.repo, "push", "origin", TRUNK)
            git(fx.repo, "reset", "--hard", "HEAD~1")
            trunk = trunk_ref(fx.repo)
            self.assertNotEqual(trunk, TRUNK,
                                "trunk_ref must not return the bare local name")
            self.assertEqual(trunk, "origin/main")
            ahead_of_local = int(git(
                fx.repo, "rev-list", "--count", f"{TRUNK}..{trunk}").stdout.strip())
            self.assertGreaterEqual(ahead_of_local, 1,
                                    "fixture must have the local trunk behind origin")


class PidAliveTest(unittest.TestCase):
    def test_self_is_alive(self) -> None:
        self.assertTrue(pid_alive(os.getpid()))

    def test_bogus_pid_is_dead(self) -> None:
        # A pid far outside any plausible range: OpenProcess / signal both fail.
        self.assertFalse(pid_alive(1 << 30))


class MarkerTest(unittest.TestCase):
    def test_not_executed_shape(self) -> None:
        self.assertEqual(not_executed("no network"),
                         {"status": "not-executed", "reason": "no network"})


class RetryPolicyTest(unittest.TestCase):
    def test_mutating_branch_calls_run_exactly_once(self) -> None:
        # P1-4 regression: the read-only allowlist must key on the full argv,
        # not the bare subcommand, or `git branch -D` (the rollback) would be
        # retried up to 3 times.
        import subprocess
        from _fixtures import TempRepo
        calls: list[list[str]] = []

        real_run = subprocess.run

        class FakeResult:
            returncode = 1
            stdout = ""
            stderr = "stubbed failure"

        def fake_run(argv, *a, **kw):
            calls.append(list(argv[3:]) if argv[:3] == ["git", "-C", str(tmp_repo)] else list(argv))
            return FakeResult()

        with TempRepo() as fx:
            import dev_common
            tmp_repo = fx.repo
            dev_common.subprocess.run = fake_run
            try:
                for args in (["branch", "track/new", "HEAD"],   # create
                             ["branch", "-D", "track/new"],     # rollback
                             ["branch", "--list"]):             # read
                    dev_common.run_git(fx.repo, args, retries=3)
            finally:
                dev_common.subprocess.run = real_run

        by_args = {}
        for argv in calls:
            by_args.setdefault(tuple(argv[:2]), 0)
            by_args[tuple(argv[:2])] += 1
        self.assertEqual(by_args.get(("branch", "track/new")), 1,
                         "a creating git branch call was retried")
        self.assertEqual(by_args.get(("branch", "-D")), 1,
                         "a destructive git branch -D call was retried")
        self.assertEqual(by_args.get(("branch", "--list")), 3,
                         "a read-only branch listing should retry")


if __name__ == "__main__":
    unittest.main()
