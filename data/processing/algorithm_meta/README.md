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

## Capability knowledge layer (D8, ADR 0146) — `capability/`

The subdirectory `capability/` holds the full per-operator capability catalog
(one sidecar per `rs:` operator, 111/111) plus the relation graph
(`capability_relations.json`). It is deliberately a subdirectory:
`tests/test_algorithm_meta_drift.cpp` pins THIS directory to the exact set of
sparse v1 sidecars above.

- Schema, query API, budgets: `src/agent/harness/capability_catalog.h` and
  `docs/adr/0146-capability-relation-graph.md`.
- Derived fields (io, parameters, determinism grade, memory policy,
  prerequisites/limitations declared by operators) are **generated** from the
  live descriptors — regenerate with the `capability_knowledge_tool` target:

  ```bash
  cmake --build build --target capability_knowledge_tool
  ./build/tests/capability_knowledge_tool gen-meta <repo-root>
  ./build/tests/capability_knowledge_tool gen-pages <repo-root>   # pi/knowledge pages
  ```

- Authored enrichment (`summary`, `failure_modes`, `applicability`,
  `teaching_use`, extra prerequisites) lives in the sidecars and survives
  regeneration; apply it with `scripts/capability_enrichment.py` (or edit the
  authored keys directly — never the derived ones).
- `tests/test_capability_knowledge.cpp` guards coverage (111/111), drift,
  graph invariants, budgets, page regeneration, and chain composition.
