# Lineage graph

`DerivationRecord` and `ArtifactStore` stay the provenance AUTHORITIES.
`LineageGraph` joins: dataset-side edges (dataset store) + experiment-side
edges (experiment store) + an optional existence resolver into one
traversable graph of `(kind, id)` nodes
(asset|dataset|dataset_version|sample|annotation|split|experiment|run|
artifact|metric).

Queries: `ancestors`, `descendants` (cycle-safe via visited set; bounded
by maxDepth + maxNodes; `budgetExhausted` says when truncated), `edgesOf`.
Nodes the resolver cannot confirm become typed tombstones (`dangling:
true`) - never silently dropped. Without a resolver the report claims
nothing about existence.

Edge vocabularies: derived_from, snapshot_of, evaluated_on, produced,
consumed. The MCP layer exposes read-only queries over this graph
(`dataset:` / `experiment:` prefixes follow the existing `lineage:`
pattern).
