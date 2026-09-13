# REVIEW_LOG — two-pass adjudication record

Format per entry: `[date] <finding-id> verdict: KEPT|RETRACTED reason: … reviewer: main|V|C`

## Round 1 (main agent, inline self-review while writing findings)
(entries appended as findings are written)

## Round 2 (Phase 7: subagent V false-positive sweep; subagent C coverage audit)
(pending)

## False-positive tally
- submitted: 0
- retracted: 0

## Round 1 — operators area method note (2026-09-13)
Deep line-by-line reads: framework/(12), runtime/(20 incl. tile_inference_engine 3304, model_runtime, providers, planner, blob), rs/: spectral_index, change_streaming (1355), raster_spatial_operators (776), sar_geocode (603), obia_classify core, qa_mask, mosaic (head+schema), io_operators, gdal warp utils + orthorectification (#694 verify), raster_spatial ops. Remaining operators files (schemas/metadata/init/declaration headers, small adapters): structural pass + lens-driven risk scan (memcpy/threads/divisions/at()/resource-close sites), flagged lines read with context. Recorded per DECISIONS D-004.

## Considered-and-dropped (operators)
- `ModelCatalog::resolve()` bypasses mUnregistered (model_catalog.cpp:2074-2083) while findLocked() honors it — contract inconsistency, but `unregister()` has zero production callers → unreachable state; dropped (P3-if-anything).
- `rs_feature_normalize_operator.cpp:314-317` `(v-lo)/span` with span==0 → NaN→int cast UB, but result std::clamp'd immediately; benign on all supported targets; dropped.
- `bit-exact` (schema root, stampDeterminismGrade) vs `bit_exact` (algorithm_descriptor) dual vocabulary — separate surfaces, no cross-feeding site; dropped.
- rs_change_streaming IR-MAD weights frame lacks the 2^31-px guard the mask path has — bad_alloc path surfaces as std::exception (logged+rethrown by execute()); behavior acceptable; dropped.

## Area method notes (Phase 7 bookkeeping, per subagent C audit)
- **jobs/** (1,465 LOC): FULL read of job_engine.cpp (1120) + headers. Zero findings; #684/#798/#799 remediations verified in place.
- **workflow/** (6,238 LOC): FULL read of workflow_run_coordinator.cpp (1379) — resume identity gate, lock ordering, ghost-run closure (#727/#750/#731/#860/#876/#931/#944) all verified. Headers/remaining files structural.
- **sdk/** (11,768 LOC): FULL reads — ipc_channel.cpp (604), ipc_frame.cpp (172), ipc_frame.h, plugin_registry lock-face map + lock-drop protocol regions. #896/#925/#926/#928/#942/#943 verified. Remaining files (manifest/package/validator/ui_schema/capabilities/discovery/diagnostics/loader) structural pass + risk scan. C flagged plugin_manifest.cpp logic-dense — spot re-read below.
- **app/** (115,688 LOC): workbench/display lifecycle core verified (inspector_host rescueSectionsFrom, command_registry m_shortcutOwners, selection_context QPointer guards, display manager stopRendering discipline); delta hunks (schema_form_builder #927) read; remaining 519 files structural pass + lens risk scan (top-20 by risk score identified; classification/georef windows previously covered by 2026-08 audits F-101..F-106 and #777–#796/#849/#857–#859/#882/#893 remediation waves). C flagged rs_sift_matcher.cpp logic-dense — spot re-read below.
- **tests/** (184k LOC, lens 6): vacuous-assertion sweep (5 hits — all stress-survival idiom, family adjudicated by #656), zero-assertion heuristic (68 hits — all helper/REQUIRE_THAT/fuzz-lambda false positives, sampled 3), shouldfail/SKIP clean, delta tests assertion-dense with tolerances. No new findings.
- **pi/** (4 files): FULL read of mcp_bridge.ts + exp-rs-spatial.ts + both test files. 2 findings.

## Round 2 — subagent adjudication (2026-09-13)
- **Subagent V** (false-positive sweep): 6/6 findings KEPT, 0 retractions, severities confirmed. Evidence refinements folded: F-OPS-2 bytes are value-initialized zeros (not uninitialized); F-PI-1 zombie scoped to unterminated/continuous oversized output; F-OPS-4 strengthened by io:clip consuming the same parameter functionally (io_operators.cpp:383-386) — proves drift, not intent. V transcripts: run evidence in agent records.
- **Subagent C** (coverage audit): ledger structurally honest (all first-party files enumerated, exclusions justified, reviewed rows timestamped). Findings acted on: F-OPS-5 filed (detection_postprocess orphan count was a real unfiled finding); finding_count attribution fixed; 551 src/ui rows added (H tier); PLAN.md statuses refreshed (below); thin-evidence spot re-reads below. Remaining pending dirs honestly tracked (processing/agent/geospatial/data/analysis/governance areas — re-passed in the closing sweep, see ledger).
- **False-positive tally**: submitted 7, retracted 0 (round-1 inline drops: 4, recorded above).

## Closing sweep (1,066 pending files → reviewed, 2026-09-13)
Per-directory method: risk-ranked top files deep-read/verified (mcp_server sandbox+dispatch 2802, plan_tools identity, cartography composition detectCycles #863, governance_store WAL/tx #751, data_manager snapshot/affinity, dataset_store commit #774, experiment_store BEGIN IMMEDIATE #811, output_committer rollback #617, plugin_host_worker_main kill-ladder, plugin_validator containment #756, range_cache snapshotConfig #903, sdk plugin_validator/manifest spot); remaining files structural pass + lens risk scan per D-004. src/ui 551 files: Qt Designer assets, H-tier structural check.
