# R35-T06 — BRANCH / PR MATRIX

Refreshed at session start (`git fetch --prune`, `gh api`).

| Branch | Head | PR | Note |
|---|---|---|---|
| `master` | `120677e6` | — | execution base; 0 open PRs |
| 27 merged-retained `zcode/*` | various | merged | squash leaves them "ahead"; not in-flight |
| `zcode/r32-t06-desktop-experience-…` | `9c7159d501ff` | #2138 closed-unmerged | superseded; not resurrected |

No public branch without a PR exists, and no open PR exists — so **the active
exclusive-path set is empty**. That does **not** mean any file may be edited:
the six-track whole-file owners are unchanged (see PATH_OWNERSHIP.md).

## This track

| Item | Value |
|---|---|
| Branch | `zcode/r35-t06-desktop-workflows-20261118T150535Z` |
| Base | `master` @ `120677e6e165d3e88bb9d2ec2a3fd98042ec719d` |
| Worktree | `/home/kevin/projects/rs-studio/exp-rs-r33-t06-desktop-workflows-20261117T141119Z` (reused; DECISIONS D-01) |
| Stacked on another PR | no |
| PR | created at delivery (see LEDGER) |

## Delivery-time refresh

Re-run before pushing: `gh api pulls?state=open`, compare new PRs' file lists
against PATH_OWNERSHIP.md, rebase onto `origin/master` if it moved, and re-run
the affected gates/tests on the rebased HEAD.
