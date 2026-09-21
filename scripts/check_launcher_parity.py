#!/usr/bin/env python3
# scripts/check_launcher_parity.py — Windows/POSIX launcher parity gate.
#
# tests/CMakeLists.txt registers `launcher_parity_conformance`, but that lane
# is Python-only and POSIX-only: conformance.py delegates the Windows side to
# "reviewed statically", which means nothing actually enforces it. This script
# is the missing half — a static parser over the Windows launchers (.cmd/.bat)
# and the PowerShell/shell twins, asserting the three properties a classroom
# launcher must hold:
#
#   1. exit-code propagation — a launcher that runs a fallible programme must
#      not end with an unconditional success exit (`exit /b 0`). A teacher
#      scripting `GRADE_ALL.cmd && …` must see the failure.
#   2. NO_PAUSE — an interactive `pause` must be short-circuited by
#      SICNU_NO_PAUSE so unattended (CI / batch) runs never block.
#   3. --out parity — where a launcher parses a bundle output directory it
#      must accept BOTH spellings (`--out=<dir>` and `--out <dir>`), because
#      both are documented and both appear in the ADRs.
#
# Windows execution is NOT attempted: this host may have no Windows shell in
# the sandbox, and claiming otherwise would fake evidence. The gate prints
# "NOT EXECUTED — static only" for the .cmd/.ps1 lanes and that string is part
# of its contract.
#
#   python3 scripts/check_launcher_parity.py --check

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TARGETS = [
    "packaging/bundle/GENERATE_SAMPLES.cmd",
    "packaging/bundle/GRADE_ALL.cmd",
    "packaging/bundle/RUN.cmd",
    "packaging/bundle/VERIFY.cmd",
    "packaging/bundle/VERIFY.ps1",
    "packaging/bundle/GRADE_ALL.sh",
    "packaging/bundle/VERIFY.sh",
    "test_wb7.cmd",
    "tools/verification12/run_tests.sh",
]

# A statement that can fail: invoking an executable.
CMD_INVOCATION = re.compile(r"^\s*(?:\"?[^\s\"]+\.exe\"?|call\s+\S+|bin\\[^\s]+)", re.I)
CMD_UNCONDITIONAL_OK = re.compile(r"^\s*exit\s+/b\s+0\s*$", re.I)
CMD_PAUSE = re.compile(r"^\s*pause\s*$", re.I)
NO_PAUSE_GUARD = re.compile(r"SICNU_NO_PAUSE", re.I)
OUT_EQ = re.compile(r"--out\s*=")
OUT_SPACE = re.compile(r"--out\s+(?![=\s])")


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def check_cmd(text, where, out):
    lines = text.splitlines()
    runs_something = any(CMD_INVOCATION.match(line) for line in lines)

    # Conservative on purpose: an `exit /b 0` guarded by a preceding failure
    # exit is a legitimate success path. What must never happen is a launcher
    # that runs fallible commands and has no failure exit at all — then every
    # caller sees green no matter what happened. (This is exactly how the
    # pre-13.0 test_wb7.cmd behaved: 19 lines ending in `exit /b 0`.)
    if runs_something and not re.search(r"^\s*exit\s+/b\s+[1-9]\d*\s*$", text, re.I | re.M):
        for idx, line in enumerate(lines, 1):
            if CMD_UNCONDITIONAL_OK.match(line):
                out.append((where, idx,
                            "runs fallible commands but has no non-zero exit: "
                            "`exit /b 0` masks every failure"))

    # A `pause` is fine only when SICNU_NO_PAUSE can skip it.
    paused = [i for i, line in enumerate(lines, 1) if CMD_PAUSE.match(line)]
    if paused and not NO_PAUSE_GUARD.search(text):
        for idx in paused:
            out.append((where, idx,
                        "`pause` is not short-circuited by SICNU_NO_PAUSE; "
                        "unattended runs would block forever"))

    if "--out" in text:
        has_eq = bool(OUT_EQ.search(text))
        has_space = bool(OUT_SPACE.search(text))
        if has_eq and not has_space:
            out.append((where, 0,
                        "parses --out=<dir> but not `--out <dir>`; the two "
                        "spellings are both documented and must both work"))


def check_ps1(text, where, out):
    if "pause" in text.lower() and not NO_PAUSE_GUARD.search(text):
        out.append((where, 0, "blocking pause without a SICNU_NO_PAUSE guard"))
    # Deliberately conservative: an `exit 0` next to a guarded `exit 1` is a
    # normal success path, not a mask. What must never happen is a launcher
    # that has no failure exit at all — then every caller sees green.
    if not re.search(r"^\s*exit\s+[1-9]\d*\s*$", text, re.M):
        out.append((where, 0,
                    "no non-zero exit anywhere: the launcher cannot signal "
                    "failure to a caller"))


def check_sh(text, where, out):
    """A shell lane that runs tests must aggregate their exit codes."""
    if re.search(r"^\s*exit 0\s*$", text, re.M) and re.search(r"^\s*\"?\$", text, re.M):
        out.append((where, 0,
                    "unconditional `exit 0` after running test executables "
                    "masks failures; aggregate and propagate"))
    if "echo \"exit=$?\"" in text and "FAILED" not in text:
        out.append((where, 0,
                    "prints each test's exit code but never aggregates one; "
                    "the script still exits 0 when a test fails"))


def main():
    parser = argparse.ArgumentParser(
        description="static launcher parity gate (Windows + POSIX)")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when any launcher violates the contract")
    args = parser.parse_args()

    findings = []
    scanned = []
    for target in TARGETS:
        path = os.path.join(ROOT, target.replace("/", os.sep))
        if not os.path.isfile(path):
            findings.append((target, 0, "declared target is missing"))
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        scanned.append(target)
        if target.endswith(".cmd") or target.endswith(".bat"):
            check_cmd(text, target, findings)
        elif target.endswith(".ps1"):
            check_ps1(text, target, findings)
        else:
            check_sh(text, target, findings)

    windows = [t for t in scanned if t.endswith((".cmd", ".bat", ".ps1"))]
    print("check_launcher_parity: %d launcher(s) parsed, %d Windows lane(s)"
          % (len(scanned), len(windows)))
    for target in windows:
        print("  %s: NOT EXECUTED — static only" % target)

    if findings:
        print("check_launcher_parity: FAILED (%d finding(s))" % len(findings))
        for where, line, reason in findings:
            loc = "%s:%d" % (where, line) if line else where
            print("  %s: %s" % (loc, reason))
        return 1

    print("check_launcher_parity: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
