# Agent Operations & Recovery Control Center — Recon

Track: Agent Operations & Recovery Control Center
Branch: `feat/agent-ops-control-center`
Baseline: `origin/master` @ `a9dc33fa` (PR #1236 merged)
Worktree: `/workspace/exp-rs-agent-ops`

Parallel OPEN — **never touch**:
| PR | Paths |
|----|-------|
| #1237 | `src/teaching/**`, `src/app/teaching/**` |
| #1238 | `src/experiment_studio/**`, `src/app/experiment_studio/**` |
| #1239 | `src/teaching_admin/**`, `src/app/teaching_admin/**` |
| #1240 | `src/science_context/**` — optional adapter only; must run on master inputs |

Owns: `src/agent_ops/**`, `src/app/agent_ops/**`, `tests/test_agent_ops_*`,
`docs/development/agent-ops-control-center/**`.

## 1. Call chains on tip (authorities to COMPOSE, not reimplement)

### 1.1 `src/agent_loop/` (RS14-11) — session spine
- `ScientificAgentSession`: Goal → snapshot → plan → preflight → (repair approval)
  → execute → verify → (diagnose → replan)* → delivery.
- `SessionJournal`: append-only, bounded, atomic save (temp+rename), fail-closed
  load, compact projection, `replay()`.
- `SessionPolicy`: mode, maxReplans, noProgressThreshold, resourceBudgetMb,
  maxSteps, executorTimeoutMs, repairApproval, teaching gate.
- `DecisionRecord`: typed audit unit (inputs / alternatives / selected / reason /
  evidence / policy).
- `fake_seams.h`: declarative `FakeScenario` doubles for offline E2E.
- `resume()`: adopt non-terminal journal; no re-execution of completed work.
- **Seam interfaces** (`session_seams.h`): IDataStateProvider, IPlanner,
  IPreflight, IExecutor, IVerifier, IDiagnoser — production adapter referenced
  as `src/agent/tools/agent_session_adapter.*` but **that adapter file is absent
  on tip** (designed, unwired).

### 1.2 `src/agentbench/` (RS14-14) — trace & suite
- Trace schema `sicnu.agentbench.trace/v1` (`AgentTrace`, `TraceStep`,
  `TraceEvidence`, fault markers via `payload.fault`).
- Suite/report: `sicnu.agentbench.suite_report/v1`; pure evaluation; no Qt.
- Integration.md §1–2: live recorder + ExperimentStore persistence are
  **designed wiring points, not built**.

### 1.3 `src/repair_planner/` (RS14-03)
- Schema + fingerprint + JSON IO only (`RepairPlan`, `RepairAction`).
- **No planner entry function on tip** — recovery bridge may *project*
  diagnosis proposals into `RepairPlan` documents when present; never invents
  operators.

### 1.4 Verifier / preflight / debugger / capsule / autonomy
| Authority | Location | Role for ops |
|-----------|----------|--------------|
| Preflight | harness `preflightIntent` + agent_loop IPreflight | ok/fixable/blocked |
| Verifier | harness `verifyArtifact` + agent_loop IVerifier | PASS / WARN / FAIL |
| Diagnoser | `harness:diagnose_run` + IDiagnoser | typed root cause + proposals |
| Debugger | `experiment/debugger` `AgentDiagnostic` (exp.diag.v1) | optional evidence |
| Capsule | `experiment/capsule` | export projection target |
| Autonomy | `agent/autonomy` decideAutonomy | gate mutating ops |
| Mission timeline | `app/workbench/mission_timeline_*` | visual language for UI |
| ExperimentStore | `saveBenchmarkResult` (D19) | Qt persistence sink |
| MCP/Pi | agent surfaces / `pi/` | drivers of same spine (extend, don't fork) |

### 1.5 Evidence summary already exists
`EvidenceSummary` / `SessionResult` is the session deliverable. Ops **extends**
it into `FinalDelivery` (goal, plan, outputs, verifier, warnings, questions,
provenance, run ids, capsule ref, benchmark refs, claims+confidence) without
replacing the journal as truth.

## 2. Implemented-but-unwired seams (primary gaps)

1. **`sicnu_agent_loop` / `sicnu_repair_planner` not in root CMakeLists** —
   libraries and tests exist; `add_subdirectory` missing → tips cannot link.
2. **Live session → AgentTrace projector** — integration.md promises it; absent.
3. **Suite report → ExperimentStore adapter** — Qt-side, absent.
4. **Production diagnoser enrichment** — harness diagnose_run exists; no
   structured ops diagnostic merging verifier/preflight/debugger/provenance
   with repairability/retryability (LLM text must not be sole root cause).
5. **Recovery/replan bridge with autonomy + budgets** — loop has replan
   bounds; no first-class retry/repair/replan/ask/abort decision object gated
   by autonomy for ops mutating actions.
6. **Ops Control Center UI** — MissionTimeline panel exists; no agent-session
   control surface (pause/cancel/resume/approve repair/export) over the loop.
7. **agent_session_adapter** — referenced by agent_loop docs; file missing.
8. **MCP `scientific:agent_session` / session surface** — not registered as
   ops control; Pi bridge stays a driver of the same spine.
9. **#1240 science_context** — must not hard-depend; optional adapter later.

## 3. Architecture chosen (compose only)

```
MCP / Pi / Control Center UI
            │
            ▼
   OperationsCoordinator  (pause/cancel/resume, budgets, autonomy)
            │
   ┌────────┼────────┬──────────────┬────────────────┐
   ▼        ▼        ▼              ▼                ▼
 AgentLoop  Live     Diagnostic   Recovery         Delivery
 session    Recorder Bridge       Bridge           Assembler
   │           │         │            │                │
   ▼           ▼         ▼            ▼                ▼
 Journal → Trace v1   OpDiagnostic  RepairPlan?   FinalDelivery
              │         │            + autonomy        │
              ▼         └────────────► replan/ask      ▼
         AgentBench              abort/retry      Capsule export
         (+ Qt store adapter)                     OpsProjection
```

- Core library `sicnu_agent_ops`: Qt-free, links `sicnu_agent_loop` +
  `sicnu_agentbench` + `sicnu_autonomy` + `sicnu_repair_planner`.
- Qt-side benchmark store writer + Control Center panel under
  `src/app/agent_ops/**` (append-only app cmake; no teaching/studio fences).
- Does **not** reimplement Planner/Loop/Verifier/Debugger/Benchmark.

## 4. Shared hotspot discipline
Append-only semantic union on: root `CMakeLists.txt`, `tests/CMakeLists.txt`,
`src/app/CMakeLists.txt`. No edits under parallel fences #1237–#1240.

## 5. Build constraints
Own `build/`; Qt `/workspace/Qt/6.8.0/gcc_64`; `-j1`/`-j2`; no full clean.
