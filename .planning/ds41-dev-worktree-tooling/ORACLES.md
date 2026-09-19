# ORACLES — ds41-dev-worktree-tooling (Track D5)

Every oracle is a command with an expected observable result. "Done" is defined by these, not
by judgment. Tool entry points live in `scripts/dev/`; the test suite is
`scripts/dev/tests/` (Python `unittest`, stdlib only, hermetic temp-repo fixtures — no network
required unless a test name says so).

## Oracle 1 — Preflight reports the real repository state

| Item | Command | Expected |
|---|---|---|
| Live run on this repo | `python scripts/dev/preflight.py --json` | exit 0; JSON contains `master_sha` = current `git rev-parse origin/master`, `open_prs`/`open_issues` counts (0/0 at baseline), `merged_recent` list, `worktrees` with this track's worktree present, `historical_branches` with non-negative ahead/behind for all 13 `agent/*`/`fix/*` branches |
| Consistency with source of truth | `python scripts/dev/preflight.py --json \| python -c "import json,sys;d=json.load(sys.stdin);assert d['master_sha'].startswith('adf8f989')"` (SHA refreshed at run time via `git rev-parse origin/master`) | exit 0 — preflight's SHA equals git's and gh's HEAD |
| Degradation (offline/gh missing) | `PATH= python scripts/dev/preflight.py --json` (no `gh` on PATH) | exit 0; `github` section reports `"available": false` and every PR/issue field is marked `not-executed` instead of crashing |
| Determinism | two consecutive runs, compare after removing timestamps | identical payloads |

Covered by: `scripts/dev/tests/test_preflight.py` (fixture-repo unit tests + live smoke test
marked `network` and skipped when `gh`/network is unavailable).

## Oracle 2 — Worktree creation fails closed on unsafe conditions

| Item | Command | Expected |
|---|---|---|
| Duplicate branch | `python scripts/dev/new_worktree.py --path <dir> --branch agent/ds41-dev-worktree-tooling` (branch exists) | non-zero exit; message names the existing ref; **no branch, no worktree, no path created** (verified by `git worktree list` unchanged and the path still absent) |
| Existing path | `python scripts/dev/new_worktree.py --path ../exp-rs-worktrees/ds41-dev-worktree-tooling --branch <fresh-name>` | non-zero exit; message says the path exists / is registered; nothing created |
| Dirty tree | fixture repo with a modified tracked file | non-zero exit listing dirty entries; `git worktree list` unchanged |
| On `master` | fixture repo with `master` checked out | non-zero exit refusing to branch from a `master` checkout |
| Happy path | fixture repo, clean, `--branch t/x --path wt-x` | exit 0; worktree created at fresh `origin/master`-equivalent base; `git -C wt-x rev-parse HEAD` equals base; prints JSON summary |
| Rollback | fixture repo where `git worktree add` is forced to fail (stub git on PATH) | non-zero exit; the branch created moments earlier is removed (no orphan branch left) |

Covered by: `scripts/dev/tests/test_new_worktree.py` (hermetic temp repos; `git` stub for the
rollback case) plus a real-repo negative case (duplicate branch, read-only attempt).

## Oracle 3 — Resource guard serializes/refuses concurrent writers of one build dir

| Item | Command | Expected |
|---|---|---|
| Fail-fast refusal | two `python scripts/dev/resource_guard.py --lock-dir <d> -- <sleep-cmd>` started simultaneously | exactly one exits 0; the other exits with the documented "busy" code (75) and prints the holder's pid/command/age; second process did **not** run the payload |
| Serialization | same pair with `--wait` | both exit 0; the second starts only after the first releases (observed via each process's start/end timestamps) |
| Parallelism clamp | `python scripts/dev/resource_guard.py --lock-dir <d> -- cmake --build b --parallel 8` (stub cmake that echoes its args) | stub receives `--parallel 2` or lower; `ninja -j9`/`make -j9` are clamped to 2; env forces `CMAKE_BUILD_PARALLEL_LEVEL`/`CTEST_PARALLEL_LEVEL` ≤ 2 |
| Stale lock recovery | lock file left behind by a dead pid, age > threshold | guard breaks it, records the break in the lock file, proceeds; live holder is never broken |
| Release on failure | payload exits non-zero | guard releases the lock (next acquisition succeeds immediately) and propagates the payload's exit code |

Covered by: `scripts/dev/tests/test_resource_guard.py` (subprocess-level concurrency; stub
payloads that write timestamps).

## Oracle 4 — Review pack produces complete, reproducible PR evidence

| Item | Command | Expected |
|---|---|---|
| Fixture branch | fixture repo with one commit ahead of base | `python scripts/dev/review_pack.py --base main --head feature --out pack.md` writes a pack containing: baseline SHA, head SHA, diff stat, changed public headers, changed tests, `git diff --check` verdict (clean), conflict scan vs sibling branches, and a PR-body draft with every required section heading |
| Required sections present | `grep -c '^## ' pack.md` and section-name check | all of: Baseline / Scope / Design / Issue mapping / Tests / Resources / Review / Known limitations / Conflict hotspots / Non-goals |
| This track | run in the worktree on `origin/master...HEAD` before push | pack matches the PR body actually used; numbers equal `git diff --stat origin/master...HEAD` |
| Reproducibility | run twice, byte-compare after stripping the timestamp line | identical |

Covered by: `scripts/dev/tests/test_review_pack.py`.

## Tool-suite gates (run twice before completion)

| Gate | Command | Expected |
|---|---|---|
| Full unit suite | `python -m unittest discover -s scripts/dev/tests -t . -v` (from worktree root) | all tests pass, rc=0 |
| Static/lint | `python -m compileall -q scripts/dev` and `git diff --check` | rc=0, no output |
| Mutation sanity (tests can fail) | for each guard test class, disable the guard in a scratch copy and re-run that class | the class fails — proves the test asserts the guard, not the environment |

## Non-oracle clarifications

- No full QGIS/OTB/ITK build and no `ctest` run is part of any oracle: this track changes no
  C++ and its oracles are proven with stub commands. The `-j1/-j2` resource policy is enforced
  and *verified in the guard itself* (Oracle 3, clamp test), which is the only place this track
  touches build invocation.
- `QT_QPA_PLATFORM=offscreen` is irrelevant to Python tooling; no Qt process is started.
