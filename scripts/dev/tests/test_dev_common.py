"""Unit tests for dev_common (helpers shared by all scripts/dev tools)."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from dev_common import not_executed, pid_alive, slug_from_url  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
