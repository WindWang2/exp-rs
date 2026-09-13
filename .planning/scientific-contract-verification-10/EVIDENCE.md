# EVIDENCE — scientific-contract-verification-10

Policy: every capability claim maps to a local command + exit code, or is marked not-executed.

## Phase 0

- `git fetch --all --prune && git checkout master && git pull --ff-only` → exit 0; origin/master = `7d78059d1a6d316d606656759a506d17bc5e3b55`.
- `git worktree add ../exp-rs-scientific-contract-verification-10 -b zcode/scientific-contract-verification-10 origin/master` → exit 0.
- Finding re-verification (all 7 LIVE): evidence lines in BASELINE.md §Findings, each backed by file:line reads on the worktree at `7d78059d1a`.
- `.gitignore` whitelist block appended; note: shared `.git/info/exclude` line 7 (`.planning/`) shadows check-ignore for fresh files → planning files added with `git add -f` (precedent: 348 tracked planning files; DECISIONS D-7).
- `git ls-files -f .planning/scientific-contract-verification-10/ | wc -l` → (filled after first commit)

## Build

- (to be filled: configure/build commands, exit codes, RSS/load notes)

## Tests

- (to be filled per phase: exact ctest invocations, assertion counts, exit codes)

