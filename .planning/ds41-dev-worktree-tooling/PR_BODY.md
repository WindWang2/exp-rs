# feat(dev): agent dev toolchain — worktree creator, build lock, review evidence (Track D5)

Baseline SHA: `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (live `origin/master` at track start and at PR creation — no drift during the track).

## Live dedup result (re-verified at PR time, not at brief time)

- Open PRs at PR creation: **9**, of which 8 are parallel tracks (#1116–#1123: geospatial fabric, data/experiment, model runtime, hyperspectral, offline labs, mission workbench, scientific verification, boundary fuzz) and one is this PR (#1124). None touches `scripts/dev/**` or `docs/development/**`.
- Open issues: **0**.
- File-level overlap of this PR's **30** paths against those PRs, remote branches, and sibling worktrees (produced by `scripts/dev/overlap_scan.py --from-HEAD`, the tool this PR adds, run on the final state): the ONLY overlapping path is `.gitignore`, and every hit is the same class of change.
  - **open PR #1120 (`agent/flash-offline-labs-12`) touches `.gitignore`** — verified by `gh pr diff 1120`: it appends its own `.planning/flash-offline-labs-12/` whitelist block at the same anchor (after the `ds41-range-cache-msvc` block).
  - **open PR #1123 (`agent/ds41-fuzz-boundaries`) touches `.gitignore`** — verified by `gh pr diff 1123`: it appends its own whitelist block at the same anchor (opened after this track's first dedup pass, before the PR).
  - Sibling worktrees `ds41-performance-observatory`, `flash-sar-polsar-12`, `flash-taskcenter-runtime-12`, `flash-temporal-phenology-12` each carry an uncommitted `.gitignore` whitelist append (in-flight work by other agents).
  - No overlap on `scripts/dev/**`, `docs/development/**`, or this track's `.planning` directory from any PR, branch, or worktree.
  - **Expected conflict hotspot: `.gitignore`** — every track appends a 4-line block at the same anchor. Both blocks are additive and semantically independent; the correct resolution is to keep both. No other file is touched by any other PR or branch.
- 13 historical `agent/*` / `fix/*` remote branches classified (`scripts/dev/stale_branches.py`, full table in `.planning/ds41-dev-worktree-tooling/BASELINE.md`): all superseded or residue; one (`agent/ds41-http-fetch-strict`) carries an unmerged regression test `tests/test_io_http_fetch.cpp` that its merged PR (#1110) dropped. Reported as a handoff, deliberately **not** ported — `tests/**` is outside this track's ownership.

## Scope

- `scripts/dev/**` (new): `preflight.py`, `overlap_scan.py`, `new_worktree.py`, `resource_guard.py`, `review_pack.py`, `stale_branches.py`, `dev_common.py`, `tests/` (40 hermetic unittest cases + `mutation_checks.py`), `README.md`.
- `docs/development/README.md` (new): the agent development guide — the five commands an agent runs, the fail-closed rules and exit-code contract, the `-j1/-j2` resource policy, the evidence policy.
- `.planning/ds41-dev-worktree-tooling/*.md`: GOAL archive, BASELINE, OWNERSHIP, DEDUP, ORACLES, DECISIONS, EVIDENCE, PR_PACK (whitelisted by the appended `.gitignore` block).
- `.gitignore`: one append-only 4-line whitelist block for the new `.planning` dir (conflict hotspot above).

## Non-goals

- No product source, no C++ change, no CMake change (the Build Portability track owns build inputs; this PR owns only the wrapper around build commands).
- No automatic deletion of branches, no PR close/merge, no force-push, no network CI: tools only read GitHub state through `gh` and record `not-executed` when it is unavailable.
- Porting the dropped `tests/test_io_http_fetch.cpp` regression from the stale branch (wrong owner; logged as a handoff instead).

## Design

Python 3 stdlib only, one entry point per work package, all output available as JSON. Key decisions and rejected alternatives are in `.planning/ds41-dev-worktree-tooling/DECISIONS.md` (D-001…D-011). Highlights:

- **Fail closed, exit codes as a contract.** `0` success (possibly degraded), `2` refused precondition, `75` busy (lock held), `1` failure. Refused operations create nothing — asserted per refusal, including a rollback test that a failed `git worktree add` deletes the branch it just created.
- **The build lock is `O_CREAT|O_EXCL` + holder metadata**, broken only when the recorded pid is dead *and* the lock is older than `--stale-after` (default 1 h). `time.monotonic()` and pid liveness are host-local by design: a build directory on a network share can only be reported busy, never stolen.
- **Parallelism is clamped mechanically**, not by memory: `cmake --build --parallel N`, `ninja -jN`, `make -jN` are clamped to ≤ 2 (a deliberate `-j1` pin is never raised), and `CMAKE_BUILD_PARALLEL_LEVEL` / `CTEST_PARALLEL_LEVEL` are exported for commands whose flags the guard does not recognize.
- **`gh` is optional.** Preflight/overlap/review-pack degrade to an explicit `not-executed` record; git-only features work offline.
- **Read-only git queries retry** because parallel agents fetching the same clone make `origin/*` refs momentarily unresolvable (observed live twice during this track; recorded in EVIDENCE.md).

## Issue / requirement mapping

No issue exists for this work (0 open issues at brief time and at PR time). Track brief: "OpenCode / DeepSeek v4.1 Flash Track D5 — Worktree/PR Hygiene, Local Review Automation & Agent Developer UX", work packages WP1–WP7 mapped to the six tools + the guide + the tests.

## Tests (each run twice; both runs green)

```
$ python -m unittest discover -s scripts/dev/tests -t scripts/dev
Ran 51 tests in 199.585s          # run 1
OK
$ python -m unittest discover -s scripts/dev/tests -t scripts/dev
Ran 51 tests in 206.356s          # run 2
OK

$ python scripts/dev/tests/mutation_checks.py      # run 1
mutation checks: 6/6 proved the tests can fail
$ python scripts/dev/tests/mutation_checks.py      # run 2
mutation checks: 6/6 proved the tests can fail
```

The mutation checks copy the tools, disable one guard each (dirty-master refusal, duplicate-branch refusal, existing-path refusal, parallelism clamp, concurrent-write exclusivity, staleness gating) and require the corresponding test class to fail on the mutated copy while passing on the pristine tree.

Oracle evidence against the **live** repository (not just fixtures) is in `.planning/ds41-dev-worktree-tooling/EVIDENCE.md`: preflight output with the real SHA/PR/worktree state; refusal of a dirty real master checkout (exit 2, `git worktree list` unchanged), duplicate-branch and existing-path refusals, and refusals of flag-shaped branch names (`--branch=-m`, `--branch=--unset-upstream`) with `HEAD` unchanged; a live concurrent-write refusal (second writer exit 75, its payload never ran; `--wait` variant serialized) and live `--parallel 8 → 2` / `-j9 → -j2` clamps; and a review pack generated for this very branch.

## Resources

No C++ build, no `ctest`, no full QGIS/OTB/ITK compile: the track's oracles are proven with stub payloads, so nothing here touches the build. The resource policy this PR enforces in tooling (`-j1` preferred, `-j2` max, `-j3+` forbidden) matches `CLAUDE.md`. The test suite drives subprocesses sequentially.

## Review disposition

Independent read-only review of `origin/master...HEAD` by a separate agent;
full record in `.planning/ds41-dev-worktree-tooling/REVIEW_LOG.md`.

- **P0 (1 found, 1 fixed):** a flag-shaped `--branch` (`-m`, `-c`, `--unset-upstream`, …) reached `git branch <name> <sha>` and renamed the repository's branch while reporting a no-op refusal. Fixed by `git check-ref-format --branch` validation before any mutation, `--` separators on every git argv carrying user input, and a verified rollback.
- **P1 (3 found, 3 fixed):** lock keyed on a non-canonical path (junction/relative-absolute spellings produced two locks) → `os.path.realpath` keying; an empty/corrupt lock could never be broken (permanent build-dir DoS) → atomic lock creation with the holder record plus mtime-age fallback; TOCTOU in stale-lock breaking → byte re-verification immediately before the unlink.
- **P1 (reviewer finding 4):** the read-only retry allowlist keyed on the bare subcommand, so the creating `git branch` and the destructive rollback `git branch -D` were retried up to 3× → allowlist now matches full argv patterns; mutating calls run exactly once.
- **P2 (6 found, 5 fixed, 1 partially):** inherited `MAKEFLAGS=-jN` defeated the cap → stripped; lock-creation failures reported as exit 75 "busy" → exit 1 with the real reason; unresolvable `--base` silently produced zero evidence → `not-executed`; `--json` returned before `--out` → both take effect; planning docs drifted from the code → corrected. The test-coverage finding is partially addressed (tests added for every fixed defect; two paths remain untested by design and are listed under Known limitations).
- **P3 (7 found, 5 fixed, 2 deferred):** double holder write removed; missing payload command refused instead of a traceback; rollback result verified and reported; lock mode `0600`; README/DECISIONS document the lock file. Deferred: the remote-less-repo failure wording (preflight already names the failing command) and the doc statement that remotes other than `origin` are not scanned for name conflicts.

Every fixed finding carries a regression test; the suite grew 40 → 51 cases and the mutation gate stayed 6/6 after the fixes (both gates run twice, green).

## Known limitations

- `preflight.py`/`overlap_scan.py`/`review_pack.py` read GitHub through `gh`; when `gh` or the network is unavailable they record `not-executed` rather than guessing. Verified live (7 open PRs seen at PR time; fixtures degrade cleanly).
- The lock is host-local (pid + monotonic clock). A build directory shared with another machine cannot be stolen — it stays busy (fail-closed). Documented in the tool's docstring.
- Name conflicts are detected against `origin` only; branches that exist solely on a second remote are not detected.
- The "worktree registered but HEAD unreadable" rollback branch and a run that actually reaches `git fetch` are not covered by automated tests (the first needs a filesystem-level fault injection; the second writes remote-tracking refs during the test run).
- One `.gitignore` integration hotspot with the other 2026-09-20 tracks (see Live dedup result): textual conflict on merge, resolved by keeping both blocks.
- `.planning/ds41-dev-worktree-tooling/GOAL_LOOP_LEDGER.md` stays uncommitted by design (DECISIONS D-010).
