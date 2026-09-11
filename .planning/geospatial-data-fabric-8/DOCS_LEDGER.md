# DOCS LEDGER — 8.0 documentation changes

| Document | Change | Reason |
|---|---|---|
| `CHANGELOG.md` | new 8.0 entry (P0 fix, identity token/bridge, STAC UTC, range-cache fault evidence, multidim string axes, GeoParquet certification, doctor additions, CLI surfaces, credential docs) | user-visible behavior + fixes |
| `docs/io/cloud-credentials.md` | NEW: provider-neutral credential-safety boundary | package G deliverable |
| `docs/io/certified-formats.md` | GeoParquet row: Certified round-trip (driver-gated) with test pointer | profile upgrade backed by executable evidence |
| `docs/io/stac-interop.md` | UTC normalization section (mixed offsets, class-split ordering, duplicates, assumed-UTC) | new 8.0 behavior contract |
| `src/data/execution_identity_resolver.h` | header comment corrected: `setExecutionIdentityResolver` returns the installed resolver, not the previous one (doc/impl mismatch) | honesty fix; consumers save the global explicitly |
| `src/geospatial/remote/remote_identity_token.h` | full contract doc (fail-closed rule, basis hygiene, single-probe form) | new public API |
| `src/geospatial/util/time_normalization.h` | parse contract incl. the epoch-nanosecond representable range (≈1678–2262) | P1 remediation documentation |
| `src/geospatial/multidim/multidim_view.h` | string-axis selection contract; `resolvedValue` = index for string axes | A10 remediation |
| `src/geospatial/formats/format_profiles.cpp` | GeoParquet notes (certified where create-capable; layer-name convention); capability JSON gated on driver availability | A7 remediation |
| planning files (`.planning/geospatial-data-fabric-8/`) | GOAL/BASELINE/OWNERSHIP/ARCHITECTURE/CAPABILITY_MATRIX/PLAN/MILESTONES/TEST_MATRIX/PERFORMANCE/REVIEW_LOG/FINAL_REPORT | track contract |

No generated help/catalog artifacts changed in this track (the `data` CLI
usage string is the only help surface touched; it now lists the new
subcommands).
