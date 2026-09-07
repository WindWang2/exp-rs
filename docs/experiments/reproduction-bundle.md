# Reproduction bundle

`reproduce export --experiment-db <db> --dataset-db <db> --run <id> --out <dir>`
writes: manifest.json, dataset_refs.json, split.json, run_config.json,
environment.json, software.json, model_refs.json, metrics.json,
provenance.json (lineage slice), README.md, checksums.txt (SHA-256 of
every file). Payload bytes are NOT copied (stable refs + digests); a
portable mode exists with a byte cap.

`reproduce validate --bundle <dir> ...` answers Exact | Compatible |
BestEffort | Impossible with per-check reasons (see reproducibility.md).
Validation reads the STORE side live: a dataset version that has vanished
since export makes the replay Impossible - the bundle never vouches for
what it cannot see.
