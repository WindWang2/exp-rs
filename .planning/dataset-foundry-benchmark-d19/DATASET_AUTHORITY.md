# DATASET_AUTHORITY

**Canonical identity:** `DatasetVersionId` of a **Committed** (or Deprecated-but-readable) version in `DatasetStore`.

**Content identity:** SHA-256 fingerprint of canonical `DatasetManifest` JSON (fingerprint field excluded).

**Membership:** sample rows + annotation tips keyed by `(versionId, sampleId)`; not inlined in the manifest.

**Split identity:** immutable `SplitManifest` id + content fingerprint persisted once.

**Label identity:** `(labelSchemaId, version)` documents; class identity is stable UUID + code, never row index.

**Feature identity (D19):** `FeatureSet` id + schema_version + digest; joins only on stable sample keys matching the set's `sample_key` field.

Anything that cannot cite the above pins is incomplete reproducibility — reported, never fabricated.
