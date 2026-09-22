# 01 — Recon baseline (2026-09-23)

## Repo state at start

- `origin/master` = `a9dc33fa73` (merge of #1236 suitability). 0 open issues.
- Open PRs and ownership fences respected:
  - #1237 `src/teaching/**`, `src/app/teaching/**`, main_window shell.
  - #1238 `src/experiment_studio/**`; #1239 `src/teaching_admin/**`
    (its `release_preflight.*` is a different, teaching-domain module).
  - #1240 `src/science_context/**` + `src/agent/data_platform_tools.cpp`.
  - #1241 `src/agent_ops/**`.
  - #1242 `src/geospatial/fabric/*`, `src/geospatial/stac/stac_client.cpp`,
    `src/geospatial/raster/raster_reader.*`.
  - #1243 spectral resampling; #1244 temporal operators + `temporal_fit.*`.
- No open PR touches this slice's files (verified by `gh pr diff
  --name-only` for all 8 open PRs).

## Module inventory (现状矩阵, condensed)

| Layer | Files | Authority | Consumed by | Tests | State |
|---|---|---|---|---|---|
| Harness intent preflight | `src/agent/harness/scientific_preflight.{h,cpp}` (1319), `band_facts.{h,cpp}` | static IntentSpec table + `spatial:raster_inspect` facts + ModelCatalog + CapabilityKnowledge | `plan_tools` harness:preflight/execute_plan gate, capability_graph, session_seams | test_platform5 eval, test_harness_evals, test_capability_drift, test_harness9_contracts | live |
| Typed preflight schema | `src/preflight/*` (`sicnu.preflight.report/1`, #1207 slice A) | none (schema leaf) | **nothing — orphaned** | test_preflight_report_schema.cpp (**unregistered**) | dead code |
| Workflow preflight | `src/agent/spatial_tools/workflow_preflight_tool.*` | RSOperatorRegistry | spatial tool registry | test_agent_tools_3 | live |
| Passport core | `src/scientific_state/*` (#1195) | GDAL collector → resolver (pure) | CLI `passport` commands (only production entry) | test_scientific_state_* (9 suites) | live, thin wiring |
| Suitability | `src/suitability/*` (#1236) | dataset store / facts providers | agent tools `suitability:assess/profiles` | test_suitability_* (11 suites) | freshly merged + reviewed |

Other same-named preflight systems (`virtual_raster_preflight`,
`algorithm_preflight`, `eo_preflight`, `temporal_preflight`, cartography
preflight) are separate domains; only the harness chain and the typed
schema layer are in this slice's scope.

## Structural findings (this slice's P0/P1 set)

1. **P0 — `src/preflight` is outside the build graph.** Root CMakeLists
   has no `add_subdirectory(src/preflight)`; `tests/CMakeLists.txt` never
   registers `test_preflight_report_schema` (the file's own header says it
   must fail until the module exists — it existed, uncompiled). The #1207
   "7 cases green" claim is unreproducible on master.
   **Dedup outcome (PR time): open PR #1246
   (`hardening/integration-build-contract-drift`) wires exactly this — same
   `add_subdirectory`, the same test registered, plus a wiring drift
   oracle. Per the conflict gate this branch DROPPED its own wiring commit
   and leaves the shared central files to #1246; the finding is recorded
   here as independent confirmation of #1246's recon.**
2. **P1 — grid facts silently zero on the production shape.**
   `raster_inspect_tool.cpp:222,233` emits `size`/`pixelSize` as objects;
   `spatial_contracts.cpp` passes them through; `band_facts.cpp gridFacts`
   parsed only arrays → `opticalChangeRules` size/resolution checks,
   `sarChangeRules` size warning, `inferenceRules` resolution window and
   `multimodalRules` resolution comparison all no-op on inspect-derived
   inputs. The eval suites never fed `size`/`pixel_size` (blind spot).
3. **P1 — `skip_preflight=true` bypasses the blocked-verdict gate**
   (`plan_tools.cpp:1050`), contradicting `scientific_preflight.h:12-14`
   ("blocked plans are refused; the LLM cannot override").
4. **P1 — non-finite doubles break the passport's self-consistency**
   (recon agent, verified): NaN geotransform → `pixel_size` NaN →
   serializes to JSON `null` → the module's own `fromJson` rejects the
   document it just produced; `1e+9999` parses as +inf and re-serializes
   to a non-strict token.
5. **P2 — empty `temporal_facts` bypass**: a declared-but-empty facts
   object found the "has facts" branch, ran zero checks, emitted nothing —
   the phenology min-scene demand was bypassable.
6. **P2 — degree/meter confusion**: `inferenceRules` compared a degree-based
   `pixelSizeX` against `min/max_resolution_meters`.
7. **P2 — collector metadata cap counted scanned entries, not stored ones**
   (`gdal_state_facts.cpp:25`), so `dropped()` stayed 0 on >512-item files
   and the "truncation is never silent" note never fired.
8. **P2 — suitability extent doubles had no finite guard** (violating the
   module's own Slice G standard) — `1e+9999`/missing members silently
   became ±inf/0.

## Dedup record

- #1236 review already hardened suitability goals/integral casts; the
  extent-double gap is the remaining unguarded field class — new work.
- #1242/#1244/#1243/#1240 own adjacent files; this slice's diff does not
  touch any of them (verified at PR time, see 03-pr-body.md).
- Old branches (`agent/flash-*`, `rs14-unified-verifier`) mined for clues
  only; no code ported.
