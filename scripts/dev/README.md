# scripts/dev — agent development orchestration tools

Python 3 (stdlib only, no installs). Each tool is an independent entry point
usable from any worktree of this repository; shared helpers live in
`dev_common.py`. The agent-facing guide is
[`docs/development/README.md`](../../docs/development/README.md).

| Tool | Purpose | Key refusals / contracts |
|---|---|---|
| `preflight.py` | One-command repository state: trunk SHA, open/merged PRs and issues, worktrees with dirty counts, remote branches with ahead/behind | exit 2 outside a repo; `gh`/network degradation is recorded as `not-executed`, never hidden |
| `overlap_scan.py` | File-level overlap evidence for planned paths (open PRs, merged PRs, remote branches, sibling worktrees) | evidence only — no equivalence verdict; `--fail-on-overlap` exits 2 for gate use |
| `new_worktree.py` | Creates a branch + worktree from a freshly fetched base | refuses on dirty master, duplicate branch, existing/registered path, path inside the repo, unresolvable base; rolls the branch back if `worktree add` fails |
| `resource_guard.py` | Build lock + parallelism clamp around any command | second concurrent writer of one build dir gets exit 75 (or queues with `--wait`); `--parallel N`/`-jN` clamped to ≤ 2; stale locks broken only for a dead holder past `--stale-after` |
| `review_pack.py` | Reproducible PR evidence pack + PR-body draft | refuses to write an incomplete pack; deterministic modulo the `Generated:` line |
| `stale_branches.py` | Classifies remote branches (merged-equivalent / superseded / unmerged-increment / diverged-stale) with evidence | advisory only; deletes nothing, merges nothing |
| `narrow_targets.py` | Maps changed paths to the minimal build/test targets the CMake wiring actually names (plus the `test_build_wiring_drift` oracle for wiring changes) | mapping is mechanical from the wiring text — paths no target covers are reported `unwired`, never guessed |

Exit codes across the suite: `0` success (possibly degraded), `1` genuine
failure, `2` refused (fail-closed precondition), `75` busy (lock held).

The lock file is `<build-dir>.agent-build.lock` — a sibling of, never inside,
the build tree — keyed through `os.path.realpath` (one lock per directory
regardless of spelling), created atomically with its holder record (mode
`0600`), and breakable only when the recorded pid is dead and the record (or
the file's own mtime, for an empty/corrupt lock) is older than
`--stale-after`. If the build directory's parent does not exist yet, the
guard creates it once and then acquires.

## Usage

```bash
python scripts/dev/preflight.py [--json] [--fetch] [--path <dir>]
python scripts/dev/overlap_scan.py [paths...] [--from-HEAD] [--include-merged 40] [--json]
python scripts/dev/new_worktree.py --path <dir> --branch <name> [--base origin/master] \
    [--no-fetch] [--on-name-conflict suffix] [--json]
python scripts/dev/resource_guard.py --lock-dir <build-dir> [--wait 900] \
    [--parallel 2] [--status] [--json] -- <command...>
python scripts/dev/review_pack.py [--base origin/master] [--head HEAD] \
    [--title "..."] [--out PR_PACK.md] [--json]
python scripts/dev/stale_branches.py [--branch <name>] [--behind-threshold 50] [--json]
python scripts/dev/narrow_targets.py [paths...] [--base origin/master] [--json]
```

Every tool accepts `--json` for machine-readable output and prints a human
summary otherwise. All git access is read-only except `new_worktree.py`
(created via `git branch` + `git worktree add`) and the fetch inside
`preflight.py --fetch` / `new_worktree.py`.

## Tests

```bash
python -m unittest discover -s scripts/dev/tests -t scripts/dev   # hermetic, no network
python scripts/dev/tests/mutation_checks.py                       # proves tests can fail
```

Test fixtures build throwaway git repositories (`tests/_fixtures.py`); the
suite needs no build, no network, and no GitHub credentials. Tests that shell
out to the real tools use `run_tool`, so they exercise the same command line
an agent types.
