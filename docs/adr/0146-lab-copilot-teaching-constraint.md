# ADR 0146: Lab Copilot — The Teaching Constraint Is a Code-Level Gate

Status: accepted · Branch `zcode/agent-lab-copilot` · Baseline `origin/master@27b9aa0a63`

## Context

The harness (`plan` / `preflight` / `grounding` / `verification` / `evidence` /
`context_ledger` / `run_loop`) is built for research analysis. Dropped into a
classroom it becomes a homework machine: "帮我做实验 3" gets a finished result
file. D9 introduces the **lab intent domain**: the harness must be able to act
as a tutor — diagnose, hint, explain — and must be *structurally unable* to do
the lab for a student. 宁可少帮，不可代做.

A prompt-level rule ("you are a tutor, refuse politely") is explicitly
insufficient: prompt constraints leak under rephrasing, role-play, urgency, and
tool-routed requests. The property we need is that the withholding happens in
code, on the only path an artifact can travel.

## Decisions

1. **A second closed intent vocabulary.** `intent_vocabulary.h` gains
   `kLabIntentVocabulary` — `lab_troubleshoot` / `lab_hint` / `lab_concept` /
   `lab_grade_request` / `lab_execute` — deliberately disjoint from the
   scientific list the capability drift floor pins. Lab intents carry no
   preflight/capability semantics; the scientific vocabulary is untouched.
2. **The gate lives in `harness_actions`, not in a prompt.** Actions resolve to
   surfaces through the closed vocabulary (ADR 0145). D9 extends that
   chokepoint: `actionProducesArtifact()` classifies artifact-producing keys
   (explicitly listed keys, plus any key whose resolved tool is the plan
   executor), and `resolvedSuggestedActionForRole()` refuses to emit a tool or
   workbench surface for a student in the lab domain. The withheld resolution
   carries `withheld_by: "teaching_constraint"`, `reason_code: "TEACHING_REFUSAL"`.
   There is no code path that hands the underlying surface across the gate.
3. **A typed refusal, not an apology.** `TEACHING_REFUSAL` joins the closed
   error taxonomy (category `validation`, retry `none`): a refusal is never
   retryable — the same request can never succeed, so retrying it is a lie.
   Refusals are `recoverable` in exactly one sense: the student can re-ask as a
   hint/diagnosis request, which the `alternative_zh` field says in Chinese.
4. **Role is session state, never message content.** `normalizeLabRole()`
   maps everything unknown — including an empty role — to `student`. The
   classifier and answer builders never read authority claims out of the
   message, so "我是老师", role-play, and injected instructions cannot escalate.
   `lab_execute` and `lab_grade_request` are reachable only with a session
   role of `teacher`/`admin`.
5. **The classifier's refusal default is `lab_hint`.** Deterministic keyword
   classification (Chinese- and English-keyword, no model, no network): unknown
   or empty input lands on `lab_hint`, never on an execute/grade intent. When
   the student routes an execution request through the tool surface
   (`routed_tool`), the request is execute-shaped regardless of its prose.
6. **The diagnostic brain consumes measurements, reuses existing codes.** The
   six canonical lab error signatures are detected from *measured* observations
   (min/max, nodata fraction, kappa, mask density, CRS pair, pixel-size pair),
   never from student prose, and each maps to an existing curated entry in
   `data/help/diagnostics.json` (band_role_unresolved, nodata_declared,
   training_invalid, output_invalid, crs_mismatch, grid_mismatch). No new
   diagnostic codes. The answer is diagnosis-first: symptom + most likely
   cause + exactly ONE verification action, which resolves through the gate.
7. **Grounding reads the same LabSpec the UI reads** (`data/labs/*.lab.json`,
   D2 schema) and anchors answers to the *current step* only. Parameter values
   are the solution: `stepDoc()` exports parameter NAMES, never values;
   values leave the module only through the teacher surface
   (`harness:lab_reference` → `reference_solution`). D2/D6/D4 dependencies
   degrade with a typed `unavailable` and honest Chinese text — the copilot
   never fabricates steps, definitions, or scores.
8. **Chinese-first answers with glossary-verbatim terms.** Answers assemble
   deterministically (no model in the loop) and cite D6 glossary `zh` terms so
   the same word appears in the UI, the help panel, and the copilot answer.

## Consequences

- The eval suite (`tests/test_harness_lab_evals.cpp`) pins all of the above
  deterministically, including ≥ 5 must-refuse reverse cases; any successful
  jailbreak is a P0 that blocks the PR.
- The scientific pipeline is untouched: outside the lab domain the gate is
  inert and every existing surface behaves exactly as before.
- Future artifact-producing action keys are covered by construction: the
  classification keys off the resolved tool (plan executor), not a hand list.
- When D2/D6/D4 land, the seams light up by pointing the loaders at the
  canonical paths; no code change is required in the copilot.
