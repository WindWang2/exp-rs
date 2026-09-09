# REVIEW_LOG — Unified Help 6.0

## Self-review during development

| # | Finding | Severity | Resolution |
| --- | --- | --- | --- |
| S1 | `HelpId` grammar initially required lowercase segments, but the goal's own examples (`command.layer.toggleEditing`) use camelCase from the authoritative registry; derivation and content must agree byte-for-byte | P1 | Grammar allows `[A-Za-z0-9_]`, case-sensitive verbatim from source registries; parameter ids keep schema camelCase. Enforced by shared derivation (`HelpId::parameterId`, `HelpId::diagnosticId`) + drift tests. |
| S2 | `domainForOperatorId` replaced `:` with `_`, so derived operator ids never matched authored JSON (`rs_sar_speckle` vs `rs.sar_speckle`) | P0 (caught by test) | Fixed to `:`→`.`; tests cover derivation round-trips. |
| S3 | `workbench.*` ids are 2-segment by design (`workbench.classification`); grammar required 3 | P1 (caught by content validation) | Grammar minimum lowered to 2 segments (kind + name). |
| S4 | `OpenCvError` normalizes to `open_cv_error`, JSON had hand-written `opencv_error` | P2 (caught by content validation) | JSON corrected; ids must always equal the derived mapping (checked at load). |
| S5 | QRC resource initializers inside a static library are dropped by the linker → empty help registry | P0 (caught by test) | help_content.qrc is compiled into executables (app, CLI, tests) instead of sicnu_help; documented in CMakeLists. |
| S6 | GDAL 3.11 (`OGRSpatialReferenceH` = `void*`): `static_cast` in `canonical_metadata.cpp` / `crs_policy.cpp` fails to compile | P1 (pre-existing on this vcpkg baseline) | `reinterpret_cast` (valid for both handle forms), mirroring master's earlier GDAL-compat commits. |
| S7 | Registry `upsert` needed by providers: pre-merged knowledge entries collide with derived descriptors during composition | P1 (caught by test) | `upsertDescriptor` + knowledge-snapshot pattern in `composeHelpSystem`; no duplicate-id noise on startup. |
| S8 | MCP `get_tool_help` initially dereferenced `registry.find()` without a null check for key parameters | P1 (self-review) | Removed the lookup — key parameters are listed as capped names; no unguarded dereference remains. |
| S9 | `HelpCompact::toJsonText` budget fallback could exceed the promised cap | P2 (caught by test) | Rewritten with a guaranteed-bound ladder (extended → plain → elided → id-only). |

## Adversarial review (Milestone N)

Two read-only reviewers (the track's full subagent budget), both completed.

### Reviewer 2 — scientific accuracy / remediation / coverage (data/help/**)

Verified clean before findings: 105 operator entries bijective with the
registry; docs links resolve; no secrets; summaries ≤120 chars; terminology
consistent with CONTEXT.md; retry=transient only on genuinely transient
codes. Verdict: **1 P0 / 8 P1 / 5 P2 — all resolved:**

| # | Sev | Finding | Resolution |
| --- | --- | --- | --- |
| 1 | P0 | DOS1 described as zero-reflectance dark object; implementation is Chavez-1% | whatItDoes/assumptions/limitations rewritten (Chavez 1996, T=1) |
| 2 | P1 | atmospheric_correction advertised nonexistent 6S model + aerosolType/aod | retitled 大气校正（DOS/QUAC）; keyParameters → method/band/gain/bias; commands.json purpose fixed |
| 3 | P1 | temporal_composite method enum wrong (min/max), quality_band typed as string, target_date misexplained | method = best_pixel/mean/median; quality_band = 1-based band number; target_date = tie-break |
| 4 | P1 | sar_change cited unimplemented ki/CFAR thresholds | manual/otsu/percentile/statistical |
| 5 | P1 | terrain_flow overstated MFD + river/watershed products | D8-only wording; keyParameters → product/nodata |
| 6 | P1 | guidance actions used `command.` prefix the consumer doesn't strip | prefix stripped in all six entries |
| 7 | P1 | dangling helpTopic concept.rs.gcp | → concept.rs.rpc |
| 8 | P1 | qa_mask keyParameters platform/flags nonexistent | → source/mask/qa_band |
| 9 | P2 | keyParameter drift (threshold_raster, change_detection enums, speckle enhanced_lee, sar_calibrate outputDomain, s2/landsat import, morphology/sieve/focal/proximity, rx_anomaly) | all aligned to live schemas |
| 10 | P2 | "gdal doctor" tool does not exist | → data doctor |
| 11 | P2 | six whyItMatters fields were filler | consequence-bearing text |
| 12 | P2 | outside_raster remediation could clip on CRS mismatch | CRS check first |

### Reviewer 1 — architecture / GUI / context resolution / search

Verified clean before findings: layer separation (no app/operator includes in
sicnu_help), qrc embedded in every consumer, search bounds/determinism, no
hover-time Markdown parsing. Verdict: **1 P0 / 3 P1 / 10 P2 — all resolved:**

| # | Sev | Finding | Resolution |
| --- | --- | --- | --- |
| 1 | P0 | QUrl lowercases `helpid://` HOST → all camelCase related links dead | ids travel in the path (`helpid:/<id>`, `url.path()`); QUrl regression test added (test_help_core) |
| 2 | P1 | parentless modeless Help Center over an app-modal dialog = frozen window | openHelpCenter suppresses while activeModalWidget() != nullptr |
| 3 | P1 | category tree never listed topics (home text promised browsing) | buildCategories populates one child per descriptor; search filters children |
| 4 | P1 | F1 filter ignored modifiers: hijacked Shift/Ctrl+F1 and left the HelpContents QAction unreachable while its label advertised F1 | bare-F1 + !isAutoRepeat() required; dead F1 binding removed from 帮助内容; "(Shift+F1)" label corrected |
| 5 | P2 | upsertDescriptor skipped deprecated/diagnostic validations; providers dropped deprecation state | validations factored in; applyKnowledge copies deprecated/supersededBy |
| 6 | P2 | nested parameter defaults clobbered by parseCommon | defaults applied after parseCommon |
| 7 | P2 | loaders not recursive despite contract | QDirIterator(Subdirectories) for disk + resources |
| 8 | P2 | toJsonText built JSON by concatenation (no escaping), no diagnostics field | QJsonObject/QJsonDocument with field-drop ladder incl. diagnostics |
| 9 | P2 | diagnostic-resolution test passed vacuously on empty registry | composes first; requires curated (registered) descriptor |
| 10 | P2 | availability fact table drift unguarded; unknown commands report available | coveredCommandIds() seam + drift test vs shell command source |
| 11 | P2 | tokenizer dropped supplementary-plane CJK | toUcs4 code-point walk; Ext B–F ranges added |
| 12 | P2 | CLI list-topics blank command titles; no reverse knowledge drift check | id fallback in CLI; reverse commands.json→source test |
| 13 | P2 | composeHelpSystem "idempotent" claim overstated | explicit first-composition-wins early-out |
| 14 | P2 | F1 while Help Center focused navigated away (own guard unreachable) | openHelpCenter keeps place when Help Center is the active window |

Post-review verification: test_help_core 11 cases / 2144 assertions green;
test_help_coverage 8 cases / 7780 assertions green (incl. new QUrl,
reverse-drift and availability-table drift tests); full tree rebuilt; CLI
projections re-verified; docs regenerated.
