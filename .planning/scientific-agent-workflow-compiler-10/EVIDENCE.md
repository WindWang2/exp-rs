# EVIDENCE — scientific-agent-workflow-compiler-10
Every capability claim maps to a local command + exit code, or is marked not-executed.

## Phase 0
- `git fetch --all --prune && git pull --ff-only` → "已经是最新的" (exit 0)
- `git rev-parse origin/master` → 7d78059d1a6d316d606656759a506d17bc5e3b55
- `git worktree add ../exp-rs-scientific-agent-workflow-compiler-10 -b zcode/scientific-agent-workflow-compiler-10 origin/master` → exit 0
- `git check-ignore .planning/scientific-agent-workflow-compiler-10/GOAL.md` → no output, exit 1 (tracked); `-v` shows negation pattern `.gitignore:149`
- Sibling overlap check: `git ls-tree -r origin/zcode/temporal-eo-phenology-change-10 | grep docs/adr` → 0148 claimed; #975/#974 no harness paths touched (verified via `git diff --name-only master...origin/<b> | grep -c harness/` → 0 for both)
- gh pr list (30) → dedupe table in BASELINE.md

## OUT_OF_SCOPE
- F-OPS-3 (rs:qa_mask fail-open), F-OPS-4 (io:reproject srcCrsOverride dead param): src/operators — algorithms track. Recorded as constraints: repair rules avoid depending on either path (pinned by tests in WP3).
- #869–#871 help-store drift: unified-help-diagnostics-6 lane.
