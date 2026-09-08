# Experiment & run model

`Experiment` = one research question (id, name, objective, runs).
`ExperimentRun` = one concrete execution with the pins listed in the
README. Statuses are truthful: Created -> Running -> (Completed | Failed |
Cancelled), with Cancelling and Interrupted on the way; transitions are
validated (`experiment.bad_transition`) and terminal states never reopen.
A run that died with the process stays Interrupted.

Identity pins are immutable after the run leaves Created
(`experiment.identity_immutable`). The experiment row is immutable once
created (differing re-upsert = conflict); runs are upserted along legal
transitions only.

Store: experiment_store (same SQLite playbook); runs indexed by
experiment/dataset/status with paged queries; one MetricRecord per run.
