# Agent Development Guide — worktrees, locks, review evidence

This directory is the implementation-side companion to the prompt-side
conventions in [`docs/agents/`](../agents/): `goal-template.md` defines how a
track brief is written, `loop-template.md` how a recurring loop is specified,
`command-vocabulary.md` which of the two a task needs. This guide covers what
an agent **does** with tools in `scripts/dev/` while executing a track, so that
ten agents working in parallel stop stepping on each other.

Nothing here invents new policy; the tools enforce the existing one.

## The five commands an agent runs

| Step | Command | Why |
|---|---|---|
| 1. Orient | `python scripts/dev/preflight.py --json` | See live master, open PRs/issues, every worktree and its dirty state, and historical branches with ahead/behind — before touching anything. |
| 2. Check overlap | `python scripts/dev/overlap_scan.py <paths> --json` | Evidence of who else touches your planned files: open PRs, remote branches, sibling worktrees' uncommitted work. Read it, decide yourself — the tool never claims equivalence. |
| 3. Create your worktree | `python scripts/dev/new_worktree.py --path ../exp-rs-worktrees/<track> --branch agent/<track>` | Fails closed on a dirty master checkout, a duplicate branch name, an existing/registered path, or an unresolvable base. Never writes to master. |
| 4. Build/test under the lock | `python scripts/dev/resource_guard.py --lock-dir build-dev -- cmake --build build-dev --parallel 8` | One writer per build directory (a second concurrent writer gets exit 75, or queues behind `--wait`), and parallelism is clamped to the policy cap (`-j1` preferred, `-j2` max). |
| 5. Assemble review evidence | `python scripts/dev/review_pack.py --base origin/master --head HEAD --out PR_PACK.md` | Diff stat, changed public headers, changed tests, `git diff --check` verdict, conflict scan and a PR-body draft with every required section. |

`python scripts/dev/stale_branches.py` is the standing census of remote
branches: which are merged-equivalent, superseded, or hold files master never
received. It only reports; it never deletes anything.

## Rules the tools enforce (so you don't have to remember them)

1. **Fail closed beats fail open.** A refused creation changes nothing: no
   branch, no worktree, no file. Refusals print the reason on stderr and exit 2.
2. **Exit codes are contracts.** `0` success (possibly degraded), `2` refused
   (fail-closed precondition), `75` busy (a live build holds the lock), `1`
   genuine failure. Degraded-but-successful runs say so in their payload
   (`"status": "not-executed"`), never silently omit data.
3. **No destructive action, ever.** No branch deletion, no PR close/merge, no
   force-push, no writing inside a master checkout. A stale lock is only broken
   when the recorded holder pid is dead *and* the lock is older than
   `--stale-after`.
4. **Evidence policy.** Every capability claim in a PR body maps to a local
   command plus its exit code, or is explicitly marked not-executed
   (`docs/agents/goal-template.md`, Evidence policy).
5. **Resources.** `-j1` preferred, `-j2` maximum, `-j3+` forbidden; build
   through the guard so the cap is enforced mechanically, not by memory. The
   guard also exports `CMAKE_BUILD_PARALLEL_LEVEL` /
   `CTEST_PARALLEL_LEVEL` for commands whose flags it does not recognise.
6. **Concurrent fetches make refs flicker.** Read-only git queries in these
   tools retry briefly; if you read refs yourself, do the same or you will see
   phantom "missing" branches for a second.

## Running the tests

```bash
python -m unittest discover -s scripts/dev/tests -t scripts/dev
python scripts/dev/tests/mutation_checks.py      # proves the tests can fail
```

The suite is hermetic (temp git repositories, no network, no build) and runs
in minutes with `-j1` semantics for subprocesses. The mutation checks copy the
tools, disable one guard each, and require the corresponding test class to
fail — a green suite that cannot fail proves nothing.

## When you find a defect in your own track

Build a minimal reproduction (a temp repo fixture is usually enough), fix it,
and keep the reproducing test in the suite. If the defect belongs to another
track's owner scope, record the evidence and hand it over in the PR body —
do not fix across the boundary.

## Related documents

- Track prompt spec: [`docs/agents/goal-template.md`](../agents/goal-template.md),
  [`docs/agents/loop-template.md`](../agents/loop-template.md),
  [`docs/agents/command-vocabulary.md`](../agents/command-vocabulary.md)
- Behavioral rules: [`.agents/AGENTS.md`](../../.agents/AGENTS.md),
  [`CLAUDE.md`](../../CLAUDE.md)
- Review discipline: [`.agents/skills/code-review/SKILL.md`](../../.agents/skills/code-review/SKILL.md)
- Tool usage details: [`scripts/dev/README.md`](../../scripts/dev/README.md)
