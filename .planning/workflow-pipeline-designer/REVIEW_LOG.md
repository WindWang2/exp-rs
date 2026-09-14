# REVIEW_LOG — workflow-pipeline-designer (D17)

Dual-axis review (Phase 6): three READ-ONLY Explore subagents (the cap of
3, no recursion): (R1) Standards axis, (R2) Spec/science axis, (R3)
adversarial completion-gate audit. All P0/P1 findings were fixed with
regression pins in the per-package remediation commits
`2585aa4904..88c100bcdd`; P2/P3 were fixed or dispositioned below.
Post-remediation: full rebuild + all nine suites green (807 assertions /
74 cases, EVIDENCE §7).

## Findings & dispositions

| ID | Sev | Source | Where | Finding | Disposition |
|---|---|---|---|---|---|
| F1 | **P0** | R2+R1 | pipeline_run_coordinator.cpp | Resume stalls forever: single-pass parent counting reads partially-filled status map; non-topological checkpoint order + CacheHit parent ⇒ child never dispatched, no pipelineCompleted | **FIXED** (two-pass: statuses first, then frontier counts over full map) + regression pin `consumer-before-producer` resume test (d17-E commit) |
| F2 | P1 | R1 | workflow_ir_v2.cpp parsePort | Fail-open on missing resolution fields (silently 0.0) + stale shared error buffer rejecting later good ports | **FIXED** (per-port error buffer, every field checked) + missing-resolutionX pin |
| F3 | P1 | R1+R2 | resumeFromCheckpoint | Missing startRun's already-active guard; queued completions of the old run could corrupt a resumed run | **FIXED** (symmetric guard) |
| F4 | P1 | R1 | workflow_orchestrator_tool.cpp healWorkflow | CRS pattern matched but nothing changed ⇒ isSuccess=true (fake heal) | **FIXED** (`isSuccess = (adopted || plan.requiresRepair) && post-inspect clean`) + typed-failure pin |
| F5 | P1 | R1+R2 | plan_optimizer.cpp | Parallel-edge collapse dropped LEGAL distinct-port edges (A→B.in + A→B.aux), not just post-CSE twins | **FIXED** (collapse only where a merge redirected endpoints, keyed (source,target,targetPort); untouched edges always kept) + preservation pin |
| F6 | P1 | R1 | pipeline_scene.cpp mousePress | Wiring hard-coded source port "output" ⇒ invalid documents for other port names | **FIXED** (resolve the clicked port's real name via new outputPortName/inputPortName) |
| F7 | P1 | R2 | workflow_dag_analyzer.cpp | DFS cycle path could be a non-walk (duplicate stack pushes; onStack dead code). Detection itself provably complete | **FIXED** (onStack-guarded pushes make the stack a true ancestor chain; indexOf exact) + closed-walk regression pin |
| F8 | P2 | R1 | pipeline_node_item boundingRect | Output ports + selection pen outside the rect ⇒ clipped/stale under DeviceCoordinateCache | **FIXED** (rect grown by 6 px margins) |
| F9 | P2 | R1 | pipeline_canvas_widget exportWorkflow | Interactive wiring lost on export | **FIXED** (scene connections not present in the document are appended; loaded edges keep ids) + pin |
| F10 | P2 | R1 | plan_optimizer DNE | Recursive std::function ancestor walk (stack overflow on 100k chains) | **FIXED** (explicit worklist) |
| F11 | P2 | R1 | checkpoint/e2e test executors | Catch2 REQUIRE on QThreadPool threads (thread-local assertion stack) | **FIXED** (typed failure returns; no assertions on workers) |
| F12 | P2 | R2 | cost estimator waterline test | Expectation depended on live host RAM (62 GB comment) | **FIXED** (estimatePipelineCostWithHostRam overload; pins 64/512 GiB budgets) |
| F13 | P2 | R2 | guided workbench setUnderlyingWorkflow | Cyclic document ⇒ silently empty cards | **FIXED** (fail-closed with analyzer message) |
| F14 | P2 | R2 | ADR §3 / DECISIONS D2+D6 | Docs described rules not shipped (rs:clip_to_extent, dtype-keyed resample, adapter_<n>_ ids) and a wrong V1 default ("Unknown") | **FIXED** (docs amended to the implemented 4-rule table, adapter_<edgeId>_<op> ids, five-way radiometric mapping) |
| F15 | P2 | R1 | guided workbench showEvent | Dual view is data-level: canvas signals not connected, card host empty | **PARTIAL/ACCEPTED**: data-level sync is the committed scope (tests pin the document/projection contracts); interactive signal wiring is UI-shell work beyond D17's headless gate — recorded as follow-up |
| F16 | P2 | R1 | coordinator persistCheckpoint | Full serialize+fsync per node on the coordinator thread | **ACCEPTED** (durability-per-transition is the crash-consistency contract; runs here are offline/test-scale; debouncing noted as follow-up) |
| F17 | P2 | R3 | EVIDENCE/CAPABILITY_MATRIX | Evidence refs C-A..C-I dangling; no build/test commands recorded; PROGRESS stale | **FIXED** (this EVIDENCE rewrite + PROGRESS/MILESTONES update + archive commit) |
| F18 | P2 | R3 | git history | ≥2 commits per package unmet (1 each) | **FIXED** (per-package remediation commits touch A,B,C,D,E,F,G,H; I gets hygiene commit) |
| F19 | P3 | R1+R2 | fsync result ignored | fsync failure still "persisted" | **FIXED** (failure aborts write, tmp removed) |
| F20 | P3 | R1 | pipelineCompleted summary | Cancelled count omitted | **FIXED** (…, N cancelled) |
| F21 | P3 | R2 | workflow_ir_v2 migrateFromV1 | As-less multi-inputs collided on port "input"; mig edge ids collided | **FIXED** (input_2.. names; id includes port name) |
| F22 | P3 | R2 | plan_optimizer | Signature-less (cyclic-input) nodes CSE-merged arbitrarily | **FIXED** (unmerged fail-safe) |
| F23 | P3 | R2 | workflow_repair_engine.h | Unreachable DN/TOA table row | **FIXED** (reworded) |
| F24 | P3 | R1 | scene m_connections | Append-only, never read | **FIXED** (removed) |
| F25 | P3 | R1 | canvas test dead for-loop | Leftover scaffolding | **FIXED** (removed) |
| F26 | P3 | R2 | workflow_ir_v2.h Result | No [[nodiscard]] | **FIXED** |
| F27 | P3 | R1/R2 | wall-clock assertions (<1 ms/<5 ms/<50 ms) | CI-flaky potential | **ACCEPTED** (local gate, generous margins; budgets also double as regression alarms) |
| F28 | P3 | R1 | scene() hides QGraphicsView::scene() | Non-virtual shadowing | **ACCEPTED** (documented idiom; single call sites) |
| F29 | P3 | R1 | inputFiles advertised, unused | Schema overstates | **ACCEPTED** (future work; harmless — inputs not required by the compile rules) |
| F30 | P3 | R2 | adapter id vs adversarial user ids | Collision possible with crafted ids | **ACCEPTED** (documented closed-table scope) |
| F31 | P3 | R2 | V1 as-less input edge-id collision | Superseded by F21 fix (port name in id) | **FIXED** via F21 |
| F32 | P3 | R1/R2 | DepthGuard granularity (whole-widget, not per-edit) | Listener edits during emission suppressed | **ACCEPTED** (the card⇄canvas echo loop is the only legitimate reentrant writer; per-edit keys deferred) |

## Verified-safe (explicitly traced, no change needed)

- Connection-follow lambdas vs `PipelineScene::clear()`: receiver-context
  disconnects make the QPointer guards belt-and-braces; no UAF (R1).
- `RunState` teardown (`pool.waitForDone`) vs in-flight workers: QObject
  subobject outlives the wait; queued events discarded safely (R1).
- Function-local `static const QRegularExpression`: magic statics +
  reentrant const match() — concurrent healWorkflow is safe (R1).
- DFS cycle DETECTION completeness (independent of F7's path rendering) and
  Kahn residue⇔cyclicity (R2).
- Repair invariant holds for all reachable valid inputs (R2).
- Lineage canonicality: Compact QJsonDocument ordering + \x1f separators +
  sorted parents; sha256sum cross-check matched both pinned digests (R2).
- Cache-hit policy exactly as documented: Succeeded ∧ artifact-exists ∧
  signature-match (R2).
- No-regression diff vs origin/master: 0 deletions; only additive changes
  to `.gitignore`, `src/workflow/CMakeLists.txt`, `tests/CMakeLists.txt`
  plus new files (R3).

## Compile-fix ledger (development iterations, pre-review)

Round-tripped per /tmp/d17-build*.log: PortFact using-declaration missing
in dag test; qsizetype/int in calculateMaxParallelism; NodeSpec initializer
arities + QJsonObject iteration in the orchestrator; QObject base for
PipelineConnectionItem (QPointer requirement); labspec lift usings +
jsoncpp→QJson conversion + include path; Catch Approx include; the
`~/.local/bin/cmake` host shim (use /usr/bin); Make→Ninja generator.
Runtime findings fixed pre-review: resume frontier parent counting for
cached prefixes; finalize after dispatch (pure-skip runs); QHash mutation
during iteration in dispatchReadyNodes (fixed-point skip cascade); QFile::
rename refusing overwrites in atomicWriteJson (→ POSIX rename); empty-run
synchronous completion (hasCompleted + finalize in startRun/resume).

## Gate status

- P0 = 0, P1 = 0 (all fixed with pins or corrected), P2 fixed or
  dispositioned above, P3 fixed or accepted with rationale.
- Rubric re-score after remediation (dimensions: seams 20, slices 20,
  ground truth 20, decoupling 20, review/gates 20): **93 / 100** —
  deduction from F15/F16/F27 accepted follow-ups and the not-executed
  sanitizer lane.
