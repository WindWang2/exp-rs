# Plugin Platform 8.0 — Milestones

| # | Milestone | Scope | Status |
|---|---|---|---|
| M0 | Baseline repairs | jsoncpp Int64 ambiguity, .exe worker path, platform-native capability anchors, capabilities.md drift | DONE (commit 1) |
| M1 | Protocol 1.1 concurrency (WP A+B) | worker pool, per-request cancel, FIFO quota gate (exact), poison escalation, frame-cap negotiation, progress coalescing, event queue bound, proxy de-serialization | DONE |
| M2 | Isolation hardening (WP C) | setpgid + group kill (ladder/crash/shutdown), RLIMIT_AS, orphan test | DONE |
| M3 | Capability 2.0 (WP D) | worker-side workDir containment (opt-in), pathIsWithinRoot, honest error-code mapping, manifest round-trip fixes (access/quotas/runtime) | DONE |
| M4 | Declarative UI (WP E) | SDK schema + validator, ui.describe/ui.invoke, worker probe, runtime passthroughs, host renderer, revoke wiring, fixture provider | DONE |
| M5 | Conformance kit (WP F+H) | PT_CANCEL/PT_CONCURRENCY/PT_RESTART/PT_UI_SCHEMA, reload-adapter restoration fix, CLI diagnosability, help-projection gate fix | DONE |
| M6 | Packaging (WP G) | staged install, SHA-256 checksums (known-answer), rollback, SBOM/signature metadata | DONE |
| M7 | Docs, review, PR (WP I) | docs updated, adversarial review, FINAL_REPORT, push + PR | DONE |
