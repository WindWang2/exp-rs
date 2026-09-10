# Dataset Foundation 5.0

`src/dataset` (library `Sicnu::dataset`) provides the scientific dataset
layer of the platform: identity, versioning, samples, annotations, label
ontology, deterministic splits, leakage auditing, quality gates and a
bounded-memory catalog (ADR 0134-0136).

Design rules (short form, see docs/adr/ for the long form):

- identity is never a path: `DatasetId`/`DatasetVersionId`/`SampleId` are
  UUIDs; content identity is the SHA-256 `DatasetFingerprint` over the
  canonical manifest;
- a committed `DatasetVersion` is immutable; edits fork draft child versions;
- samples are one envelope + typed payloads (point/pixel/window/patch/
  polygon/object/pair/temporal/multimodal);
- class identity is the stable `LabelClass` code+schema version, never a row
  index;
- splits are deterministic (fixed PRNG) and persisted as manifests;
- leakage claims require a recorded audit; the platform never implies one
  that did not run.

Docs: [concepts](concepts.md) | [manifest](manifest.md) |
[sample model](sample-model.md) | [label schema](label-schema.md) |
[splits](splits.md) | [leakage policy](data-leakage-policy.md) |
[versioning](versioning.md) | [quality](quality.md) |
[performance](performance.md)

CLI: `sicnu_geo_rs_cli dataset create|list|inspect|version|validate|diff|stats|label-schema|split|leakage`.

## Platform 7.0 additions

- **Split manifest + leakage report persistence**: `saveSplitManifest` is
  immutable per id (re-saving different content is a `dataset.conflict`);
  leakage reports are an append-only history keyed by content digest.
  Runs can finally cite resolvable split pins.
- **Sample promotion** (`sample_promotion.h`): classification regions,
  segmentation objects, annotation chains, pre/post pairs (event group
  required) and temporal assemblies are promoted into governed samples under
  DRAFT versions. Pipeline raw values (e.g. a cv::Mat label integer) appear
  only as promotion-time rules mapped to label-schema class codes — a raw
  value never becomes identity, and unmapped values refuse the promotion
  listing the gaps.
- **Fold-level audit** (`fold_audit.h`): per-fold materialization +
  `LeakageAuditor` runs, per-fold class balance with explicit zero-ratio
  flags, and a deterministic replay check (regenerate + fingerprint
  equality).
- **Facets & quality at scale** (`dataset_store_facets.cpp`): a facet side
  table (draft-only, atomic per-sample replace) answers sensor/region/class/
  digest questions with SQL-side GROUP BY whose results are bounded; the
  bounded tail is reported as an explicit "(other)" bucket. The quality
  summary cache is stamped with (sample count, max roword) so staleness is
  detectable, never hidden.

MCP surface: `dataset:list`, `dataset:inspect`, `dataset:version`,
`dataset:diff`, `dataset:stats`, `dataset:validate`, `dataset:label_schema`,
`dataset:split_inspect`, `dataset:leakage_audit` (see
`src/agent/data_platform_tools.h`).
