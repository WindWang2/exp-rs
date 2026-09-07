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

CLI: `sicnu_geo_rs_cli dataset create|inspect|validate|diff|stats`.
