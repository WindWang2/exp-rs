# ADR 0146: LabSpec — Data-Driven Lab Specifications

Status: accepted · Branch `zcode/lab-spec-data-driven` · Baseline `origin/master@27b9aa0a63`

## Context

"本科实验有几门" had three mutually contradictory answers:

- `docs/labs/` hand-maintained markdown documented **7** labs;
- `guided_workflow_widget.cpp` hardcoded **10** workflows in C++
  (`createSpectralAnalysisWorkflow()` … `createObiaWorkflow()`, four of which
  appear nowhere in the docs);
- the doc-cited menu paths (`Raster > Enhancement`) no longer exist after
  ADR 0099's task-centric menu rework.

The existing widget tests asserted struct-field round-trips only — zero
behavioural coverage — and `WorkflowStep::actionId` was a free-form menu-action
string, unusable for headless execution or grading. Lab content was therefore
duplicated in two dead sources (C++ and prose) with no executable semantics.

## Decisions

1. **One LabSpec, one truth.** Each lab is a declarative JSON document at
   `data/labs/<id>.lab.json`, validated against
   `data/schemas/labspec.schema.json` (draft-07, same convention as
   `pipeline_schema.json`). The guided workflow widget, the generated
   documentation, and (future) auto-grading all consume the same file.
   Documentation is *build output*: `scripts/gen_lab_docs.py` regenerates
   `docs/labs/*.md`, and `--check` makes drift a test failure.
2. **Steps are operator-bound, not menu-bound.** `actionId: "menu string"`
   is replaced by `{operator_id, params}` resolved against the Processing
   Registry (`rs:*` / `opencv:*`). A step therefore doubles as (a) a UI
   launch with prefilled parameters and (b) a headless-executable unit for
   D4/D7: the widget submits the same `JobRequest{algorithmId, params}` that
   dialogs and agent tool callers submit, so guided execution and headless
   execution share one path. Steps that are pure UI affordances (load data,
   open compare view) keep an optional `action` slot name; steps with neither
   are manual teaching content. `operator_id` and `action` are mutually
   exclusive by schema.
3. **No C++ fallback content.** The widget renders whatever
   `lab::loadLabSpecsFromDir()` returns. A missing directory, an invalid file,
   or a duplicate id produces a typed error (`lab::LabSpecError`) surfaced as
   a dedicated error entry in the widget; the ten `createXxxWorkflow()`
   factories are deleted. Silent fallback to built-in content is forbidden.
4. **Validation happens at every layer that can afford it.**
   The loader enforces the schema structurally (typed errors, no exceptions).
   `tests/test_labspec.cpp` is the drift guard: every `operator_id` must
   resolve in `AtomicAlgorithmRegistry`, every `params` object must satisfy
   the operator descriptor via the shared `validateParameters()` seam, every
   `grading_ref` must resolve to an existing pipeline, and regenerated
   markdown must equal committed markdown.
5. **Path contract.** Lab inputs are referenced as project-root-relative
   paths beginning `data/` (sample rasters stay gitignored); lab outputs
   begin `outputs/` and resolve to `output/labs/<labId>/<name>` at submit
   time. Resolution happens at the widget's submit boundary via
   `lab::resolveLabParamPaths()` with an injected resolver — the loader stays
   pure (no filesystem, no QCoreApplication), so tests inject fakes.
6. **Canonical inventory is the union.** The 7 documented labs and 10
   hardcoded workflows reconcile into 11 numbered labs (`lab01`–`lab11`,
   zero-padded for stable ordering): georeferencing stays (docs-only, UI
   bound), atmospheric correction joins from the widget, and the three widget
   orphans (PCA, mosaic, OBIA) are promoted rather than deleted. All three
   inventories now report the same count.
7. **`grading_ref` is an optional pointer, not a copy.** It references an
   existing `data/pipelines/*.json` recipe (e.g. `landsat_ndvi.json`); D4/D7
   own the grading engine. The drift guard pins the reference so specs never
   cite dead artifacts.

## Consequences

- Adding a lab is a data commit: one `.lab.json` plus regenerated docs; no
  C++, no doc edits. Schema-invalid content is refused at load with a
  precise, file-and-line-free but self-contained reason string.
- Widget UI text is localized with `tr()` at the rendering layer; LabSpec
  content itself is authored bilingual by contract (`title`/`title_zh`,
  `description_zh` as the teaching body) and is *not* routed through Qt
  translation machinery. Repo-wide translation extraction remains D6.
- The loader is a new file pair (`src/app/widgets/lab_spec_loader.{h,cpp}`)
  kept dependency-light so `tests/test_guided_workflow_widget` compiles it
  without the app shell.
