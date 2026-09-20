# REVIEW_LOG — ds41-dev-worktree-tooling (Track D5)

Independent review of `origin/master...agent/ds41-dev-worktree-tooling`
(base `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`), read-only, by a reviewer
agent separate from the implementation agent. The reviewer re-verified every
finding against the tip commit it was given and reported its own scope note
(the branch moved under it by three commits during the review, which it
re-verified against).

Severity vocabulary per `docs/agents/command-vocabulary.md` (P0 crash/data
loss/destructive, P1 boundary/race/contract violation, P2 perf/nondeterminism/
missing defence/doc drift, P3 debt).

## Disposition summary

| Severity | Found | Fixed here | Deferred (with reason) |
|---|---|---|---|
| P0 | 1 | 1 | 0 |
| P1 | 3 | 3 | 0 |
| P2 | 6 | 5 | 1 (P2-9 remaining test-coverage items, see below) |
| P3 | 7 | 5 | 2 (P3-15 message wording, P3-16→now fixed) |

**P0/P1 = 0 remaining.**

## Findings and disposition

| # | Sev | Finding (reviewer's words, condensed) | File:line | Disposition |
|---|---|---|---|---|
| 1 | P0 | Flag-shaped `--branch` (`-m`, `-c`, `-t`, `--unset-upstream`, `--edit-description`) reaches `git branch <name> <sha>`, which parses it as an option — `-m` RENAMES the repository's branch (reproduced on a fresh temp repo: `master` became a SHA-named branch), the rollback `git branch -D -m` silently fails, and the tool reports a no-op refusal. | `new_worktree.py:171` | **Fixed** (`a4800a307`): `git check-ref-format --branch` validation before any mutation + `--` separators on all git argv with user input + verified rollback (`_rollback_branch` re-checks existence). Regression tests `FlagInjectionTest` (refusal for 7 option-shaped names, "renames nothing", remote-only-name conflict). |
| 2 | P1 | Lock keyed on a non-canonical path: only relative `--lock-dir` values were resolved, `_norm()` was dead code, so a junction/symlink or relative/absolute mix of one build dir produced TWO locks and both payloads ran concurrently (reproduced with `mklink /J`). | `resource_guard.py:232-235, 52-54` | **Fixed**: `lock_path_for` canonicalises through `os.path.realpath`. Regression test `test_two_spellings_of_one_build_dir_share_one_lock`. |
| 3 | P1 | An empty or unreadable lock file could never be broken (`_read_holder` → `{}`, no mtime fallback) — permanent DoS on that build dir; realistic trigger is SIGKILL in the window between `os.open` and the holder write. | `resource_guard.py:63-92, 108-131` | **Fixed**: lock created atomically WITH its record (staged file + `os.link`, mode `0600`, no empty window), and `_is_stale` falls back to the file's mtime age when the record is unreadable. Tests: empty/corrupt lock breakable once old, fresh empty lock still blocks. |
| 4 | P1 | The read-only retry allowlist contained the bare subcommand `branch`, so the creating `git branch` and the destructive rollback `git branch -D` were retried up to 3× (reproduced with a stubbed `subprocess.run`: create ran twice, `branch -D` ran three times) — contradicting `run_git`'s docstring and DECISIONS D-007. | `dev_common.py:38-42, 85`; `new_worktree.py:171,179,188` | **Fixed**: allowlist matches full argv patterns (`("branch","--list")`, …); `RetryPolicyTest` pins create/rollback at exactly one attempt and a read-only listing at three. |
| 5 | P2 | Inherited `MAKEFLAGS=-j8` defeats the cap for `make` (reproduced; payload saw `-j8`). | `resource_guard.py:204-209` | **Fixed**: `governed_env` strips `-j<digits>` tokens before appending the cap. Test `test_inherited_makeflags_j_is_stripped`. |
| 6 | P2 | An unresolvable `--base` silently yields zero evidence, and `--fail-on-overlap` then exits 0 — a false negative for the primary gate. | `overlap_scan.py:57-61, 166-169, 222-226` | **Fixed**: base resolution is verified once; affected sections report `not-executed` with the reason (and `--from-HEAD` fails loudly when no paths were given). |
| 7 | P2 | TOCTOU in stale-lock breaking: decision read from the file, then unlink+recreate — a live holder's lock created in between could be deleted. Reviewer could not reproduce it (120×3 attempts) but flagged the in-code "safe" comment as wrong. | `resource_guard.py:108-126` | **Fixed**: `_break_if_still_stale` re-reads the bytes immediately before the unlink and bails to the busy path if they changed; comment corrected. |
| 8 | P2 | Non-ENOENT lock-creation failures (EACCES/EPERM/EMFILE) still surface as exit 75 "locked by pid None". | `resource_guard.py:134-146` | **Fixed**: those errnos map to exit 1 with the real reason; the busy message is only produced for genuine contention. |
| 9 | P2 | Test-suite gaps: the remote-only branch-name conflict, the "worktree registered but HEAD unreadable" rollback branch, the suffix-collision path, any run that reaches `git fetch`, the empty/corrupt-lock case, `MAKEFLAGS` inheritance, two-spellings, `--json --out`, and the conditional gh-degradation assertion were all unasserted. | tests | **Partially fixed**: the first, the lock cases, MAKEFLAGS, two-spellings and the retry policy now have tests. Remaining: the HEAD-unreadable rollback branch and a `git fetch`-reaching run stay untested by design (the first needs a filesystem-level fault injection, the second writes remote-tracking refs during tests); recorded as a known limitation. The conditional gh-degradation assertion is intentional (a live `gh` legitimately succeeds) and now documented in ORACLES.md. |
| 10 | P2 | `.planning` docs drift from the code: ORACLES expected a `master_sha` key, claimed the guard "records the break in the lock file", described a non-existent `network`-marked smoke test, said master checkoutouts are refused outright, DECISIONS described the lock inside a `.devlock` directory and `--wait` default 900, and named a guide file that does not exist. | `.planning/*` | **Fixed**: ORACLES.md and DECISIONS.md corrected; scripts/dev/README.md documents the lock file's real path, mode, atomicity and parent-creation behaviour. |
| 11 | P3 | Holder record written twice (widening the empty-lock window); busy message prints `waited Nones`. | `resource_guard.py:69-78, 147-148` | **Fixed**: single atomic write; the `Nones` template is gone with the exit-1 path split. |
| 12 | P3 | A missing payload command raises a raw traceback. | `resource_guard.py:261-262` | **Fixed**: refuses with a message and exit 1; `KeyboardInterrupt` also handled (exit 130). |
| 13 | P3 | `--json` returns before `--out`, so `--json --out PR_PACK.md` writes nothing and exits 0. | `review_pack.py:226-229` | **Fixed**: `--out` and `--json` now both take effect. |
| 14 | P3 | The rollback `git branch -D` result is unchecked, so "(branch rolled back)" can print while the branch survives; `_branch_exists` only checks `origin`. | `new_worktree.py:179,188` | **Fixed**: `_rollback_branch` verifies the branch is gone and warns otherwise; the remote-conflict test exercises the `refs/remotes/origin/<name>` path. (Other remotes than `origin` remain out of scope — documented.) |
| 15 | P3 | `trunk_ref` fabricates `origin/master` when nothing resolves, so preflight exits 1 with a confusing message in a remote-less repo; the stale-break path loops without backoff. | `dev_common.py:141-145` | **Partially fixed**: the stale-break loop now never spins (each iteration either acquires, breaks, or sleeps 0.5 s). The remote-less-repo message is left as-is; preflight's failure text names the failing command, which is actionable. |
| 16 | P3 | Lock created `0o644` under the umask; `0o600` would be more defensive on shared hosts. | `resource_guard.py:108` | **Fixed**: staged lock files are created `0o600`. |
| 17 | P3 | The parent-directory creation is undocumented; README silent. | — | **Fixed**: README and DECISIONS document it. |

## Reviewer's "verified correct" list (kept, verbatim in substance)

40→51-test suite green; mutation gate 6/6; `compileall` and
`git diff --check` clean; gh-removal degradation exits 0 with
`not-executed` records for all four gh-using tools; every `new_worktree`
fail-closed path re-verified (duplicate, existing path, registered path,
dirty master, unresolvable base, missing remote, path inside the repo
including a case-differing Windows spelling, `--path=-f`, file `--path`)
plus the working paths (detached HEAD, unicode/space path, `../` traversal,
clean-master bootstrap, suffix adoption); all `resource_guard` behaviours
(`--parallel 8→2`, `-j9→-j2`, `-j1` preserved, env vars, refusal at 75 with
the payload not run, `--wait` serialisation, exit-3 propagation with
release, dead-holder break, live-holder protection, `--status`); no
destructive capability anywhere by grep and test; pack reproducibility and
its incomplete-pack refusal.

The reviewer also noted it accidentally created and then deleted one
branch (`brand-new-branch`) in its own scratch fixture while testing the
P0, verified gone with `git log -1` = `adf8f9895`.

## Verification after the fixes

```
$ python -m unittest discover -s scripts/dev/tests -t scripts/dev
Ran 51 tests in 199.585s   OK      # run 1
Ran 51 tests in 206.356s   OK      # run 2

$ python scripts/dev/tests/mutation_checks.py
mutation checks: 6/6 proved the tests can fail   # run 1
mutation checks: 6/6 proved the tests can fail   # run 2
```

Live re-verification of the P0 fix on this repository: `--branch=-m` and
`--branch=--unset-upstream` are refused with
`not a valid branch name` and `git rev-parse --abbrev-ref HEAD` is unchanged;
a valid name creates the worktree and is removed afterwards.
