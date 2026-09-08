# Dataset manifest

`DatasetManifest` (src/dataset/dataset_manifest.h) is the version
contract. `schema_version` is strict (foreign versions rejected with
`dataset.manifest_version`); unknown fields are tolerated.

Canonical bytes = `sicnu::data::canonicalizeJsonRfc8785(manifest.toJson())`;
`DatasetFingerprint` = SHA-256 of those bytes minus the `fingerprint` field.
`manifestFingerprintMatches()` self-verifies on load.

Sections: identity (dataset/version/parent), header (name/description/
created), `source_assets` (AssetId+revision pins), `entries` (asset|sample
members), `schema` (modality/sensor/CRS/band roles/resolution), optional
`label_schema` ref, spatial + temporal extent, `split_manifests`,
tags/license/citation, provenance/statistics/quality summaries, fingerprint.

100k-sample datasets keep samples OUT of the manifest: the manifest carries
counts/summaries; sample rows live in the store (see performance.md).
