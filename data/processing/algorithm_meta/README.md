# Algorithm Catalog Sidecars (ADR 0122) — GENERATED ARTIFACTS

**These files are generated, not hand-maintained (#707).** Every field they
carry is authored in code on the operator's `metadata()` (task family, GPU,
notes, tags) and flows into the `AlgorithmDescriptor`'s `AgentMetadata` —
the single source of truth. Regenerate the whole directory with:

```bash
sicnu_geo_rs_cli --export-catalog data/processing/algorithm_meta
```

The shipped files must stay byte-identical to that regeneration; the drift
gate in `tests/test_algorithm_meta_drift.cpp` fails when a sidecar
contradicts the descriptor. Do not hand-edit — change the operator metadata
instead and re-export.

Optional per-algorithm capability manifests consumed by
`AlgorithmMetaStore` (`src/processing/framework/algorithm_meta_store.*`) and
merged into the MCP `list_algorithms` / `search_algorithms` /
`get_algorithm_schema` responses as a `catalog` object — the agent-facing
capability discovery layer for task-to-algorithm matching.

One JSON file per algorithm, named after the algorithm id (`:` → `-`):

```json
{
  "id": "rs:infer",            // required — must match the registry id
  "task": "inference",         // task family (segmentation, classification, ...)
  "input": "raster",           // primary input contract
  "output": "raster",          // primary output contract
  "gpu": false,                // GPU required/preferred
  "accuracy": 0.89,            // optional benchmark accuracy in [0, 1]
  "notes": "...",              // selection guidance for agents
  "tags": ["..."]
}
```

Files whose `id` does not match a registered algorithm are ignored by
discovery but still loaded (they document intent for upcoming operators).
