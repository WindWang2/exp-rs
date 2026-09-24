# Ledger — completion/scientific-planner-recovery (worktree 11/15)

## Track identity

- Branch: `completion/scientific-planner-recovery`, worktree `../exp-rs-completion-planner-recovery`
- Baseline at branch time: `origin/master` = `e4904cd3c568e1e730396237ec7034dc9215b272` (= recon seed, re-fetched 2026-09-23)
- Recovery target: the implementation of PR #1193 (RS14-09 Scientific Task Planner)

## Gap evidence (current master, re-verified at execution time)

1. `git ls-tree -r origin/master --name-only | grep src/planner` → **empty** (no `src/planner` module, no `sicnu_planner` target).
2. No `test_scientific_planner_*` on master; no `data/planner/`.
3. PR #1193 state = MERGED (2026-09-22T09:02:53Z) but merge commit `89e31a901`
   is **docs-only** ("phase 0-2"): 4 `.planning/RS14-09-scientific-planner/*.md`
   files, 354 insertions. The implementation slices A0/A–G/R described in the PR
   body (src/planner, 9 test executables, 403 assertions, golden scenarios)
   **never landed**. PR head branch `agent/rs14-scientific-task-planner` is not
   among remote branches (deleted), so recovery must re-implement from the
   committed design docs — full-branch cherry-pick is impossible AND forbidden.
4. Master still EXPECTS the planner: `docs/integration.md` §3 is written from
   the passport side toward "the planner" ("Planner 在生成 ScientificPlan 前…");
   RS14-06's integration section names "RS14-09 Scientific Task Planner
   (PR #1193)" as a future consumer of `AgentDiagnostic`. No open PR restores it
   (open PR count = 0 at execution time).
5. No competing implementation: repo-wide namespace `sicnu::planner` is unused;
   `src/repair_planner` (RS14-03 repair plans) and `src/science_context`
   (`sicnu::science_context::PlanningContext` — different namespace, different
   concept: a projection of the science-context bundle) are distinct.

## Historical scope being recovered (from committed design docs + PR body)

Only what the historical docs explicitly scope (`.planning/RS14-09-scientific-planner/{plan,slices}.md`, PR #1193 body):

- Versioned fail-closed docs: `scientific_goal/1.0`, `planning_context/1.0`, `scientific_plan/1.0`; canonical key-sorted JSON; SHA-256/16 fingerprints.
- Deterministic rule/graph baseline `planScientificWork`: goal kind → staged DAG (import→preprocess→analyze→verify→publish), numeric-domain gating inserts calibration bridges or typed questions, grid alignment, acceptance criteria → verifier targets, every unmet need = typed question.
- Constraints/budgets: forbidden operators, required determinism, allowed families, maxSteps/maxRam/maxCostClass → risks + blocking decisions, plan stays fully visible.
- Multiple ranked candidates with why/whyNot + candidateIndex.
- Proposal validation seam: untrusted external plan JSON → deterministic validator, closed sorted `planner:proposal_*` rejection codes; identity re-minted from content.
- Teaching projection: hidden-answer view (masks student-decision params in teaching+guided/minimal only, honest `masking_applied`), explanation view (per-step rationale, transition whys, thinking questions). Never does the experiment for the student.
- WorkflowIR 1.0-shaped projection with explicit contracts-domain→artifact_facts map (fail-closed, honest unknown + warnings).
- 4 golden scenarios, offline; regenerate only via explicit env opt-in, never on CI.
- Never executes operators; no dataset I/O; no TaskCenter; no Qt.

## Oracle set

| id | Oracle | Evidence |
|---|---|---|
| O1 | Module exists & complete per recovered scope | `src/planner/` 12 files; `git grep sicnu::planner` |
| O2 | Per-capability current-master RED/structural-gap | This ledger §Gap evidence + per-slice RED notes below |
| O3 | Narrow tests green, narrow builds only | `ctest -R "test_scientific_planner" -j1` on planner-only targets |
| O4 | Provider/authority wiring: fake + real-shaped tests | fake provider in tests; real-shaped fixtures use real `rs:` ids gated by `findScientificContract` |
| O5 | No placeholder-as-success | typed questions/verdicts/rejection codes pinned by negative tests |
| O6 | No second truth source | transitions gated by `sicnu::contracts::findScientificContract`; drift tests pin mirrors (asset lifecycle, artifact_facts map coverage, intent vocab) |
| O7 | Mutation/adversarial oracle kills wrong impls | mutation probes in review test (calibration-gate mutation, masking leak mutation, proposal fail-open mutation) |
| O8 | Determinism | same-input replay byte-identical; fingerprint stable — pinned in tests + goldens |
| O9 | Review P0/P1/P2 = 0 | independent reviewer verdict line |
| O10 | Key oracle run twice consecutively | final `ctest -R "test_scientific_planner" -j1` ×2 transcripts |

Build discipline: one CMake configure for the worktree; only narrow targets
(`sicnu_planner` + 9-10 test executables); `ctest -R` filtered; no full build,
no clean rebuild. Full-app link is NOT in this track's DoD (integration-level
composition is Prompt 16's job); PR will state this honestly.

## Round log

| round | change | oracle | result |
|---|---|---|---|
| 0 | recon + worktree + this ledger | O2 | gap proven (§Gap evidence) |
| 1 | slice A0: skeleton CMake + closed vocabularies + SHA-256 (kK table copied verbatim from the FIPS-tested src/preflight implementation after a 2-constant transcription bug was killed by the FIPS vectors) | O3 | vocab+sha256 green |
| 2 | slice A: goal/context/plan schema, canonical JSON, fingerprint (plan_id excluded), fail-closed readers, bounds | O3/O5/O8 | schema tests green |
| 3 | slice B: rule table + planScientificWork baseline (staged spine, contracts-verified bridges, grid gate, verifier targets, typed questions, ranked candidates) | O3/O4/O5/O6/O8 | core tests green (real rs: ids gated by contracts) |
| 4 | slice C: constraints/budgets (forbidden/determinism/families narrowing; step/RAM/cost-class overruns annotated, plan stays visible). Budget questions are candidate-local by design; narrowing is silent while a lawful sibling exists | O3/O5 | constraints tests green |
| 5 | slice D: teaching views (honest masking, explanation rationale/whys/thinking questions) | O3/O8 | teaching tests green |
| 6 | slice E: proposal validator (closed sorted planner:proposal_* codes; identity re-minted; verdict consistency carries its own code, split from structural check) | O3/O5/O7 | proposal tests green |
| 7 | slice F: workflow_ir-shaped projection (explicit fully-covering contracts→artifact_facts map, honest unknown+warnings, state-less steps refused) | O3/O5/O6 | ir tests green |
| 8 | slice G: 4 offline golden scenarios, env-var-opt-in regeneration, byte-stable replay | O3/O8 | golden green in update+verify modes |
| 9 | drift + review tests: mirrors pinned to scientific_state lifecycle / contracts domains / harness intent data; 4 mutation oracles; hostile-shape adversarial cases; zero-ready-asset boundary answered with a typed blocking question | O2/O5/O6/O7 | drift+review green |
| 10 | FULL NARROW SUITE | O3 | `ctest -R test_scientific_planner -j1` → 59/59 passed |
| 11 | commit slices A0-G+R (11 conventional commits) | — | clean tree, 11 commits |
| 12 | independent adversarial review (subagent, out-of-tree probes) | O9 | BLOCKED: 3 P0 + 3 P1 + 3 P2 (producer/reader gap, stage-wide constraints, fail-closed seams, sibling verdict stain, contract-less operators, mirror honesty) |
| 13 | remediation commit 4a22095d1 + regression oracles for every finding | O7/O9 | suite 67/67 green; sent for re-review |
| 14 | PR 前同步: master still e4904cd3c (no rebase needed); parallel open PRs #1277-1279 touch grader/repair_planner/preflight — zero directory overlap with src/planner; shared files are append-only unions (tests/CMakeLists.txt tail) | — | dedupe clean |
| 15 | re-review of residue fixes (commit 0f829189e): boundedAssetRefs unifies all five per-asset loops (R1-R4 probes re-verified); drift pin mutation-proven (kModalityDem rename kills the suite) | O9 | **VERDICT: READY/PROCEED** — 0 P0/P1/P2 open |
| 16 | O10 key oracle run TWICE consecutively | O10 | run 1: 67/67 passed; run 2: 67/67 passed |

Resource discipline note: one configure; only `sicnu_planner` + 10 test
executables built (plus their existing deps: Catch2, jsoncpp, contracts,
scientific_state objects); no full-tree build was requested (one accidental
targetless ninja invocation was killed early and superseded by narrow-target
builds; it is not cited as evidence).

## Per-slice RED notes

- All slices: the structural RED is current master's missing module (§Gap 1-3);
  each slice additionally has behavior RED notes recorded when its test is
  written (test committed in the same slice, red-first per goal-loop).
