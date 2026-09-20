# feat(dev): agent dev toolchain — worktree creator, build lock, review evidence (Track D5)

Generated: 2026-09-20T02:39:05Z

## Baseline

- base `origin/master` @ `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (docs(agents): record Platform 5.0 audit request)
- head `HEAD` @ `a4800a307a98242dd2d23d24fb0ae75fdb98bdf5` (fix(dev): address independent review — P0 flag-injection, lock canonicalisation, atomic lock, retry policy)
- commits in range: 7

## Scope

- tests (10): `scripts/dev/tests/__init__.py`, `scripts/dev/tests/_fixtures.py`, `scripts/dev/tests/mutation_checks.py`, `scripts/dev/tests/test_dev_common.py`, `scripts/dev/tests/test_new_worktree.py`, `scripts/dev/tests/test_overlap_scan.py`, `scripts/dev/tests/test_preflight.py`, `scripts/dev/tests/test_resource_guard.py`, `scripts/dev/tests/test_review_pack.py`, `scripts/dev/tests/test_stale_branches.py`
- dev_tooling (8): `scripts/dev/README.md`, `scripts/dev/dev_common.py`, `scripts/dev/new_worktree.py`, `scripts/dev/overlap_scan.py`, `scripts/dev/preflight.py`, `scripts/dev/resource_guard.py`, `scripts/dev/review_pack.py`, `scripts/dev/stale_branches.py`
- docs (7): `.planning/ds41-dev-worktree-tooling/BASELINE.md`, `.planning/ds41-dev-worktree-tooling/DECISIONS.md`, `.planning/ds41-dev-worktree-tooling/DEDUP.md`, `.planning/ds41-dev-worktree-tooling/GOAL.md`, `.planning/ds41-dev-worktree-tooling/ORACLES.md`, `.planning/ds41-dev-worktree-tooling/OWNERSHIP.md`, `docs/development/README.md`
- other (1): `.gitignore`

## Non-goals

<!-- TODO: what this PR deliberately does not do -->

## Design

<!-- TODO: design notes and alternatives rejected -->

## Issue mapping

<!-- TODO: issue numbers this PR closes or relates to -->

## Tests

<!-- TODO: exact commands and their output, run twice -->

## Resources

<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->

## Review disposition

<!-- TODO: reviewer findings and their fix state -->

## Known limitations

<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->

## Conflict hotspots

overlap evidence for this diff's 26 paths:
- open_prs: 2
  - `agent/ds41-fuzz-boundaries` touches 1 of these paths
  - `agent/flash-offline-labs-12` touches 1 of these paths
- merged_prs: 0
- remote_branches: 2
  - `origin/agent/ds41-fuzz-boundaries` touches 1 of these paths
  - `origin/agent/flash-offline-labs-12` touches 1 of these paths
- sibling_worktrees: 4
  - `refs/heads/agent/ds41-performance-observatory` touches 1 of these paths
  - `refs/heads/agent/flash-sar-polsar-12` touches 1 of these paths
  - `refs/heads/agent/flash-taskcenter-runtime-12` touches 1 of these paths
  - `refs/heads/agent/flash-temporal-phenology-12` touches 1 of these paths

---

## Diff stat

```
.gitignore                                       |   4 +
 .planning/ds41-dev-worktree-tooling/BASELINE.md  |  73 +++++
 .planning/ds41-dev-worktree-tooling/DECISIONS.md | 136 +++++++++
 .planning/ds41-dev-worktree-tooling/DEDUP.md     |  55 ++++
 .planning/ds41-dev-worktree-tooling/GOAL.md      | 156 ++++++++++
 .planning/ds41-dev-worktree-tooling/ORACLES.md   |  75 +++++
 .planning/ds41-dev-worktree-tooling/OWNERSHIP.md |  34 +++
 docs/development/README.md                       |  76 +++++
 scripts/dev/README.md                            |  57 ++++
 scripts/dev/dev_common.py                        | 273 +++++++++++++++++
 scripts/dev/new_worktree.py                      | 238 +++++++++++++++
 scripts/dev/overlap_scan.py                      | 260 ++++++++++++++++
 scripts/dev/preflight.py                         | 227 ++++++++++++++
 scripts/dev/resource_guard.py                    | 370 +++++++++++++++++++++++
 scripts/dev/review_pack.py                       | 240 +++++++++++++++
 scripts/dev/stale_branches.py                    | 244 +++++++++++++++
 scripts/dev/tests/__init__.py                    |   0
 scripts/dev/tests/_fixtures.py                   | 107 +++++++
 scripts/dev/tests/mutation_checks.py             | 180 +++++++++++
 scripts/dev/tests/test_dev_common.py             | 124 ++++++++
 scripts/dev/tests/test_new_worktree.py           | 238 +++++++++++++++
 scripts/dev/tests/test_overlap_scan.py           |  71 +++++
 scripts/dev/tests/test_preflight.py              |  78 +++++
 scripts/dev/tests/test_resource_guard.py         | 339 +++++++++++++++++++++
 scripts/dev/tests/test_review_pack.py            |  88 ++++++
 scripts/dev/tests/test_stale_branches.py         |  86 ++++++
 26 files changed, 3829 insertions(+)
```

## Commits

```
a4800a307 fix(dev): address independent review — P0 flag-injection, lock canonicalisation, atomic lock, retry policy
46423e9d2 fix(dev): create the lock's parent directory instead of reporting busy
5a2271810 fix(dev): route tool failures through exit codes instead of tracebacks
3587aa77e fix(dev): trunk_ref must return the origin/* ref, not the bare branch name
77437c146 docs(dev): agent development guide for worktrees, locks and review evidence
833a58397 feat(dev): agent worktree, build-lock and review-evidence toolchain
8a3087035 chore(dev): track D5 planning baseline, ownership and oracles
```
