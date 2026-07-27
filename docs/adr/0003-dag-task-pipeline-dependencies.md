# 0003 DAG Task Pipeline Dependencies Architecture

We decided to add `QList<long> parentTaskIds` to `AlgorithmTaskInfo` for explicit DAG pipeline dependency gating.

### Context & Decision
To support multi-stage remote sensing execution workflows:
1. **Dependency Gating**: Tasks with `parentTaskIds` remain in `Queued` state until every parent task transitions to `Completed`.
2. **Cascade Unblocking**: Upon parent task completion, `TaskCenter` evaluates downstream child tasks and triggers execution dispatching once all parent constraints are satisfied.
3. **Upstream Failure Cascade**: If any parent task fails or is canceled, all downstream child tasks are automatically canceled with log `Canceled due to upstream parent task failure`.
