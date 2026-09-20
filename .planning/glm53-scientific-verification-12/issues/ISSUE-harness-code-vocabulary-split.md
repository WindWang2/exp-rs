# Issue: the harness error-code vocabulary is split across two files, and the contract graph scans only one

**Severity:** P1 (contract graph silently under-approximates the error taxonomy;
the byte-gated snapshot hides it)
**Found by:** Track 02 `glm53-scientific-verification-12`, oracle O-6
**Status on master:** **live defect**, reproducible by a single command
**Ownership:** `src/agent/harness/**` — **outside** Track 02's writable set,
hence issued out rather than patched.

---

## Summary

`graph_assembly.cpp` builds the harness `error_code` node set by scanning
**only the `kXxx` string constants in `harness_error.h`**. The authoritative
harness vocabulary, however, also lives in the `errorCategoryForCode()` table
in **`harness_error.cpp`**, which is **larger**:

| source | codes |
|---|---|
| `harness_error.h` — `inline constexpr const char *kXxx = "…"` | **40** |
| `harness_error.cpp` — `{ "CODE", { "category", RetryClass } }` table | **66** |

**26 codes exist only in the `.cpp` table.** They are real, live, classified,
retry-policied codes. Any help page that documents one of them produces a
`diagnostic_for` edge into a node that the graph never created.

## Minimal repro (no build required — uses the prebuilt tool)

```
contract_inventory --source-root . --out live_graph.json
```

Observed output:

```
nodes: 1147 edges: 426 findings: 5
note: unreadable help file: ./data/help/commands.json
finding: dangling_ref diagnostic_for:harness/ACQUISITION_DATES_MISSING (target error_code node missing (diagnostics.json))
finding: dangling_ref diagnostic_for:harness/COMPLEX_BANDS_REQUIRED  (target error_code node missing (diagnostics.json))
finding: dangling_ref diagnostic_for:harness/DATES_NOT_ASCENDING     (target error_code node missing (diagnostics.json))
finding: dangling_ref diagnostic_for:harness/IO_ERROR                (target error_code node missing (diagnostics.json))
finding: dangling_ref diagnostic_for:harness/UNWRAP_PROVIDER_UNAVAILABLE (target error_code node missing (diagnostics.json))
```

Exit code **1** (findings present).

## Root cause, in two places

**(1) The node set is built from one file.** `src/contracts/graph_assembly.cpp`:

```cpp
errScanner.scanHarnessCodes(
    readFile( joinPath( sourceRoot,
                        "src/agent/harness/harness_error.h" ) ),
    errReport );
...
for ( const auto &[var, code] : errReport.harnessCodes )   // kXxx constants ONLY
{
    ContractNode node;
    node.id = "harness/" + code;
    node.kind = "error_code";
    node.origin = "harness_error.h";
    g.addNode( std::move( node ) );
}
```

`harness_error.cpp` is never read by the assembler.

**(2) The edge is created unconditionally.** In the same function, for every
`diagnostics.json` entry whose family is `harness` or `operator`:

```cpp
if ( family == "harness" || family == "operator" )
    g.addEdge( { "diagnostic_for", id, family + "/" + code, "diagnostics.json" } );
```

There is **no `hasNode` check** before `addEdge`, so a reference to a
`.cpp`-only code becomes a dangling edge rather than a build-time or
assembly-time error.

## The 26 codes with no `kXxx` constant

```
ACQUISITION_DATES_MISSING        BASELINE_NO_ZERO_DOPPLER_MASTER
BASELINE_NO_ZERO_DOPPLER_SLAVE   BASELINE_STATE_INTERPOLATION_FAILED
COMPLEX_BANDS_REQUIRED           COREGISTRATION_FAILED
DATES_NOT_ASCENDING              DEM_CRS_MISMATCH
DEM_EXTENT_INSUFFICIENT          DEM_GRID_UNSUPPORTED
GRID_CRS_MISSING                 IO_ERROR
NETWORK_INVERSION_EPOCH_LIMIT    NETWORK_INVERSION_PAIR_LIMIT
NETWORK_INVERSION_PATTERN_BLOWUP NETWORK_INVERSION_RANK_DEFICIENT
ORBIT_EPOCH_MISMATCH             ORBIT_SEGMENT_INVALID
PAIR_GRAPH_DISCONNECTED          SCENE_TRUTH_INVALID
TOPO_PHASE_METADATA_MISSING      TOPO_PHASE_ORBIT_COVERAGE
UNWRAP_PROVIDER_FAILED           UNWRAP_PROVIDER_INVALID_OUTPUT
UNWRAP_PROVIDER_TIMEOUT          UNWRAP_PROVIDER_UNAVAILABLE
```

These are in active use, e.g.:
- `src/agent/harness/capability_catalog.cpp:41-42`
  (`"TRAINING_INVALID", "COMPLEX_BANDS_REQUIRED", "ACQUISITION_DATES_MISSING",
   "DATES_NOT_ASCENDING", "UNWRAP_PROVIDER_UNAVAILABLE"`)
- `src/agent/spatial_tools/result_assessment_tool.cpp:171,276`
  (`SpatialToolResult::failure( …, "IO_ERROR", "io", false )`)
- `src/processing/algorithms/sar/sar_unwrap_provider.cpp:86`
  (`QStringLiteral( "UNWRAP_PROVIDER_UNAVAILABLE" )`)

Five of them (`IO_ERROR`, `COMPLEX_BANDS_REQUIRED`, `ACQUISITION_DATES_MISSING`,
`DATES_NOT_ASCENDING`, `UNWRAP_PROVIDER_UNAVAILABLE`) currently have a
`diagnostic.harness.*` help page, which is why there are exactly **five**
dangling edges rather than 26.

## Why the committed snapshot masks this — the important part

The committed `data/contracts/contract_graph.snap.json` reports **0** dangling
targets. It also has **no node** for any of the five codes and only **52**
`diagnostic_for` edges, versus a larger live set. The snapshot is from an
**earlier tree**: the five diagnostics were added later and the snapshot was
never regenerated.

So the situation is:

- The **byte gate** (`contract_inventory --check`) is RED, for stale bytes.
- The **structural problem** (five broken references) is invisible, because the
  file that would exhibit it is the stale one.

This is the worst ordering. The tool's own remedy text says:

> `SNAPSHOT STALE: … differs from the live contract graph.`
> `Remedy: contract_inventory --source-root <repo> --out <file>`
> `then review the diff as a conscious contract update.`

A maintainer following that remedy would regenerate the snapshot **with the
five dangling edges in it** and commit them as the new baseline — turning a
masked defect into a blessed one. That is precisely the "never auto-accept
unknown registry drift just to make snapshots green" non-goal.

## Suggested fixes (for the owning track / the platform)

**Preferred — make the vocabulary single-sourced.** Have
`error_code_scanner` also parse the `errorCategoryForCode()` table in
`harness_error.cpp` (the pattern `{ "CODE", { "category", RetryClass::X } }` is
regular and already machine-scanned elsewhere), or generate the `kXxx`
constants from the table. Then `harness_error.h` stops being the de-facto
vocabulary.

**Minimum — make the inconsistency impossible to ignore.** Add a `hasNode`
guard in `graph_assembly.cpp` before creating the `diagnostic_for` edge, and
record a finding (rather than a dangling edge) when the target is absent, so the
failure names the *file* that must change.

**Regardless — the platform-side fix (Track 02's scope).**
`tests/test_snapshot_drift_12.cpp` now asserts
`buildLiveGraph(sourceRoot()).graph.computeFindings().empty()`, so this class of
breakage is reported as a *structural finding* and can no longer hide behind a
byte mismatch. The lane is expected to FAIL on master with exactly these five
edges; that red is the signal.

**Do not** "fix" it by deleting the five help pages — that removes authored
content to silence a finding.

## Related Track 02 artifact

- `tests/test_snapshot_drift_12.cpp` — structural gate over both the committed
  snapshot and a fresh live assembly (Oracle O-6).
- `src/contracts/snapshot_diff.{h,cpp}` — readable drift report, so a
  regeneration review can see *what* changed.
- `EVIDENCE.md` E-13 records the full analysis.
