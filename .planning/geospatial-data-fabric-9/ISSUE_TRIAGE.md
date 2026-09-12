# ISSUE TRIAGE — geospatial-data-fabric-9 (re-verified on `132da5e998`)

Method: every open issue re-located on the latest master code, not old line
numbers. Classification per /goal vocabulary.

| Issue | Title (short) | Sev | Classification | Ownership / action |
|---|---|---|---|---|
| #848 | TerrainFlow fillDepressions NoData borders | critical | still-valid (on our base; local-only master commit `8f6293bceb` claims fix — not merged, not our area) | processing — NOT ours |
| #849 | SelectionContext heap UAF | critical | same as above | workbench — NOT ours |
| #850 | VectorWriter move drops mTransactionActive → silent GPKG rollback | critical | **still-valid on `132da5e998`** (verified: move ctor/assign at vector_writer.cpp:197-220 never touch `mTransactionActive`) | **OURS — M0.** Root cause: move transfers handle/paths/finalized but not transaction state. Fix + regression test (old code must fail). |
| #851 | TaskCenter::flushPendingLaunches self-deadlock | critical | still-valid | task center — NOT ours |
| #852 | Temporal workspace off-affinity reads | critical | still-valid | data manager — NOT ours |
| #853–#857 | hydrology UB, SAR, spectral sentinels, display leaks | high | still-valid | processing/display — NOT ours |
| #858 | Spectral/Histogram widget dead listeners → GDAL handle leak | high | still-valid; widget-side fix | workbench widgets — NOT ours (handle ownership in widgets, not in geospatial) |
| #859–#868, #876–#879, #881, #882 | workbench/concurrency/help/cartography families | high/med | still-valid | NOT ours |
| #869–#872 | help/diagnostic/operator schema drift (rs:infer etc.) | high | still-valid | schema-governance track (Track 10) — NOT ours, except io:* runtime truth |
| #873 | domainFromDeclaredScale +Inf divisor | med | still-valid | processing contracts — NOT ours |
| #874 | Float32 sentinel strict double equality in readMask | med | **still-valid on `132da5e998`** (verified: raster_reader.cpp:330) | **OURS — M0.** Fix: compare sentinels in the band's storage precision (Float32 bands compare in float space); NaN path unchanged; regression with a non-float-exact sentinel. |
| #875 | SplitEngine div-by-zero SpatialKFold | med | still-valid | dataset — NOT ours |
| #880 | io:inspect parameter schema drift | med | still-valid; mechanical governance assigned to Track 10 | **OURS (runtime contract truth only, M6/M9)**: the `io:inspect` schema() must describe the real runtime params. We verify with a mechanical schema-vs-runtime test for io:* operators owned here; the JSON/help projection drift remains Track 10's. |

## Issue → milestone → verification map (our issues)

| Issue | Milestone | Verification |
|---|---|---|
| #850 | M0 | `tests/test_io_vector_contract.cpp`: move a mid-stream GPKG writer → finalize → reopened layer has ALL features (fails on old code: 0 features). Plus cancel-after-move discards staging. |
| #874 | M0 | `tests/test_io_raster_contract.cpp`: Float32 raster, NoData = -9999.9 (not float-exact), window mask marks sentinel pixels 0 (fails on old code: 255). |
| #880 | M6/M9 | mechanical test asserting io:* schema() names == runtime param reads for the io family we own (drift guard). |

## Not duplicated / deliberately not touched

- Remote branches `*-5/6/7/8` are merge residue — no unmerged I/O work exists
  on any remote branch that this track must absorb (verified by comparing
  branch tips against merge history in BASELINE.md).
