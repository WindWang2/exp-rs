# hardening(r20-03): Pi/MCP harness — IPC result-type gate, capability-loader fail-closed closure, typed tool refusals

**BASE** `28e1891390` (= origin/master at live recon; re-verified before push)
**HEAD** `19fad36ec8`

## Scope (track 03/05)
All changes inside `src/sdk/exprs`, `src/agent/harness`, `src/agent_ops`, two derived data files (`data/processing/algorithm_meta/capability/rs-sam-classify.json`, `pi/knowledge/capability-classification.md`, both tool-regenerated) and three test files. No cartography/mapspec/layout_tools/symbology, no cli/app/geospatial (ownership proof: `.planning/zcode-r20-03/OWNERSHIP.md`).

## What & why (each with a real master-RED)
1. **IPC response `result` type gate (W1, P2)** — `decodeEnvelope` accepted `ok:true` frames with a non-object `result` (the Request side's `params` has been gated since the protocol was written). Host consumers (`plugin_host_proxies.cpp:201`, `plugin_host_process_runtime.cpp:207/551/650`) index `result` with jsoncpp's **non-const** `operator[]`; on a non-object that is `resolveReference` — a `Json::LogicError` throw (abort when jsoncpp is built without exceptions) on threads where several of those sites have no catch. Worker frames are untrusted per the host's own contract; the enforceable seam is decode. Refused shapes now fail typed `E6002` at the boundary; null/absent/object keep the params-gate vocabulary. RED: master accepted `result:"boom"`.
2. **Capability loader fail-closed closure (W2 + R1 + R2, P2)** — catalog/relations/knowledge `reload()` are designed to *record problems and skip the bad file*, but wrong-typed `schema_version`/`id`/`family`/`kind`/`extends`/`variants[].when.param` (and non-object documents) threw past the per-file gate: the whole scan aborted, the knowledge catalog was left **silently wiped** (reload resets state before scanning), MCP degraded to generic errors, and unguarded direct callers (GUI catalog panel, copilot dispatch) die. R1 hardened the version gates to `isInt()` (int32-range safe: `3000000000`/`4294967295.0` pinned) and skipped non-object documents before the non-const id index; R2 gated the last two escape cells (`variants[].when.param` in the validator; caller-supplied `variantParams` in the query-time walk) and made the per-file problem-ownership scan match the exact `where + ": "` prefix. RED: 12 assertions across the three loaders threw on master.
3. **decision_record typed refusals (W3, P3)** — the tool read its seven string parameters with bare `asString()`; wrong-typed arguments threw out of `execute()` on every direct-dispatch path (registry callers: workflow_planner, capability_graph, tests) instead of the tool's typed `INVALID_PARAMETER` contract. Sibling tools already gated. RED: array action threw through callTool.
4. **TooLarge hexdump honesty (W4, P3)** — `IpcFrame::read` cleared `payload` before the cap check, so the channel's `"; stream head: "` diagnostic always dumped zero bytes. The 4-byte length prefix (the only real evidence — the frame body is never read) is now salvaged, before the error write so a payload/error-aliasing caller still ends up holding the E6003 message (the old test call site aliased them — caught live, de-aliased). RED: `salvaged.size() == 0`.
5. **Dead field (W5, P3)** — `OpsRunRequest::approvePendingRepair` had zero readers/writers since the #1539 token-path fix; removed (#1636-style). No aggregate-init or serialization references it (verified).
6. **Cross-track repair with receipts** — r18-a (#1663) appended a limitation to `RsSamClassifyOperator::metadata()` without regenerating the sidecar; the D8 drift gate was RED on master. `capability_knowledge_tool gen-meta` re-determines 157 sidecars with a single-file diff; `gen-pages` propagates to the knowledge page (20728 B < 32 KiB budget). Gate green ×2.

## Verification (all local; CI dead repo-wide per #1491)
Environment: Arch CachyOS x86_64, GCC 16, cmake+Unix Makefiles Debug, `QT_QPA_PLATFORM=offscreen`, global build lock `-j2` (five parallel tracks serialize), binaries run `-j1`. Full logs + recipes: `.planning/zcode-r20-03/TEST_RESULTS.md`.
- **RED** (master src + HEAD tests, same binaries): `test_exprs_ipc` 2 assertions (result accepted; salvage empty), `test_capability_knowledge` 12 assertions/3 cases (loader throws), `test_harness_evals` 1 case (tool throw). Exit 42 each.
- **GREEN ×2** (HEAD): exprs_ipc 29 cases/176 asserts; capability_knowledge 17/1637; harness_evals 19/295; agent_ops_core 47/471; surface_parity 12/5506; surface_e2e 4810+4674; r14_C_mcp_workflow_cancel; pi Node suite 39/39 (×3; one load flake re-run green).
- **Family ×2**: mcp_server (9686-assert class), task_center_executor, workflow_cancel, r19_02_workflow_cancel_order, workflow_execution_plane, workflow_recovery, agent_loop_seams, plugin_capabilities, science_context ×3, surface_protocol(_r4) — 27/27 runs RC=0.
- **plugin_host process family (R19-02 NOT_EXECUTED debt, now run)**: 6 binaries GREEN (allowlist, runtime_host, execution_barrier, manifest, exprs_plugin_system, channel_parity_r4).
- **KNOWN_BASELINE_FAIL (master==HEAD byte-identical failures, evidenced by a dedicated master-vs-HEAD build+run cycle — NOT caused by this PR)**: ① `test_surface_snapshot` registry leg — CT-6, 7 cartography tools missing `properties:{}`/`type:"object"` (track 04 red zone; failing tool named: `cartography:chart_list`; projection+wire legs green). ② plugin diagnostic-code family (5 binaries: host_process:1259 E5005 access-gate refusal not observed; boundary_r4 symlink-escape; halffail/unload_order/loader diagnostic codes) — pre-existing on master in this environment.
- NOT_EXECUTED: CI (#1491), Windows/macOS, `test_plugin_host`/`test_python_plugin_host`/`test_python_plugin_manager` (targets only exist under `SICNU_EMBED_PYTHON=ON`, off in this configure).

## Reviews (both fresh, adversarial, independent)
- **R1**: READY-WITH-FIXES → closed in `017b0a9f66`/`71a373cc78` (P1 range-safe `isInt` gates + non-object skip; P2 comment accuracy + typed-refusal pins; P3 RAII restore, null vocabulary, indent).
- **R2** (full-range + failure-matrix lens): READY-WITH-FIXES → closed in `88a637df6b`/`c757c2492f` (P1 `variants.when.param` gate + wiped-catalog fix; P2 query-time caller-param skip; P3 exact problem prefixes). Both reviews' five explicit verification items passed.

## Issue triage (ledger: ISSUE_TRIAGE.md)
Task-prompt seed issues (#1539/#1540/#1552/#1589/#1592/#1594/#1654, #1628-x, #1629-x, #1630-x, #1634/1643/1644/1645/1649/1650, #1570-1574) verified ALREADY-FIXED on live master with per-row evidence — no double-fixing. New cross-track findings reported, not fixed (out of ownership): CLI approval-surface unreachability (`--approve` no-op, no token flag — D9), unbounded planted-file reads on loader paths (D6), R19-02 R12 ClearGuard no-observable-difference analysis.

## Reproduction shortcuts
- RED: `git checkout 28e1891390 -- src/ && cmake --build build-r20-03 --target test_exprs_ipc test_capability_knowledge test_harness_evals test_agent_ops_core --parallel 2 && ./build-r20-03/test_exprs_ipc` (expect exit 42, failures at the cited lines)
- GREEN: `git checkout HEAD -- src/ && <rebuild> && ./build-r20-03/{test_exprs_ipc,tests/test_*}` ×2

**不等待 CI/CD；不自动合并。**
