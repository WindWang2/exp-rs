# 0006 Unify JobEngine behind TaskCenter Seam Architecture

We decided to encapsulate `JobEngine` as an internal execution adapter behind the `TaskCenter` seam.

### Context & Decision
Previously, UI dialogs such as `SicnuAlgorithmDialog` were burdened with parallel background runners, submitting `JobRequest` instances to `JobEngine` while simultaneously enqueuing tasks into `TaskCenter`. This caused duplicate status tracking, dual progress listeners, and split cancellation logic.

1. **Single Execution Interface**: UI dialogs and callers interact exclusively with `TaskCenter` (`TaskCenter::instance().enqueueTask(...)`).
2. **Internal Adapter Seam**: `TaskCenter` encapsulates priority scheduling, DAG dependency gating, and dispatches background worker execution through internal `JobEngine` and `QgsTask` adapters.
3. **Unified Locality & Leverage**: All task lifecycle states, progress updates, logs, auto-load signals, and cancellation operations pass through `TaskCenter`.
