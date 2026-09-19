# DECISIONS — ds41-dev-worktree-tooling (Track D5)

Autonomy is `full`; every technical fork encountered is recorded here with the rejected
alternatives, per `.agents/AGENTS.md` unattended-mode rule.

## D-001 — Tooling language: Python 3 (stdlib only), in `scripts/dev/`

- **Context:** the repo is C++20 + Qt6 for product code, but `scripts/` already carries Python
  tooling (`verification_ladder.py`, `capability_enrichment.py`, …) and the brief restricts
  this track to dev orchestration.
- **Decision:** Python 3.13, stdlib only (no pip installs, no new dependencies —
  `docs/agents/goal-template.md` dependency rule), one entry point per work package.
- **Alternatives rejected:**
  - C++ + Catch2 in `tests/`: would need shared CMake registration, a full configure pass, and
    `-j1/-j2` rebuilds for every iteration — directly conflicts with the resource policy, and
    C++ buys nothing for orchestrating `git`/`gh`.
  - Pure shell (bash): the primary host is Windows; shell portability (quotings, `O_EXCL`
    locks, process liveness) would double the code size, and two of the six tools need JSON
    output.
  - PowerShell: excellent on Windows, poor on the Linux/macOS machines used by other tracks.

## D-002 — Locking primitive: `os.open(..., O_CREAT|O_EXCL)` lock file, not `fcntl`/`msvcrt`

- **Context:** the guard must work on Windows (this host), Linux and macOS.
- **Decision:** create-with-exclusive-flag lock file inside a per-build-dir `.devlock`
  directory; holder metadata (pid, host, boot-relative start time, command) written into the
  lock file for diagnostics; stale-lock breaking requires the recorded pid to be dead *and* the
  lock age to exceed `--stale-after` (default 3600 s). PID liveness uses `os.kill(pid, 0)` on
  POSIX and `OpenProcess` via `ctypes` on Windows.
- **Alternatives rejected:** `fcntl.flock` (POSIX only), `msvcrt.locking` (Windows only),
  directory-rename tricks (not atomic across platforms).

## D-003 — Guard default: fail-fast (exit 75), `--wait` for serialization

- **Context:** the oracle requires "拒绝/序列化" (refused **or** serialized).
- **Decision:** default refuses immediately with exit code 75 (`EX_TEMPFAIL`) so CI-like callers
  can branch on it; `--wait <seconds>` (default 900) serializes with a bounded wait; both paths
  are tested. The payload never starts until the lock is held.
- **Alternative rejected:** silent queue with no bound — hides contention and can deadlock a
  pipeline forever.

## D-004 — Parallelism clamping in the guard, not in a separate config

- **Context:** brief mandates `-j1` preferred, `-j2` max, `-j3+` forbidden.
- **Decision:** the guard rewrites known parallelism flags of the wrapped command
  (`cmake --build … --parallel N`, `ninja -jN`, `make -jN`, `cmake --build -j N`) down to the
  configured cap (default 2, `--parallel 1` to pin), and exports
  `CMAKE_BUILD_PARALLEL_LEVEL` / `CTEST_PARALLEL_LEVEL` to the same value. Unrecognized
  commands run unchanged — the guard is a lock + clamp, not a build-system emulator.
- **Alternative rejected:** only setting env vars — `ninja -j9` on the command line still wins
  over the environment.

## D-005 — Worktree creator: fail closed on every precondition, one escape hatch

- **Context:** brief: "防重名、防在 master 写入"; track-level rule allows a dated/short-SHA name
  when the desired one exists.
- **Decision:** refusals (exit 2, distinct messages, nothing created): current branch is
  `master`; working tree dirty; branch name exists locally or on the remote; target path exists
  or is already registered as a worktree; base ref unresolvable (after fetch). The single
  escape hatch is `--on-name-conflict suffix`, which adopts `<name>-<YYYYMMDD>-<short-sha>`
  instead of failing — it still never reuses or overwrites an existing branch or path.
  Creation is two-phase (`git branch` then `git worktree add`); if `worktree add` fails the
  creator deletes the branch it just made (rollback, verified by test).
- **Alternative rejected:** auto-force (`-B`) — it silently discards an existing branch, which
  is exactly the accident this track exists to prevent.

## D-006 — `gh` is optional; every tool degrades to `not-executed`

- **Context:** `gh` was reachable from the sandbox during baseline work, but network access to
  `github.com:443` was blocked in sandboxed shells while `api.github.com` was not — access is
  not guaranteed at the time a tool runs.
- **Decision:** tools shell out to `gh` when present and report `github_available: false` plus
  `not-executed` markers otherwise; git-only features (worktree/branch/diff data) always work
  offline. Exit code stays 0 for degraded-but-successful runs; only a requested-and-impossible
  operation fails.
- **Alternative rejected:** hard dependency on `gh` — would make preflight unusable offline,
  which is the state most agents hit first.

## D-007 — Read-only git queries retry briefly (concurrent-fetch flakiness)

- **Context:** observed live: five overlapping `git fetch --prune` processes from parallel
  agents on the same clone made `origin/*` refs momentarily unresolvable from another process
  (`fatal: ambiguous argument`), resolving seconds later.
- **Decision:** a `run_git` helper retries read-only arguments (rev-parse/log/branch) up to 3
  times with short backoff, then fails with the last error verbatim. Writes (worktree add,
  branch create) are never retried — a partially applied write is a fail-closed event.
- **Alternative rejected:** no retry — preflight would intermittently report phantom
  "missing" branches under the exact concurrency this track targets.

## D-008 — Overlap scanner reports evidence, never equivalence

- **Context:** brief: "只作为 evidence，不自动判定代码等价".
- **Decision:** the scanner lists (a) open PRs touching the planned paths (via `gh`),
  (b) remote branches whose commit-set touches those paths (with ahead/behind and the
  overlapping file list), (c) sibling worktrees' uncommitted changes touching those paths.
  Each hit carries its source and paths; no "same/different" verdict is emitted.
- **Alternative rejected:** semantic similarity scoring — unverifiable automatically and
  explicitly out of scope.

## D-009 — Review pack is deterministic and section-complete

- **Decision:** fixed section order; all volatile values (timestamps) confined to one
  `Generated:` line so two runs are byte-identical after stripping it; a self-check asserts
  every required heading exists before writing (missing heading = failure, not a silently
  truncated pack). The PR body draft embeds the machine-collected evidence and leaves only
  narrative fields (design text, issue mapping) as `TODO` markers for the author.
- **Alternative rejected:** free-form generation — non-reproducible evidence, which the brief
  forbids ("完整、可复现的 PR evidence").

## D-010 — No `.goal-loop-ledger.md` in the shared repo

- **Context:** the brief says the ledger stays in the worktree unless repo norms require
  otherwise; `.planning` whitelists only `*.md` per track, and the ledger is iteration noise,
  not a stable fact.
- **Decision:** keep the ledger at `.planning/ds41-dev-worktree-tooling/GOAL_LOOP_LEDGER.md`
  inside the worktree and do **not** `git add` it. The committed planning set is
  GOAL/BASELINE/OWNERSHIP/DEDUP/ORACLES/DECISIONS + EVIDENCE/REVIEW_LOG/PR_BODY.
- **Alternative rejected:** committing the ledger — it would churn a shared file every round
  for no reader.

## D-011 — Tool placement and naming

- **Decision:** `scripts/dev/preflight.py`, `overlap_scan.py`, `new_worktree.py`,
  `resource_guard.py`, `review_pack.py`, `stale_branches.py`, plus `dev_common.py`
  (shared git/gh helpers, retry, JSON output, exit codes) and `scripts/dev/tests/`
  (unittest). A thin `scripts/dev/README.md` documents the commands; the user-facing guide is
  `docs/development/agent-dev-tooling.md`.
- **Alternative rejected:** a single `devtools.py` with subcommands — six independent entry
  points are independently invocable from any track's worktree and independently testable.
