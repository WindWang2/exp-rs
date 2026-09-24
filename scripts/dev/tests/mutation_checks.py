"""mutation_checks.py — prove the oracle tests fail when the guard is removed.

A test that never fails proves nothing. For each fail-closed behaviour this
driver copies scripts/dev to a scratch directory, applies one targeted
mutation that disables exactly one guard, runs the corresponding test class
against the mutated copy, and requires the run to FAIL. Then it re-runs the
class against the pristine copy and requires it to PASS.

Usage:
    python scripts/dev/tests/mutation_checks.py            # all mutations
    python scripts/dev/tests/mutation_checks.py --filter dirty
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

DEV_DIR = Path(__file__).resolve().parents[1]


@dataclass
class Mutation:
    name: str
    tool: str
    test_module: str
    test_class: str
    old: str
    new: str


MUTATIONS = [
    Mutation(
        name="dirty-master-refusal",
        tool="new_worktree.py",
        test_module="test_new_worktree.py",
        test_class="RefusalTest",
        old='''        if dirty:
            preview = "; ".join(dirty[:5]) + (" …" if len(dirty) > 5 else "")
            return _refuse(''',
        new='''        if False:
            preview = "; ".join(dirty[:5]) + (" …" if len(dirty) > 5 else "")
            return _refuse(''',
    ),
    Mutation(
        name="duplicate-branch-refusal",
        tool="new_worktree.py",
        test_module="test_new_worktree.py",
        test_class="RefusalTest",
        old='''    existing = _branch_exists(repo, args.branch)
    if existing:''',
        new='''    existing = _branch_exists(repo, args.branch)
    if False and existing:''',
    ),
    Mutation(
        name="existing-path-refusal",
        tool="new_worktree.py",
        test_module="test_new_worktree.py",
        test_class="RefusalTest",
        old='''    if target.exists():
        return _refuse(f"target path already exists: {target}")''',
        new='''    if False:
        return _refuse(f"target path already exists: {target}")''',
    ),
    Mutation(
        name="parallelism-clamp",
        tool="resource_guard.py",
        test_module="test_resource_guard.py",
        test_class="ParallelismClampTest",
        old='''    out = list(argv)
    changes: list[str] = []''',
        new='''    out = list(argv)
    changes: list[str] = []
    return out, changes''',
    ),
    Mutation(
        name="concurrent-write-refusal",
        tool="resource_guard.py",
        test_module="test_resource_guard.py",
        test_class="ConcurrencyTest",
        # Realistic defect: lock creation stops being exclusive (the
        # os.O_CREAT|O_EXCL / atomic os.link hand-off replaced by a plain
        # create-if-missing), so two writers can both "acquire".
        old='''            os.link(str(staged), str(lock_path))''',
        new='''            os.open(str(lock_path), os.O_CREAT | os.O_RDWR, 0o600)''',
    ),
    Mutation(
        name="stale-lock-safety",
        tool="resource_guard.py",
        test_module="test_resource_guard.py",
        test_class="LockLifecycleTest",
        # Realistic defect class: "every existing lock is stale" — i.e. no
        # staleness gating at all (neither the pid gate nor the age gate,
        # including the mtime fallback for unreadable records). The
        # live-holder test must catch that.
        old='''    holder = _read_holder(path)
    pid = holder.get("pid")
    if isinstance(pid, int) and pid_alive(pid):
        return False, holder
    started = holder.get("started_monotonic")
    if isinstance(started, (int, float)):
        age = time.monotonic() - started
    else:
        try:
            age = time.time() - path.stat().st_mtime
        except OSError:
            return False, holder
    if age < stale_after:
        return False, holder
    return True, holder''',
        new='''    return True, holder''',
    ),
    Mutation(
        name="narrow-target-exact-match",
        tool="narrow_targets.py",
        test_module="test_narrow_targets.py",
        test_class="NarrowTargetsTest",
        # Realistic defect: the exact source-list index stops contributing
        # hits, so mappings silently degrade to weak/unwired and the
        # "build only what changed" contract is gone.
        old='''    for raw in paths:
        rel = raw
        strong_hits = list(by_rel.get(rel, []))''',
        new='''    for raw in paths:
        rel = raw
        strong_hits: list[str] = []''',
    ),
]


def run_class(copy_root: Path, mutation: Mutation) -> subprocess.CompletedProcess:
    env_dir = copy_root / "scripts" / "dev"
    tests_dir = env_dir / "tests"
    return subprocess.run(
        [sys.executable, "-m", "unittest",
         f"tests.{mutation.test_module[:-3]}.{mutation.test_class}"],
        cwd=str(env_dir), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", timeout=600,
    )


def main(argv: list[str] | None = None) -> int:
    only = None
    if argv and argv[0] == "--filter":
        only = argv[1]

    results = []
    for mutation in MUTATIONS:
        if only and only not in mutation.name:
            continue
        with tempfile.TemporaryDirectory(prefix="devtools-mutation-") as tmp:
            copy_root = Path(tmp) / "repo"
            shutil.copytree(DEV_DIR, copy_root / "scripts" / "dev")
            tool = copy_root / "scripts" / "dev" / mutation.tool
            text = tool.read_text(encoding="utf-8")
            if mutation.old not in text:
                print(f"SKIP {mutation.name}: pattern no longer matches {mutation.tool}")
                results.append((mutation.name, "skipped"))
                continue
            tool.write_text(text.replace(mutation.old, mutation.new, 1),
                            encoding="utf-8")
            mutated_run = run_class(copy_root, mutation)
            # the pristine run uses the real tree, so it exercises the real guard
            pristine = subprocess.run(
                [sys.executable, "-m", "unittest",
                 f"tests.{mutation.test_module[:-3]}.{mutation.test_class}"],
                cwd=str(DEV_DIR), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=600,
            )
            failed_as_required = mutated_run.returncode != 0
            passed_pristine = pristine.returncode == 0
            verdict = "pass" if (failed_as_required and passed_pristine) else "FAIL"
            results.append((mutation.name, verdict))
            print(f"{verdict:4} {mutation.name}: mutated rc={mutated_run.returncode} "
                  f"(required != 0), pristine rc={pristine.returncode} (required 0)")
            if verdict == "FAIL":
                tail = "\n".join((mutated_run.stdout or "").splitlines()[-8:])
                print("  mutated-run tail:\n    " + tail.replace("\n", "\n    "))

    bad = [name for name, verdict in results if verdict != "pass"]
    print()
    print(f"mutation checks: {len(results) - len(bad)}/{len(results)} proved the "
          f"tests can fail")
    if bad:
        print("NOT PROVEN: " + ", ".join(bad))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
