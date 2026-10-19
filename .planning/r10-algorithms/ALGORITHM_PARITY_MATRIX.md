# ALGORITHM_PARITY_MATRIX — R10-A (WP A1 deliverable)

Generated from the live registry truth on master `8a0864276` (method and full
189-row evidence table: `research/A1_PARITY_DATA.md`; this file is the
decision-facing summary). "Sidecar" = `data/processing/algorithm_meta/` (top
level + `capability/`); "manifest" = `data/processing/toolbox_manifest.json`.

## Registration truth

| family | registered | registry entry point | ghost ids | missing from any declared surface |
|---|---|---|---|---|
| rs | 157 (17 OPENCV-gated) | rs_operators_init.cpp:307-485 | 0 | — |
| io | 17 | io_operators_init.cpp:39-68 | 0 | capability catalog (rs-only by design, undeclared) |
| opencv | 6 | opencv_operators_init.cpp:24-42 | 0 | same |
| gdal | 5 | gdal_operators_init.cpp:25-44 | 0 | same |
| otb | 4 | otb_operators_init.cpp:23-39 | 0 | same |

Macro table == guaranteed add() table for every family (the #707 dual-path
parity holds — no drift).

## Manifest / sidecar parity

| surface | entries | parity result |
|---|---|---|
| toolbox_manifest.json | 50 ids (42 required + 18 handcrafted, deduped) | 0 ghosts; all ids resolve to live provider algorithms; bidirectional gate test_toolbox_coverage.cpp green-by-design |
| algorithm_meta/ top level | 57 (56 rs + gdal:polygonize) | 0 ghosts; export is task-gated by design (only operators with `meta["task"]`) |
| algorithm_meta/capability/ | 157 rs sidecars + relations graph | set == rs registry exactly (0 missing / 0 ghost); **D1**: 63 sidecars flattened array parameters → FIXED in this branch (commit "fix(capability): sidecar parameter rows carry array semantics…") + independent drift gate added |

## Parameter-level mismatches

- Names / required flags: 157/157 sidecars verified against code `schema()`
  paths — no ghost parameters, required semantics match (4 audited in depth).
- Types: the ONLY systematic mismatch was D1 (array flattening) — fixed here.
- Remaining known gaps (not parameter bugs):
  - [P3] `otb:bundle_to_perfect_sensor` inherits the base-class empty schema
    while `otb_operator_base.cpp` strongly validates `output` — schema under-docs
    the contract (left as-is this round; noted for the OTB family owner).
  - [P3] OPENCV=OFF builds leave 17 capability sidecars without their
    registration (build-config gap, coverage gate is OPENCV-gated too).
  - [P2] capability discovery is rs-only without saying so → INTERFACE_HANDOFF
    item 3 (agent-side surface).

## Test coverage (per family)

1016 test translation units / 963 declared targets; operator-id direct
references: 186/189 (`gdal:pansharpen`, `rs:change_mad`, `rs:change_ratio`
have indirect coverage). Five parity gate suites (26 cases) incl. the new
array-semantics gate from this branch.
