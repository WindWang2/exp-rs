# Dataset concepts

```
DatasetId            logical, evolving object (e.g. "landcover-AOI-7")
  - DatasetVersion   immutable content state (draft -> committed -> deprecated)
       + manifest    canonical description (fingerprinted)
       + samples     typed sample rows (paged, store-owned)
       + annotations immutable revision chains pointing at samples
       + splits      persisted SplitManifests (+ leakage summaries)
       + quality     findings + level gate (draft/valid/certified)
```

- **Identity vs content.** Two versions with identical content share a
  fingerprint but never an id; provenance tracks which one a run consumed.
- **Immutability.** Committed versions refuse sample/annotation writes
  (`dataset.not_draft`). Mutations happen on a draft child.
- **Loose references.** Datasets reference `DataAsset`s by AssetId string;
  the data layer stays authoritative. Lineage joins both stores at query
  time (see docs/experiments/provenance.md).
- **Store.** One SQLite file (WAL, schema-versioned, read-only forward
  tolerance) owned by the host; the module never guesses paths.
