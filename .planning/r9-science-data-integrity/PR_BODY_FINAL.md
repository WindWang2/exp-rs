# fix(science,data): close unresolved review threads from #1469/#1472/#1473 + #1456/#1450/#1457 still-open tails (R9 direction A)

## Summary

Direction-A hardening wave (r9-science-data-integrity): closes **6 unresolved inline review threads** left by already-merged PRs #1469/#1472/#1473 (all `isResolved=false`, re-verified against master source before adoption), plus the 3 genuinely still-open items found by re-auditing open issues #1456/#1450/#1457 against master (everything else in those issues is `ALREADY_FIXED_IN_MASTER` — full dedup matrix in `.planning/r9-science-data-integrity/PR_ISSUE_REVIEW_MATRIX.md`, committed on this branch). No new subsystems; every fix reuses an existing single authority (QgsDataSourceResolver, radiometric vocabulary, canonicalizeJsonRfc8785, durable_sidecar WriteStatus, StoreStmt::stepFailed).

## Fixes (Bug → RootCause → Repro → Fix → Regression)

| # | Finding (source) | Root cause & fix |
|---|---|---|
| 1 | #1469 review @ `radiometric_state.cpp` (also #1467/#1450) | Every non-drive-letter colon was classified as a provider URI, so a legal POSIX filename like `/data/acq:2026.tif` skipped both read AND write of its GDAL radiometric metadata; a reopened layer fell back to `DigitalNumber` and could re-calibrate already-calibrated data. New header-inline QtCore-only `local_source::isPlainLocalPath` (no new link edge for eo_preflight): a colon beyond a drive letter is a scheme separator only when it precedes every path separator; ambiguous spellings fail closed. **Red**: standalone repro — master logic wrong on 5/17 vectors, new rule 17/17 (committed); in-tree compile-red + 17-vector regression + `Q_OS_UNIX` end-to-end colon-file section. |
| 2 | #1472 review @ `radiometric_state.h` (#1450) | `ModelEoDomainContract::validate()` kept a private `kStates` without `sigma0`/`beta0`, so SAR models were rejected at manifest registration before the runtime preflight could compare through the shared vocabulary. kStates + error message + header doc now carry them. **Red**: `validateManifestJson(sigma0/beta0)` non-empty issues on master (exit 42 verified). |
| 3 | #1472 review @ `qgsdatasourceresolver.cpp` | Qt folds EVERY nonempty authority — localhost included — into `toLocalFile()` as an SMB-style `//localhost/...` path (independently probed on Qt 6.8.0/MSVC by round-1 review), which the early return passed through, so `file://localhost/C:/data/a.tif` failed every local existence check. localhost is now stripped (`setHost`) BEFORE `toLocalFile` — both platforms get the bare platform path; degenerate no-path form falls back to the source (fail-closed). Tests: platform-split localhost expectations + the percent-decode assertion made platform-aware (it failed on the Linux lane as-is). **Red on Windows lane verified** (25-char `//localhost/...` return, exit 42). |
| 4 | #1472 review @ `temporal_workspace.cpp` + `gdal_dataset_wrapper.cpp` | The temporal identity scan normalized `file://` only for the `scenes` parameter — any other datasource-valued temporal parameter hit `QFileInfo` on the raw URL (`unidentifiable local input`); the shared `params::fileExists()` fed the raw URL to `QFile::exists` so operators rejected valid `file://` inputs as FileNotFound. Both now route through `QgsDataSourceResolver::localPath()`. `rs_json_params.cpp` is a B-side shared file — minimal additive change (accepts strictly more inputs), flagged for B's serial integration. |
| 5 | #1473 review @ `data_manager.cpp` (#1457) | `WriteStatus::VerifyFailed` means the sidecar ALREADY renamed new bytes onto the target before read-back failed — but the boolean check took it as a plain failure and suppressed `assetChanged`, leaving displays on the pre-relocate VRT while the file changed underneath. The reload signal now follows what the TARGET holds: VerifyFailed → emit + typed `relocate.dependent_vrt_verify_failed` ERROR diagnostic; pre-publish failures keep the old warning/no-signal contract. Deterministic `SICNU_FAULT_POINT` seam (dataset_store pattern). **Red** verified (no diagnostic/no seam on master, exit 42). |
| 6 | #1473 review @ `dataset_store_samples.cpp` | Both prepare-failure branches in `removeSample` read `stmt.error()` AFTER rollback — a successful ROLLBACK resets the connection error to "not an error", losing the cause of a damaged-schema/authorizer-refused DELETE. Capture-before-rollback in both branches (the step branch already did). **Red**: DROP-TABLE injection per statement — master reads the reset string, fix names `no such table` and the base row survives the facet-prepare rollback (exit 42 verified). |
| 7 | #1456 STILL_OPEN @ `query_planner.cpp` | The catalog-walk truncation signal (query.maxItems stopped the scan) was unconditionally overwritten by the selector recompute, so `maxItems <= sceneBudget` planned as "not truncated". The two caps are now OR-ed. **Red**: both masked shapes false on master (exit 42 verified). |
| 8 | #1450 STILL_OPEN @ `suitability_*` | `contentDigest()` hashed Qt's Compact dialect — unreproducible by any canonical-path verifier. Both digests now hash `canonicalizeJsonRfc8785` (the same RFC 8785 authority as execution/dataset fingerprints). Values where the dialects agree keep their digest; divergent values migrate from unverifiable to verifiable (old artifacts already failed closed). Tests pin `digest == sha256(canonicalize(toJson()))`. |
| 9 | #1448/#1457 tails @ `dataset_store_facets/splits`, `workspace_catalog` | `facetDistribution`/`facetCrossCounts` return typed `dataset.store_query_failed` on mid-scan sqlite ERROR; channel-less listers (`facetNames`, `splitManifestsForVersion`, `leakageReportsForSplit`) follow the established loud-subset `qWarning` (#1333 item-10 pattern, signatures unchanged); workspace catalog's `Stmt` gains `stepFailed`/`error`, `page()/count()/byId()/byPath()` + alias/tag hydration warn instead of reading "no more rows", and the `open()` schema probe **fails closed** on a step error instead of falling through to `createSchema()` writing an older schema into a possibly-newer database. |

## Verification evidence (local MSVC lane; no online CI waited on)

- Configure: `configure_r9.cmd` (Ninja, Qt 6.8.0, vcpkg GDAL 3.12.4/PROJ 9.8.1, `-j2`) — exit 0.
- Targeted build of all 7 affected test targets: `cmake --build build-dev --parallel 2 --target ...` — **exit 0**.
- **Two full green passes** (identical parameters, before/after the red-stash cycle): `test_radiometric_state` 9/9 (111 asserts), `test_datasource_resolver` 14/14 (97), `test_sample_label_annotation` 17/17 (173), `test_suitability_spatial` 20/20 (213), `test_io_fabric_plan` 8/8 (95), `test_virtual_raster_create` 12/12 (133), `test_eo_platform_10` 21/21 (578) — **101 cases / 1400 assertions ×2, all exit 0**.
- Formal CTest record: `ctest -R <5 key new cases> -j1` — **5/5 Passed, exit 0** (#67/#246/#278/#592/#841).
- **Red proof** (`git stash` src fixes → rebuild → run → pop): fixes 1(compile+standalone),2,3,5,6,7 fail on master code with the exact expected assertions (exit 42 logs committed in the ledger); 8 is a contract lock (dialects agree byte-wise for these objects — honestly recorded as not-red); 4/9 verified at code level + adversarial review (deterministic mid-scan CORRUPT injection is not constructible portably; matches the #1448 sweep precedent).

## Independent review (two rounds, neither reviewer implemented anything)

- **Round 1** (full adversarial pass, incl. independent Qt 6.8.0 probe programs): 2×P0 + 1×P1 + 7×P2 → **all P0/P1 closed** (P0-2 bad `\a` escape in a test literal; P0-1 the original Windows-localPath assumption was **falsified by measurement** and the fix rewritten to the setHost approach; P1 resources/icons symlink deletion excluded from staging — mechanically checked below). P2s: 4 fixed, 3 recorded.
- **Round 2** (delta review by a third agent; re-compiled every changed TU with the project's own compile commands, re-probed the localhost matrix): **MERGEABLE, zero P0/P1 outstanding**; two advisory P2s recorded below.

## Commits

10 commits: one per work item + planning-track docs. `git diff --check` clean; staged set verified at exactly the intended files (no build artifacts, no `resources/icons`, no local runner scripts).

## Cross-boundary notes

- B-side shared files, minimal additive changes flagged for serial integration: `src/operators/framework/model_catalog.{cpp,h}` (+2 tokens), `src/operators/framework/rs_json_params.cpp` (route fileExists through localPath).
- Adjacent same-shape issues recorded, deliberately not expanded here: `rs_classification_pipeline.cpp:165` (A-adjacent; needs bool→status contract), `agent_loop/session_journal.cpp:62`, `agent/harness/evidence.cpp:60` (B); `science_context/asset_state_provider` unlocked resolver writes (needs C ownership call); SHA-256 fork consolidation + spatial_split layering debt (structural, #1450/#1467).
- Advisory P2s from round 2 (non-blocking): busy_timeout could be set before the schema probe; the manual UNC fallback is belt-and-suspenders under Qt 6.8.

## Not verified

- POSIX/macOS lanes (colon-named GeoTIFF end-to-end, `file://localhost` POSIX spelling): covered by platform-aware assertions + Qt-documented behavior + reviewer probes on this lane; **NOT_VERIFIED(Linux/macOS)**.
- Sanitizer lanes not run (MSVC dev lane; no ASan/UBSan toolchain configured here).

## SHAs

- base: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` (= origin/master at push time, re-fetched)
- head: `6f1c3b8589fa1d80f5e16efe4c77323a84fee5e4`

**Online CI not waited on** (per track protocol; all verification above is local and reproducible via the committed runner scripts' commands).
