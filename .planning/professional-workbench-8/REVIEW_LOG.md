# REVIEW LOG — Professional Workbench 8.0

## Round 1 — adversarial review (M6)

Two read-only subagents over the full diff `origin/master...HEAD @ 9a449341e0`
(track maximum: 2 subagents). A = architecture/correctness; B = concurrency/
tests/performance/portability. Findings consolidated and deduplicated below;
every P0/P1 fixed, all reasonable P2s fixed, P3s fixed or justified.

| ID | Sev | Finding (short) | Disposition |
|----|-----|-----------------|-------------|
| A1 | P0 | Rasters smaller than the request always fail (fittedSize upsamples; readWindowResampled refuses upsampling) | **FIXED** — scale clamped to ≤1.0 (native-size preview); regression test added |
| A2 | P1 | Catalog index drifts on addChildToCollection (DataManager emits no per-asset signal) → children could vanish/duplicate | **FIXED without touching src/data** — collection children always render from the authoritative `childAssetIds`; standalone set excludes claimed children; index no longer mirrors membership; collection lifecycle events heal the index via rebuild; index-lag fallback fetches the snapshot; drift regression test added |
| A3 | P1 | Malformed schema (minItems > maxItems) hangs the GUI in the seeding loop | **FIXED** — loop bounded by maxItems (recursively, incl. nested arrays) |
| A4/B-1/B-2 | P1/P2 | Qt::UniqueConnection with lambda: assert-abort in debug builds, connection leak in release; UAF when receiver==service (destroyed lambda runs after members destructed) | **FIXED** — UniqueConnection dropped; receiver==this treated as receiver-less; dedupe via map-contains so one destroyed-connection per receiver |
| A5/B-6 | P1/P3 | "Optional-group absence" only existed in validate(); values() emitted empty/defaulted groups | **FIXED** — collectFields omits untouched optional groups (shared groupTouched rule, extended to recognize JSON-editor content); test pins absence |
| A6 | P2 | Nested raster/vector/asset/model combos never received pushed choices | **FIXED** — refreshComboChoices recurses into children and array items; test pins a nested raster combo |
| A7 | P2 | setValues no longer signal-silent for object arrays (rows created connected+unblocked mid-apply) | **FIXED** — new rows blocked before values are applied |
| A8 | P2 | Async-check warning flashed once then never returned (tooltip reset + same-result early return) | **FIXED** — tooltip re-applied on every delivery |
| A9 | P2 | Stale array-item paths could route async-check marks to the wrong row | **FIXED** — delivery is widget-pointer based (QPointer captured at schedule time; paths are diagnostics only) |
| A10 | P2 | Clearing a checked value left a stale failed mark | **FIXED** — empty value resets the property + canonical tooltip |
| A11 | P2 | In-flight raster preview could paint over collection/multi-selection details | **FIXED** — preview hidden on those details; callback verifies the pane's current source |
| A12/B-4 | P2 | suggestedNextAction docs promised unimplemented arms (broken/governance) | **FIXED (docs+header side)** — documented priority now matches the implementation; facts remain in ContextFacts; no relocate/open-result registry commands exist yet |
| A13 | P3 | Array-item children signal-connected twice | **FIXED** |
| A14 | P3 | Item rows not renumbered after removal | **FIXED** |
| A15/B-7 | P3 | minItems seeding skipped nested object arrays | **FIXED** (recursive seeding) |
| A16/B-10 | P3 | cancel() wholesale-clear could resurrect an in-flight canceled token | **FIXED** — prune removes only delivered (non-in-flight) tokens |
| A17 | P3 | rowCount() includes the sentinel + cap not documented | **FIXED** — API comment + §23 note |
| A18 | P3 | RGB composite masked NoData by band 0 only | **FIXED** — per-channel mask |
| A19/B-15 | P3 | No production SchemaEnumProvider / runAsyncChecksNow caller | **PARTIALLY FIXED, rest documented** — TaskPanelHost::setFormValues now forces async checks after restore; docs state honestly that no shell enum provider is installed yet (free-text degradation is the default until a host installs one). Full provider wiring is a deliberate follow-up (needs a shell-side DataManager/ModelCatalog binding decision) |
| B-3 | P2 | Overview-less huge rasters → unbounded native reads on the shared 2-thread pool, non-cancellable | **FIXED** — typed `Unsupported` above `kMaxNativePreviewPixels` (40 MP, no overviews); test pins the refusal |
| B-5 | P2 | removeAsset O(tail) → O(N²) batch unload | **FIXED** — swap-and-pop (order note documented) |
| B-8 | P3 | refreshEnumSources held raw Field* across the provider callback | **FIXED** — collects paths, resolves fresh at apply time |
| B-9 | P3 | Cache key vs dispatch normalized paths differently | **FIXED** — normalized once at request entry |
| B-11 | P3 | Context test used plain QApplication before QgsVectorLayer | **FIXED** — QgsApplication bootstrap like the sibling suite |
| B-12 | P3 | 500 ms wall-clock assertions too tight for Debug/CI | **FIXED** — 2000 ms ceilings |
| B-13 | P3 | rs_scan_pool.cpp listed twice in test_schema_form_4 | **FIXED** |
| B-14 | P3 | Dead onFilterChanged slot | **FIXED** (removed) |
| B-16 | P3 | `!self` → `invokeMethod(self.data())` check-then-use window | **RECORDED, no change** — standard Qt idiom; queued calls into a destroyed context are dropped by Qt; funnel is single-threaded destruction |

Also: one stray unrelated edit (tests/helper_external_process.cpp `<sys/wait.h>`
removal) appeared in the worktree during execution — reverted to master's
version; final diff contains no unrelated churn.

One unexplained single-run SIGABRT in test_asset_preview_service under
3-concurrent-build machine load was investigated: the reproducible defects
found were test-side (stack-object deleteLater misuse → heap corruption;
dangling capture in a helper lambda) and were fixed; 14+ consecutive clean
runs followed. Documented in PERFORMANCE.md.

Subagent budget: 2/2 used (A + B).
