# R14-E Baseline — 本科遥感实验教学、自动评测与科学证据链收敛

- **Track:** E (teaching / lab / grading / verification evidence)
- **Worktree:** `../exp-rs-r14-E-teaching-evidence` (branch `hardening/r14-E-teaching-evidence`)
- **BASE_SHA:** `f72f0c11731361488a384a085797aae64efaf7ee` == `origin/master` at start (verified via `gh api .../branches/master`)
- **Date:** 2026-10-15
- **Host note:** executed in ZCode, not Google Antigravity. `/teamwork-preview` and
  `/goal-loop` are not registered skills in this host; per the prompt's degradation
  clause the same protocol (lead/implementer/independent-reviewer roles, RED→GREEN×2,
  file ledgers) is executed with native subagent tooling. No skill availability is
  fabricated.

## Open PRs at baseline (dedupe set)

| PR | Track/Area | Overlap with E |
|----|-----------|----------------|
| #1504 | r12-b geodata durability (`src/geospatial,src/data,src/dataset,src/experiment`) | `src/experiment/**` FORBIDDEN for E; read-only |
| #1505 | r13-j security/concurrency | check diff before touching shared seams |
| #1506 | r14-A science processing (`src/operators/rs`) | none (A-owned) |
| #1507 | r14-C pi/mcp workflow | none (C-owned) |
| #1508 | r14-B model runtime | none (B-owned) |
| #1509 | r13-k JSON type safety | owns `src/verification/canonical_json.cpp`, `src/science_context/agent_adapter.cpp`, `src/science_context/capability_router.cpp` → E must NOT touch those files |

## Issue triage vs master code (verified by reading code, not issue state)

| Issue | Verdict (code evidence) |
|-------|------------------------|
| #1466 (teaching/suitability cluster, 5 items) | **ALREADY_FIXED_ON_MASTER** — `boundedIntegral` fail-closed (suitability_goal.cpp:55), Windows python probing (script_adapters.cpp:89), bounded stdout capture w/ truncated flags (script_adapters.cpp:173-205), typed discovery skips (batch_assessment.cpp recordSkip + skipped_files rows), sidecar atomic session write (lab_session_state.cpp:202) |
| #1464 (grader stamps / faultlab inert params) | **ALREADY_FIXED_ON_MASTER** — SequencingStamp instant compare (grader_matcher.cpp:53-145, 256-281; unparseable → Unprovable by design for legacy docs), `fault_unsupported_params` typed rejection (fault_transforms.cpp:52-67), cell_size consumed (fault_transforms.cpp:380-395) |
| #1492 (studio thread barrier + QPointer) | **ALREADY_FIXED_ON_MASTER** (PR #1501 commit 1aa90c039d) — try/catch barrier, `shared_ptr<atomic<bool>> m_aliveFlag`, queued invoke, detached reaper (experiment_studio_dock.cpp:593-680) |
| #1465 item broker cache key | **ALREADY_FIXED_ON_MASTER** — failureDigest in CacheKeyMaterial (broker.cpp:244-261) |
| #1465 item std::equal OOB ×4 | **ALREADY_FIXED_ON_MASTER** — length gate in all 4 matchers (asset_state_json.cpp) |
| #1465 item agent_adapter static init | **OWNED BY OPEN PR #1509** — do not touch |
| #1465 item terminology_provider | **OUT_OF_E_OWNERSHIP** (`src/help/**` not in E write list) |
| #1488 / #1447 (canonical json types) | master has isUInt64 path; open PR #1509 adds exception barrier → **LEAVE TO #1509** |
| #1458 (portable bundle P1; studio gate) | bundle part lives in `src/experiment/**` (#1504 owner) — E does not touch; studio-gate part: re-verify on master below |

## Unresolved review threads (merged PRs) in E scope

- **#1482 → src/app/experiment_studio/experiment_studio_dock.cpp — [P2] "Avoid per-cell
  allocations in the large-table path"**: 10k-row report still creates 6 QTableWidgetItem
  per row (60k heap objects); reviewer asks for model-backed table or bounded/lazy
  population. **OPEN at audit time → later ADDRESSED_BY_OPEN_PR #1510** (bounding arm);
  this track's model-backed implementation was dropped at push time per dedupe (LEDGER).

## Audit plan

16 WPs from the R14-E prompt; each audited on master code with dedupe against the
table above. Findings and oracles in ORACLES.md / LEDGER.md as work proceeds.
