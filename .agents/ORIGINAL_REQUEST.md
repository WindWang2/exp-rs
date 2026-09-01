# Original User Request

## Initial Request — 2026-09-01T15:58:21Z

Process and resolve all 5 open Pull Requests (#708, #709, #710, #711, #712), merge them sequentially into master using squash and merge, verify full CMake build and Catch2 test suite passing, and completely clean up associated worktrees and branches.

Working directory: /home/kevin/projects/rs-studio/main
Integrity mode: development

## Requirements

### R1. PR Review, Conflict Resolution & Merging
- Process all 5 open Pull Requests in logical/chronological order (#708, #709, #710, #711, #712).
- Inspect branch diffs, resolve any git rebase or merge conflicts accurately without losing functionality.
- Merge each PR into `master` using squash and merge (`gh pr merge --squash --delete-branch` or git squash merge).

### R2. End-to-End Build & Test Verification
- Run CMake build on `master` to ensure zero compilation or linking errors.
- Execute the Catch2 unit test suite / CTest targets and verify 100% pass rate.

### R3. Worktree and Branch Cleanup
- Safely remove all 4 secondary worktrees (`exp-rs-cartography-layout`, `exp-rs-resolve-all-open-issues`, `exp-rs-spatial-platform`, `exp-rs-temporal-analysis`) via `git worktree remove`.
- Prune and remove corresponding local feature branches and remote branches.

## Acceptance Criteria

### Integration & Repository State
- [ ] All 5 open PRs (#708, #709, #710, #711, #712) are merged and closed on GitHub.
- [ ] `gh pr list` reports 0 open PRs.
- [ ] Local `master` branch is up-to-date with all changes integrated.

### Build and Test Verification
- [ ] `cmake --build` succeeds without errors.
- [ ] All Catch2 unit tests pass (100% green, 0 failures).

### Cleanup Verification
- [ ] `git worktree list` shows only `/home/kevin/projects/rs-studio/main`.
- [ ] The 4 worktree directories are completely cleaned up.
- [ ] Corresponding local feature branches and remote feature branches are deleted.
