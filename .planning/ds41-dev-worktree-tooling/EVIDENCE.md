# EVIDENCE — ds41-dev-worktree-tooling (Track D5)

Every claim below is a local command with its exit code, or an explicit
`not-executed`. Timestamps are UTC. Environment: Windows 10 (host of the
track), Python 3.13.9, git 2.53.0. **No C++ build and no ctest ran at any
point** — this track adds no C++ and every oracle is proven with stub
payloads, per ORACLES.md "Non-oracle clarifications".

## Phase 0 (preflight & dedup)

| Command | Result |
|---|---|
| `git fetch origin --prune` | rc=0 (first attempt from the sandboxed shell failed to reach github.com:443; succeeded unsandboxed — recorded as an environment fact, BASELINE.md) |
| `git rev-parse origin/master` | `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (2 commits ahead of the brief's snapshot `2761a6857`) |
| `gh pr list --state open --limit 100` | rc=0, **0 open PRs** at 00:30 UTC |
| `gh issue list --state open --limit 200` | rc=0, **0 open issues** at 00:30 UTC |
| `gh pr list --state merged --limit 80` | rc=0; #1115 … #1070 (table in BASELINE.md) |
| `git branch -r` | 14 refs: master + 13 `agent/*`/`fix/*` |
| ahead/behind per branch (`git rev-list --count`) + `git cherry` + content spot checks | every one of the 13 is superseded or residue (BASELINE.md table) |
| Worktree creation | `git worktree add ../exp-rs-worktrees/ds41-dev-worktree-tooling -b agent/ds41-dev-worktree-tooling origin/master` → HEAD `adf8f9895`, rc=0 |
| `.gitignore` whitelist | `git add` without `-f` accepts `.planning/ds41-dev-worktree-tooling/*.md`, refuses `*.log` (probe files removed after) |
| Re-check before finishing (7.5 h later, 7 new open PRs had appeared) | `git fetch origin --prune` rc=0, master still `adf8f9895`; branch is 3 ahead / 0 behind; open PRs #1116–#1122 are all product tracks; `overlap_scan.py` finds only `.gitignore` (per-track whitelist appends) — PR #1120's `.gitignore` hunk confirmed as a same-anchor append (conflict hotspot, see PR body) |

## Gate A — full tool test suite (run twice on the final code, both green)

```
$ python -m unittest discover -s scripts/dev/tests -t scripts/dev
Ran 51 tests in 199.585s
OK
$ python -m unittest discover -s scripts/dev/tests -t scripts/dev
Ran 51 tests in 206.356s
OK
```

An earlier run of the same gate on the pre-review code (40 tests) also passed
twice; the growth 40 → 51 is entirely regression tests for the independent
review's findings (see REVIEW_LOG.md).

## Gate B — mutation checks (run twice, both green: 6/6)

```
$ python scripts/dev/tests/mutation_checks.py
pass dirty-master-refusal: mutated rc=1 (required != 0), pristine rc=0 (required 0)
pass duplicate-branch-refusal: mutated rc=1 … pristine rc=0
pass existing-path-refusal: mutated rc=1 … pristine rc=0
pass parallelism-clamp: mutated rc=1 … pristine rc=0
pass concurrent-write-refusal: mutated rc=1 … pristine rc=0
pass stale-lock-safety: mutated rc=1 … pristine rc=0
mutation checks: 6/6 proved the tests can fail
```
(run twice on the final code, identical; the two patterns that targeted the
pre-rewrite lock internals were re-pointed at the current code). Each row copies the tools to a temp dir, disables one
guard, runs the corresponding test class against the mutated copy AND the
pristine tree.

## Oracle 1 — preflight on this repository (real, read-only)

`python scripts/dev/preflight.py` → rc=0:

```
preflight @ 2026-09-20T00:28:49Z
  repository : C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\ds41-dev-worktree-tooling
  master     : adf8f9895242 docs(agents): record Platform 5.0 audit request  (2026-09-20T00:27:12+08:00)
  from branch: agent/ds41-dev-worktree-tooling (HEAD 77437c1467a0, 1 dirty entries)
  open PRs   : 7   open issues: 0
    merged #1115 … #1110 (titles listed)
  worktrees  : 11   (each with branch, path, dirty count; main checkout dirty=58)
  remote non-master branches: 13 (ahead/behind per branch)
```

`master_sha` equals `git rev-parse origin/master` (asserted by test_preflight's
fixture equivalent; here compared by eye — identical string). Degradation path:
`PATH= python scripts/dev/preflight.py --json` records
`github.open_prs.status == "not-executed"` with rc=0 (covered by
`test_preflight.PreflightFixtureTest.test_github_degrades_to_not_executed`).

## Oracle 2 — worktree creation fails closed (real repository attempts)

| Attempt | Result |
|---|---|
| dirty real master checkout (`C:/Users/wangj.KEVIN/projects/exp-rs`, on master, 58 dirty entries) | rc=2, `refused: master checkout … is dirty (58 entries): …`; `git worktree list` unchanged (11), no path created, no branch created |
| duplicate branch `agent/ds41-dev-worktree-tooling` | rc=2, `refused: branch already exists (local branch): agent/ds41-dev-worktree-tooling`; branch/worktree counts unchanged |
| existing/registered path (this worktree's own directory) | rc=2, `refused: target path already exists: …` |
| path inside the repo, unresolvable base, clean-master bootstrap, suffix adoption | covered by fixtures: `tests/test_new_worktree.py` (7 refusal tests + happy path + rollback), all green |

## Oracle 3 — resource guard refuses/serializes concurrent writers (live)

Live proof on this host (payload = python that writes a start marker, sleeps
2 s, then prints):

```
$ resource_guard.py --lock-dir <build> --json -- <sleeping-payload> &     # t=0
$ sleep 0.7
$ resource_guard.py --lock-dir <build> -- python -c "print('SECOND PAYLOAD RAN - BAD')"
refused: build dir … is locked by pid 51596 (waited 0.0s); another build is writing it
second rc=75   (second payload never printed)
$ sleep 2.5 ; ls <build>.agent-build.lock   →  gone (released after the first finished)
$ resource_guard.py --lock-dir <build> --wait 5 -- python -c "print('BOTH-RAN-OK')"
BOTH-RAN-OK
wait rc=0
```

Parallelism clamp (live, stub payload echoes argv):

```
$ resource_guard.py --lock-dir <b> -- <stub> cmake --build b --parallel 8
resource-guard: clamped parallelism: --parallel 8 -> 2        # stub received --parallel 2
$ resource_guard.py --lock-dir <b> -- <stub> ninja -j9 -C b
resource-guard: clamped parallelism: -j9 -> -j2               # stub received -j2
$ resource_guard.py --lock-dir <b> -- <stub> ninja -j1 -C b    # no clamp: -j1 survives
```

Not-executed items: no real `cmake`/`ninja`/`ctest` invocation ran (no C++
build in this track's oracles); the clamp is verified on the argv the stub
received.

## Oracle 4 — review pack on this branch

`python scripts/dev/review_pack.py --base origin/master --head HEAD --out PR_PACK.md`
→ rc=0, `review pack written … (26 changed files, 3 commits, diff --check clean)`;
all 10 required section headings present, baseline/head SHAs and commit list
included, conflict-hotspots section populated from the live overlap scan.
Determinism asserted by `test_review_pack.ReviewPackTest.test_reproducible_modulo_generated_line`.

## Static checks

| Command | Result |
|---|---|
| `python -m compileall -q scripts/dev` | rc=0 |
| `git diff --check` | rc=0, no output |
| referenced-path existence assertion (goal-template.md §存在性断言) over the new docs | all `.agents/skills/code-review/SKILL.md`, `docs/agents/*.md`, `docs/development/README.md`, `scripts/dev/README.md` exist |
| weasel-word scan `grep -E "尽量|适当|必要时|合理|充分|酌情"` | 0 hits |
| `git status --porcelain` before each commit | inspected; see incident note below |

## Incident observed during this track (concurrency hazard, recorded as motivation)

While committing the toolchain, an inspection of `git status --porcelain`
revealed ~13,000 files staged as additions in this worktree's index, including
the whole `.agents/` tree; the commit that followed created a **root commit
containing the entire repository** (13,174 files, 3.48 M insertions) even though
the branch had a parent. Recovery: `git reset --soft 8a3087035` (the planning
commit, still reachable from the reflog) + `git restore --staged .`, then two
clean commits (`8a3087035`, `833a58397`, `77437c146`) verified file-by-file with
`git diff --cached --name-only` before committing. The origin of the foreign
staging is outside this session (the fleet grew to 11 worktrees during the
same window and other agents run git commands on the same clone); this is
direct evidence for the Track's premise — a commit-hygiene pre-check (verify
`git diff --cached --name-only` is exactly the intended paths) is now a
documented step in the guide. No data was lost; the recovery is reproducible
from this log.

Second instance of the same hazard class, milder: two concurrent `git fetch
--prune` processes (other agents, same clone) made `origin/*` refs
momentarily unresolvable ("fatal: ambiguous argument") for a reader seconds
before they resolved again — the reason `dev_common.run_git` retries read-only
queries.
