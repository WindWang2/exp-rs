# ADR 0138: Lineage Graph, Environment Capture & Reproduction Bundles

- Status: Accepted (2026-09-07)
- Scope: `src/experiment` (lineage, environment, reproduction bundle/validator)
- Depends on: ADR 0134–0137; existing `DerivationRecord`/ArtifactStore lineage; ADR 0112 (MCP lineage)
- Ownership: Track D

## Context

Provenance exists per derived asset (`DerivationRecord`) and per workflow
run, queryable through MCP `lineage:` tools, but the graph stops at
asset/run granularity. Dataset versions, samples, splits, experiments and
evaluation records are not graph citizens, so "which raw assets fed this
metric", "is this historical experiment replayable" and "is this bundle
safe to hand to a third party" have no data path. Environment capture does
not exist (the workspace bundle records a software name); an unfiltered
`QProcessEnvironment` dump would leak credentials.

## Decisions

1. **Lineage is a query layer over loose refs.** `LineageGraph` nodes are
   typed `(kind, id)` pairs — `asset`, `dataset`, `dataset_version`,
   `sample`, `annotation`, `split`, `experiment`, `run`, `artifact`,
   `metric` — with edges (`derived_from`, `evaluated_on`, `produced`,
   `consumed`) assembled from: dataset store lineage edges, experiment store
   run bindings, and (via injected adapters) DataManager derivation records
   and artifact refs. Queries: ancestors, descendants, inputs, outputs,
   producer, consumer — cycle-safe (visited set), dangling-ref aware
   (unresolved refs become typed tombstone nodes, never dropped silently),
   depth- and count-bounded.
   - Rejected: a new authoritative provenance store. DerivationRecord and
     ArtifactStore remain the authorities; this layer joins and traverses.

2. **Environment capture is allowlisted.** `RunEnvironment` records: git
   commit + dirty flag (when available), build type, platform/arch,
   compiler id+version, Qt/GDAL/PROJ/GEOS/OpenCV/ONNX Runtime versions
   (already probed by the build/runtime where available), model runtime
   device, plugin manifest versions, locale/timezone. Everything else is
   excluded BY DEFAULT — there is no "dump all environment variables" path,
   and a secret filter (denylist patterns: key/token/password/secret/
   credential/cookie/auth, case-insensitive) runs over the collected map as
   defense-in-depth with unit tests asserting representative secrets never
   survive. Non-determinism is honest: runs record determinism grades
   (ADR 0124) instead of pretending.

3. **Experiment-scoped reproduction bundle.** `reproduce export` writes:
   `manifest.json` (bundle schema version, run identity, verdict),
   `dataset_refs.json` (version id + fingerprint + asset refs/digests),
   `split.json`, `run_config.json` (canonical), `environment.json`,
   `software.json`, `model_refs.json` (id@version + digest),
   `workflow.json` (when a workflow run produced it), `metrics.json`,
   `provenance.json` (lineage slice), `README.md`, `checksums.txt`
   (SHA-256 of every bundle file). Payload data is NOT copied by default —
   stable refs + digests; an explicit portable mode copies capped bytes
   (mirroring the workspace bundle's policy).
   `ReproductionValidator` checks dataset availability/fingerprint match,
   model digest match, operator availability, version compatibility,
   workflow validity, required artifacts, environment compatibility and
   answers with a typed verdict: `Exact | Compatible | BestEffort |
   Impossible` + reason list.
   - Rejected: extending `ReproBundleExporter` in place. The workspace
     bundle (governance-owned) and the experiment bundle answer different
     questions; they share conventions (checksums, modes), not code paths.

4. **GC safety.** Dataset versions, split manifests and runs pin their
   referenced artifacts through the existing ArtifactStore ref mechanism
   (`refKind = "dataset_version" | "split" | "experiment_run"`); GC
   therefore already respects them without modification. Dataset-store
   deletes are tombstones (deprecated/deleted flags), never row drops while
   refs exist.

## Compatibility

- Additive modules and store files; MCP gains read-only `dataset:` /
  `experiment:` / `reproducibility:` query tools; CLI gains `dataset`,
  `experiment`, `reproduce` command groups on the existing dispatcher.
- No changes to DerivationRecord, ArtifactStore, GovernanceStore schemas.

## Resources

- Lineage traversal is bounded (max depth/nodes) with cursor paging at the
  store level; environment capture runs once per run (no polling).
- Bundle export is streaming (file-by-file checksum write), no in-memory
  concatenation of payloads.
