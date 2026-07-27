# 0004 Upstream Output Placeholder Substitution Architecture

We decided to use `${task.<parent_id>.output}` placeholder substitution for dynamic data passing between parent and child tasks in DAG Task Pipelines.

### Context & Decision
To enable seamless multi-stage algorithm chaining without manual intermediate path configuration:
1. **Placeholder Syntax**: Child task parameter maps may contain string placeholders such as `${task.12.output}` or `${task.parent.output}`.
2. **Dynamic Parameter Substitution**: When a parent task completes successfully, `TaskCenter` inspects downstream child tasks and resolves all matching placeholders to the parent's actual `outputLayerPath` prior to dispatching execution.
