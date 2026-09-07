# ADR 0134: Dataset Identity, Versioning & Store

- Status: Accepted (2026-09-07)
- Scope: `src/dataset` (new `sicnu_dataset` library), `docs/datasets`, `tests/dataset`
- Depends on: ADR 0009 (assets ≠ display), ADR 0129 (governance store), ADR 0130 (data-plane reliability playbook)
- Ownership: Track D

## Context

The platform has strong identity for assets (`AssetId`+`AssetRevision`),
artifacts (logical key + version + digest) and workflow runs (`runId` +
per-step fingerprints), but the scientific object a result is actually
produced FROM — a versioned, fingerprinted, quality-gated dataset with typed
samples — does not exist. Governance `DatasetRecord` is a member-list
bookkeeping row; training samples are ephemeral `cv::Mat`s; a "dataset" in
common usage today is a folder path. Experiments therefore cannot name what
they consumed, and results cannot be traced to a frozen dataset state.

## Decisions

1. **New module, no governance fork.** `src/dataset` is a new static library
   `sicnu_dataset` (Qt6::Core + GDAL-free headers; SQLite PRIVATE), PUBLIC
   linking `Sicnu::data` for `Result`/`Diagnostic` and canonical-JSON reuse.
   Governance `DatasetRecord` stays untouched; a later integration seam may
   link governance datasets to scientific versions, but none of this track's
   semantics live in `src/data/governance`.
   - Rejected: extending `GovernanceStore` with scientific tables. The
     governance store is the project workspace index (ownership Track 3.0
     line); overloading it would couple two schemas' migrations and page
     budgets.
   - Rejected: a second `Result`/JSON vocabulary. All dataset APIs reuse
     `sicnu::data::Result<T>`/`Diagnostic` and
     `canonicalizeJsonRfc8785` so byte-stability guarantees remain
     single-sourced.

2. **Identity vocabulary.** `DatasetId` (logical, UUID), `DatasetVersionId`
   (immutable state, UUID), `SampleId`, `AnnotationId`, `SplitManifestId`,
   `LabelSchemaId` — strong types following the `SICNU_WORKSPACE_ID_TYPE`
   macro pattern. Physical storage paths are locators, never identity.
   Fingerprint (`DatasetFingerprint`, full SHA-256 over canonical manifest
   JSON) is content identity, separate from logical identity.

3. **Immutable version lifecycle.** `Draft → Committed (+Deprecated)`. A
   committed `DatasetVersion` is immutable; any edit happens on a draft child
   version with `parentVersionId`. Commit runs prepare→validate→stage→commit→
   publish in one SQLite transaction; interrupted staging is detected and
   cleaned or resumed, never surfaced as a version. Deprecation marks but
   never deletes — referenced versions stay readable.
   - Rejected: mutable versions with revision counters (asset-style). A
     dataset referenced by a run must be byte-stable, not merely detectably
     changed; comparisons need identity equality, not diff detection after
     the fact.

4. **Manifest is the contract.** `DatasetManifest` (schema-versioned JSON,
   strict-version + unknown-field-tolerant reader, like workflow run payloads)
   carries: id/name/description, version graph, source asset refs (AssetId +
   revision), entries, dataset schema (modality, band roles, resolution,
   CRS), label schema reference, spatial/temporal extent, split manifests,
   quality summary, statistics summary, tags/license/citation, fingerprint.
   Canonical serialization = `canonicalizeJsonRfc8785(manifestJson)`.

5. **Store.** `DatasetStore` (SQLite WAL, `schema_version`, newer→read-only,
   checked writes with rollback-on-failure, `checkpointForBackup`, paged
   queries with `kMaxPageSize=500`, batch upserts in one transaction) — the
   ADR 0130 playbook applied to the new tables (datasets, versions, entries,
   samples, annotations, label schemas/classes, splits, quality reports,
   lineage edges). One store file per project science root; path supplied by
   the host, the module never guesses locations.

6. **Ownership boundary.** Datasets reference `DataAsset`s; they never open,
   copy or mutate asset payloads. Pixel access for patch generation is
   delegated upward (caller-provided block readers), keeping `sicnu_dataset`
   GDAL-free at the header level and testable without raster fixtures.

## Compatibility

- Additive only: no existing header/API changes. `sicnu_data` is untouched.
- Store schema carries `schema_version` from day one; forward-compat guard
  identical to governance/artifact stores.
- `RsClassDef` int ids interoperate through explicit `LabelMapping`; no
  implicit renumbering anywhere.

## Resources

- Store queries are paged and indexed; sample iteration is cursor-based
  (bounded memory at 100k+ rows, enforced by scale tests).
- Fingerprinting hashes canonical JSON of metadata only — it never walks
  pixel payloads.
