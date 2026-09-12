# Contract Platform 9.0 — Unified Contract Projection & Drift Guards

> docs/verification/CONTRACT_PLATFORM_9.md
> Track: `feat/unified-contract-verification-9` · planning: see
> `.planning/unified-contract-verification-9/`

## What this is

One mechanical answer to a recurring failure class: an implementation
accepts a parameter, emits an error code, registers a command, or declares a
capability — and one of its *projections* (schema, help page, diagnostics
page, tool catalog, empty-state CTA, preflight suggested action) silently
lags behind. Issues #869, #870, #871, #872, #879, #880, #881, #882 are all
instances. They were fixed by hand on master; this platform makes the whole
class fail tests instead.

## Components

| Component | Path | Role |
|---|---|---|
| Source scanning primitives | `src/contracts/text_scan.*` | literal/comment-aware brace/paren matching and regex extraction |
| Operator param scanner | `src/contracts/operator_param_scanner.*` | extracts *declared* (schema()) vs *read* (run + same-file helpers) parameter keys per registered operator |
| Canonical descriptor | `src/contracts/contract_descriptor.*` | minimal typed descriptor projected from schema(); deterministic JSON + round-trip |
| Command reference scanner | `src/contracts/command_ref_scanner.*` | registered ids, surface lookups, CTA commandIds, preflight actions |
| Error code scanner | `src/contracts/error_code_scanner.*` | ErrorCode enum ↔ toString switch; harness code constants |
| Contract graph | `src/contracts/contract_graph.*` | nodes/edges/findings, canonical `exp.contract.graph.v1` |
| Graph assembly | `src/contracts/graph_assembly.*` | builds the live graph from registries, scanners and `data/` JSON |
| Snapshot tool | `src/contracts/tool/contract_inventory_main.cpp` | regenerates / byte-verifies `data/contracts/contract_graph.snap.json` |
| Guards | `tests/test_contract_{platform,projection,command,diagnostics,capability}_9.cpp` | the red/green gates |
| Benchmark | `tests/benchmark_contract9.cpp` | generation cost evidence (`exp.bench.contract9.v1`) |

## Guards and the issue classes they close

| Guard (test) | Closes | Mechanism |
|---|---|---|
| impl-reads ⊆ schema-declares (projection_9) | #872/#879/#880 | scanner set difference; allow-list entries require a reason |
| dead schema params (projection_9) | schema rot | declared-but-never-read detection |
| lookup/CTA/preflight → registry (command_9) | #881/#882 | every referenced action id must be registered |
| help ↔ registry both directions (command_9) | #869 | no phantom `command.*` topics; no unhelped commands |
| HelpContentStore single load (command_9) | #871 | regression: one file → one descriptor, zero errors; duplicate ids → error |
| enum ↔ toString switch (diagnostics_9) | enum drift | set equality on scanned values |
| diagnostics census (diagnostics_9) | #870 | every harness/operator code needs a curated page or reasoned allow-list — `fallback()` cannot silently absorb |
| capability/tool/recipe floors (capability_9) | phantom capability | references resolve; operators reachable from the agent surface |
| snapshot freshness (platform_9) | everything at once | byte-compare `data/contracts/contract_graph.snap.json` against a fresh generation |

## Mutation policy (the red direction is proven, not assumed)

- `projection_9` plants drift in synthetic sources and *deletes a real
  schema declaration from a copy of `io_operators.cpp`* — the scanner must
  report `includeStatistics` as an undeclared read (old behavior red).
- `diagnostics_9` runs the census against a catalog with one page removed —
  the exact code must be reported.

## Honesty rules

- Unknown/unparseable source constructs produce unresolved findings that
  FAIL the suite — the scanner never silently skips. Depth budgets and
  unreadable/oversized files are loud findings too.
- Allow-lists live in the tests as `{id, key, reason}` records and are rot
  guarded: after each census, every entry must have matched a live finding
  in the same run — an exception whose condition disappeared breaks the
  suite until it is removed. Entries naming cross-track drift carry an
  explicit OWNED-BY-#88x marker.
- The snapshot test failure message contains the exact regeneration
  command; a stale snapshot is never a green.

## Regenerating the snapshot

```sh
cmake --build build --target contract_inventory
./build/src/contracts/contract_inventory \
    --source-root . --out data/contracts/contract_graph.snap.json
```

Review the diff as a conscious contract update — that friction is the
design.

## Cost

Tooling-side only; zero product-runtime changes. Measured by
`benchmark_contract9` (see PERFORMANCE.md).
