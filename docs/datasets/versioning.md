# Versioning & store safety

Lifecycle: `createDraftVersion` -> mutate (samples/annotations/labels,
draft-only) -> `stageVersion` (validate + canonical manifest + staged flag)
-> `commitVersion` (ONE transaction: status->committed + fingerprint) ->
optionally `deprecateVersion` (mark-only; referenced versions stay).

Crash contract: staging is recoverable work - a crash can only leave a
Draft; `staleStagedDrafts()` reports staged-but-uncommitted versions for
the caller to resume or clean. Nothing half-committed is ever surfaced.

Store (dataset_store): SQLite WAL, `schema_version` + read-only forward
tolerance (`dataset.store_read_only`), checked transactions with
rollback-on-failure, `checkpointForBackup()` (TRUNCATE checkpoint, busy
checked), paged queries (`kMaxPageSize = 500`), mutex-guarded single
connection. Duplicate ids are `dataset.conflict`; deleting datasets with
non-draft versions is refused (`dataset.delete_refused`).
