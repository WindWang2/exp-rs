# Plugin Platform 8.0 — Goal

Evolve the existing plugin isolation runtime (5.0 base, PR #830) into a mature cross-platform
extension ecosystem: versioned host protocol 2.0 (additive, minor-bumped), real per-request
concurrency behind declared quotas, hardened cross-platform isolation, deepened capability model,
declarative out-of-process UI contributions (host-rendered schema controls), resilient contribution
lifecycle, strengthened packaging/versioning, expanded conformance kit, and docs/DX — with zero
regression to in-process safety.

Constraints honored: no second registry/scheduler/runtime; ≤2 subagents (reserved for the final
adversarial review); master read-only; worktree `feat/plugin-platform-8`; local evidence only.

Milestones and status live in MILESTONES.md; per-package decisions in ARCHITECTURE.md.
