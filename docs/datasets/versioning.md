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

## Version tags (12.0)

A tag is a stable, human-chosen name pinned to one COMMITTED version of a
dataset — `addVersionTag(datasetId, "baseline", versionId)` — resolving via
`versionByTag` and listing via `versionTags` (tag-ascending). Semantics:

- Tags live in the STORE (`version_tags` table), never in the manifest:
  tagging changes no content, so version fingerprints are untouched.
- A tag is immutable once written; re-adding the name fails with
  `dataset.tag_conflict` even when it would point at the same version.
  Moving a tag is an explicit `removeVersionTag` + `addVersionTag`.
- Drafts cannot be tagged (`dataset.not_committed`); a tag whose version row
  disappeared resolves to nothing rather than fabricating an id.
- Names are 1..128 chars, no leading/trailing whitespace, no control
  characters (`dataset.tag_invalid`).

## Keyset cursor paging (12.0)

`samplesPageCursor(versionId, cursor, limit)` pages samples in insertion
order with O(log n) deep pages (the `(dataset_version_id, roword)` index).
`cursor` is the opaque `nextCursor` of the previous page (empty = first);
`total` is the full row count independent of the cursor. The cursor embeds
the version id — replaying it against another version fails
`dataset.cursor_mismatch`; a truncated or foreign cursor fails
`data.cursor_invalid`. Page size stays bounded by `kMaxPageSize`.

## Persisted QA reports (12.0)

`saveQaReport` appends a `DatasetQaReport` as audit evidence for one
version (`qa_reports` table; append-only — rows are never updated). Reads:
`qaReportsForVersion` (newest-first, typed, bounded) and `latestQaReport`.
A corrupt evidence row fails the typed read (`dataset.corrupt_qa_report`)
instead of returning a thinned history. `DatasetQaReport::fromJson` parses
strictly (`dataset.qa_schema` on foreign schema versions).
