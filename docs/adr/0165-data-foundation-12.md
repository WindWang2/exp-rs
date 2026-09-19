# ADR 0165: Data Foundation 12.0 — Version Tags, Scale Cursors, Retention and Bundle Import

Date: 2026-09-20
Status: Accepted
Track: flash-data-experiment-12

## Context

The data plane (ADR 0130 playbook), dataset versioning (ADR 0134), run identity
(ADR 0137) and reproduction bundles (ADR 0138) exist and are correct after the
post-deep-review fix waves (#1045/#1046/#1056, #1105). Ten-thousand-record
projects and long-running campaigns, however, need four capabilities the
platform did not have:

1. **Stable names over versions.** Datasets have an immutable version DAG but
   no human-stable pointer ("baseline", "release-2") that survives later
   version churn; consumers cite raw version ids.
2. **Deep-page scale.** All list APIs are `LIMIT/OFFSET` paged: at 100k rows a
   deep page costs O(offset), and every run upsert opens its own transaction.
3. **Retention.** Experiment runs are immortal: duplicate ingest attempts and
   old exploratory runs accumulate forever, and nothing answers "which runs
   may be deleted, and what exactly would that delete?" before it happens.
4. **Offline import.** Reproduction bundles could be exported and validated,
   but not installed into a store on another machine.

Additionally, `GovernanceStore::removeAsset` silently cascaded relationship
rows (dataset memberships, result inputs, run outputs), so an asset still used
by a dataset or run could disappear from under them.

## Decision

1. **Version tags are store-level pointers, not manifest fields** — a
   `version_tags` table in the DatasetStore pins a unique-per-dataset name to
   one COMMITTED version. Tags never enter the fingerprinted manifest;
   re-pointing a tag is refuse-then-move (`dataset.tag_conflict`), so a tag
   always resolves to the same content it was pinned to.

2. **Keyset cursors alongside offset paging** — new `*ByCursor` APIs
   (DatasetStore samples, ExperimentStore runs) order by an explicit key tuple
   and carry an opaque, versioned cursor (shared codec in `src/data/
   query_cursor.{h,cpp}`). The cursor embeds its filter, so replaying it
   under a different filter fails typed (`dataset.cursor_mismatch` /
   `experiment.cursor_mismatch`) instead of silently resuming elsewhere.
   Existing offset APIs are unchanged. ExperimentStore gains `upsertRunsBatch`
   and `saveMetricRecordsBatch` (one transaction per sweep), sharing the exact
   validation of `upsertRun` via one extracted write path.

3. **Retention is plan/execute, never a bare delete** —
   `ExperimentStore::planRunPrune(policy)` returns the exact removable run set
   for the CURRENT store state; `executeRunPrune(plan)` re-derives eligibility
   under the write lock inside one transaction, so a stale plan shrinks and
   can never over-delete. Promotion evidence and lineage endpoints protect
   runs by default; pruning rewrites the affected experiments' run-id lists in
   the same transaction (no phantom references). Governance cleanup uses one
   shared reference predicate (`collectAssetReferences`) for plan, execution
   re-validation and the `removeAsset` guard.

4. **removeAsset refuses by default** — an asset still referenced by a dataset
   membership, result input, run output or downstream lineage edge fails with
   `store.asset_referenced`. The explicit `Cascade` policy preserves the
   single-transaction relationship cleanup for the one caller that mirrors an
   already-decided authority (WorkspaceService propagating a DataManager
   removal) and for ghost reconciliation.

5. **Bundle import is evidence installation** —
   `ReproductionBundleImporter::importRun` verifies checksums first (same
   integrity-first rule as validation), refuses foreign schema versions,
   refuses bundles whose identity pins do not reproduce the recorded execution
   fingerprint, and installs the run as status **Created** — a store that
   never observed the execution never fabricates a terminal lifecycle.
   Re-import of the same bundle (same execution fingerprint) is an idempotent
   no-op; `keepOriginalRunId` onto an occupied id with a different identity
   refuses instead of overwriting.

6. **QA reports are append-only audit evidence** — `qa_reports` table in the
   DatasetStore; `DatasetQaReport` gains a strict `fromJson`; corrupt evidence
   rows fail the typed history read instead of returning a thinned history.

7. **Uniform writer-contention budget** — WorkspaceCatalog and ArtifactStore
   now set `busy_timeout=5000` like the other three stores (issue #752
   precedent).

## Consequences

- Deep pages become O(log n) wherever an index covers the filter + order
  (samples: `(dataset_version_id, roword)`; runs: `(experiment_id,
  created_ms)`, the new `(created_ms, run_id)` and
  `(dataset_version_id, created_ms, run_id)`); the 100k-sample / 10k-run
  scale Oracle (`test_data_foundation_scale`) emits structured JSON perf
  evidence.
- The repeat-execution classifier (`repeat_execution.{h,cpp}`) answers
  same/duplicate/rerun/deviated from the indexed execution fingerprint —
  environment drift is reported as evidence and never changes the verdict
  (ADR 0137 semantics unchanged).
- Tags, cursors, QA reports and prune plans are additive schema (`CREATE TABLE
  IF NOT EXISTS`); the store schema versions stay at 1 and older stores gain
  the new tables transparently on open.
- Removal of a referenced governance asset is now a two-step protocol
  (resolve references, or an explicit, auditable Cascade choice).

## Non-goals

- No server-side database, no cloud dependency; everything stays local-first.
- No raster bytes in SQLite (payloads stay in the artifact layer).
- No light-projection for run list pages (page bound keeps full deserialization
  bounded; revisit if run payloads grow).
