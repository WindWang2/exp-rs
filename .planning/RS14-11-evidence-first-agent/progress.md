# RS14-11 Evidence-first Agent Loop — Progress

## Round log

### Phase 0 — Recon & dynamic dedup (done)
- Baseline re-fetched: `origin/master` @ `4f6632e1f6`, no open PRs; sibling RS14
  worktrees still at baseline (inspector-ui has unmerged ADR 0172 — this track takes 0173).
- Full architecture recon of mission runtime / workflow engine / TaskCenter /
  harness (planner, preflight, repair, verifier, diagnoser, evidence, ledger,
  session store). See `recon.md` for the capability map and dedup matrix.
- Key gap confirmed: no orchestration layer owns the loop, the budget, or the
  audit trail; the mission layer records runs but never launches them (out of scope).

### Phase 1/2 — Plan & slices (done)
- `plan.md`: pure C++20 core (`sicnu_agent_loop`) + Qt-capable adapter in
  `src/agent/tools`; seams, DecisionRecord, journal, budgets, modes, DoD.
- `slices.md`: A–G with RED→GREEN granularity.

### Slice A — state machine + decision records + journal (GREEN)
- `src/agent_loop/session_state.*`: closed stage vocabulary (10 stages), 3 terminal
  states, typed transition table (35 edges), replan counting, absorbing terminal.
- `src/agent_loop/decision_record.*`: versioned DecisionRecord
  (inputs/alternatives/selected/reason/evidence/policy), fail-closed reader.
- `src/agent_loop/session_journal.*`: append-only bounded journal, atomic persist
  (temp+rename), fail-closed reader, deterministic compaction projection,
  replay without seams.
- `tests/test_agent_loop_core.cpp`: 15 cases / 208 assertions, pure C++ (no Qt/GDAL).
- Bugs caught during GREEN (both test-visible):
  1. `advance()` into a terminal state did not set the terminal marker.
  2. `load()` called `buffer.str()` twice across the parse range — UB across two
     temporaries; caught by the 14-entry reload test.
  3. Fail-closed reader rejected records without `decision_id` (test factory bug,
     reader correct).

### Environment notes
- Build: `cmake --preset dev-default` in `exp-rs-wt-rs14-agent-loop/build-dev`
  (Unix Makefiles, Debug). Single-target builds only, `-j2` max.
- ctest discovery uses Catch2 case names (no TEST_PREFIX): select with
  `ctest -R "<case name substring>"`.
