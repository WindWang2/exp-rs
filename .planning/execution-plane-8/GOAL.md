# GOAL — Execution Plane, Worker Runtime, Admission, Cache & Recovery 8.0

Make the authoritative TaskCenter → JobEngine → Workflow execution plane scale
predictably to long-running and high-cardinality remote-sensing workloads,
eliminate algorithmic scheduling cliffs, harden worker lifecycle/recovery,
unify output commitment, and make resume/cache identity scientifically
trustworthy.

Non-negotiable: the single scheduling chain
`WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor/RSOperator`
remains the ONLY scheduling path. No second scheduler, no second agent loop,
no competing model runtime, no ad-hoc publication paths.

## Execution mode

- Fully autonomous; branch `feat/execution-plane-8` in worktree
  `/home/kevin/projects/rs-studio/exp-rs-execution-plane-8` from
  `origin/master` = `322dfd3876` (latest verified 2026-09-10).
- Local verification only; no online CI waiting.
- Subagent budget: ≤ 2 total (read-only reviewers).

## Work packages (track contract)

| WP | Scope | 7.0 known-limitation source |
|----|-------|-----------------------------|
| A | Admission complexity redesign (O(n²) cliffs) | Limitation 6: "TaskCenter 提交扫描 O(n²)：10k 任务提交约 80s (Debug)" |
| B | Resource-aware admission 3.0 | 7.0 package B deepening |
| C | Worker runtime lifecycle | Limitations 2 (no Windows Job Object) + 3 (owner-thread marshal) |
| D | Retry semantics formalization | 7.0 package D formalization |
| E | Resume 3.0 fail-closed identity | Limitation 5 (moved-output / changed-operator resume) |
| F | Execution cache identity (remote + model/operator) | Limitation 1 (identity resolver SEAM ONLY) |
| G | OutputCommitter convergence | Limitation 4 (image_fusion GUI/CLI bypass — re-verify against current master) |
| H | Locking/concurrency audit | 7.0 package H deepening |
| I | Observability trace events | Verification 7.0 trace extension |
