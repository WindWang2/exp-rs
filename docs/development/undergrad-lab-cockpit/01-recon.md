# Phase 0 — Recon: Undergraduate Lab Cockpit

Baseline: `origin/master` @ `a9dc33fa` (2026-09-22 Asia/Shanghai).
Open PRs at start: none (re-checked before ship).

## Goal

Productize existing teaching foundations into a student-facing **Lab Cockpit**
UI + Qt-Core/jsoncpp **projection layer**. Projection only — no second truth
for curriculum / lab / recipe / verifier / grader / explain / autonomy / capsule.

## Dependency graph (what we project)

| Layer | Path | Authority | Merged? | Wired to student UI? |
|-------|------|-----------|---------|----------------------|
| Curriculum manifest | `data/curriculum/undergraduate_rs.curriculum.json` (`sicnu.curriculum/1`) | Teaching org over labs | Yes | **No** — no Course Home |
| Lab registry | `data/labs/lab-registry.json` | Lab id / pack / grading paths | Yes | Indirect via Guided Workflow list |
| LabSpecs | `data/labs/*.lab.json`, `*.labspec.json` | Steps, data, grading refs | Yes | Partial — `GuidedWorkflowWidget` |
| Curriculum harness | `src/agent/harness/curriculum_{catalog,progress,availability,registry_probe}.*` | Load/validate/progress/availability | Yes in `sicnu_agent` | **No UI**; `tests/test_curriculum.cpp` **exists but unwired in CMake** |
| ScientificRecipe | `src/recipes/*` | Compiled skill from lab | Sources present | **No CMakeLists** — compile/registry unwired as a leaf |
| Explain / Why-step | `src/explain/*` (`sicnu_explain`) | StepExplanation + ViewModel + badges | Yes | No cockpit panel; scientific workbench only has `sci_inspection` |
| Grader | `src/grader/*` (`sicnu_grader`) | Rubric/evidence/report | Yes | No student feedback panel |
| Verifier | `src/verify/*` (`sicnu_verifier`) | Pass/Fail/Indeterminate lattice | Sources + CMakeLists | **`add_subdirectory(src/verify)` missing** from root |
| Autonomy L0–L5 | `src/agent/autonomy/*` (`sicnu_autonomy`) | Policy + decide + status projection | Yes | No ladder viz in teaching UI |
| Capsule | `src/experiment/capsule/*` | Reproducibility export/readiness | Yes | No student submit hook in cockpit |
| Lab runtime session | `src/lab/session_{state,store}.*` (`sicnu_lab_runtime`) | `sicnu.lab-session/1` stage/checkpoint | Yes | Not course-scoped; different from cockpit nav session |
| Guided Workflow UI | `src/app/widgets/guided_workflow_widget.*` + `lab_spec_loader.*` | Lab list + step runner | Yes, docked | Exists but **no curriculum modules/prereqs/hours/goals/packs/completion** |
| Scientific workbench | `src/app/workbench/scientific/sci_inspection.*` | Sci facts inspection | Yes | Inspector-adjacent; not course home |
| Dock / commands | `main_window_docks.cpp`, `command_defs.cpp` | Shell registration | Yes | Append-only seam for cockpit |

## Student questions → existing seams

| Student need | Seam to project |
|--------------|-----------------|
| Chapter/lab progress | `CurriculumProgress::summary` + catalog module order |
| Learning goals | module `learning_outcomes` + lab `objective(_zh)` |
| Data readiness | `buildAvailabilityReport` packs + LabSpec `prerequisites` + passport/inspector hooks (injected) |
| Next step + why | LabSpec/ScientificRecipe stages + `StepExplanationViewModel` |
| Autonomy bounds | `autonomyStatusProjection` / `decideAutonomy` (display/request only) |
| Evidence done | session evidence refs + checkpoint results + grader evidence ids |
| Tech vs science validation | verifier report vs grader report (keep vocabularies distinct) |
| Check errors without answers | grader reason slugs + verify fail/indeterminate; hint policy; never leak golden |
| Save/submit reproducible | teaching nav session + capsule export hook (pointer only) |

## Merged vs unwired (avoid redo)

**Do NOT reinvent:** curriculum catalog/progress/availability, LabSpec loader,
ScientificRecipe schema, explain builder/validator, grader engine, verify
status lattice, autonomy policy/engine, capsule builder, `sicnu.lab-session/1`.

**Unwired / incomplete seams this cockpit closes:**
1. No Course Home projecting curriculum modules → labs → status.
2. Guided Workflow is lab-list-only; ignores curriculum DAG, packs, hours, goals.
3. No readiness aggregator (READY / WARNINGS / BLOCKED / UNKNOWN) with reason+evidence.
4. No autonomy ladder display for students.
5. No verifier+grader feedback panel with **indeterminate ≠ pass**.
6. No teaching-scoped session (course/lab/run/autonomy/capsule refs) across restart.
7. `test_curriculum.cpp` not registered; `src/verify` not `add_subdirectory`'d;
   `src/recipes` has no leaf CMake target.

## Owns / must not touch

**Owns:** `src/teaching/**`, `src/app/teaching/**`,
`docs/development/undergrad-lab-cockpit/**`, own tests, append-only CMake.

**Must not steal:** Parameter/Fault/Debugger Studio UI, teacher authoring UI,
Agent context broker, Agent ops/recovery control center.

## Demo path (Definition of Done)

Real chain: Course Home → `lab15_data_inspection` (m01) → readiness → guided
steps → why-this-step → jump/prefill existing operator → feedback → capsule
hook → restart restores teaching session.

## Build notes

- Own build dir: `/workspace/exp-rs-lab-cockpit/build`
- Qt: `/workspace/Qt/6.8.0/gcc_64` (sibling `build-verify` pattern)
- Parallelism: `CMAKE_BUILD_PARALLEL_LEVEL=1|2`, `ninja -j1|-j2`
