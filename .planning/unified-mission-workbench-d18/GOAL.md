# exp-rs D18 — Unified Remote-Sensing Mission Workbench / Scientific Workspace

Repository:

https://github.com/WindWang2/exp-rs

You are the primary autonomous engineering agent for this task.

This is a **very large, long-running development task**. Treat the expected engineering/reasoning budget as **300M–500M+ tokens**. Do NOT collapse this task into a small patch, shallow refactor, documentation-only pass, or a few isolated UI changes.

The purpose of this task is to perform the **next major integration stage of exp-rs** after the 10.0 platform wave and the recently merged D14/D15/D16/D17 vertical workbenches.

The target is:

> Build a unified project-level remote-sensing scientific workspace in which data, AOI, map state, temporal analysis, spectral analysis, registration, classification, workflow execution, Agent reasoning, cartography, provenance, experiment history and final outputs all participate in one coherent mission context.

The emphasis is **integration, authority convergence, workflow coherence, scientific state management and professional usability**, not adding another isolated algorithm family.

---

# 0. Mandatory execution rules

Operate fully autonomously.

Do not stop and ask the user to choose between reasonable implementation alternatives.

When several approaches are possible:

1. inspect the current code;
2. inspect existing ADRs;
3. inspect current contracts;
4. inspect recently merged PRs;
5. determine the existing authority;
6. choose the smallest architecture that preserves that authority.

Do not create parallel frameworks when an existing implementation can be extended.

Do not invent requirements based on this prompt if current master has changed.

The latest repository is the source of truth.

---

# 1. Master must remain read-only

First:

```bash
git fetch --all --prune
git checkout master
git pull --ff-only
```

Record:

```bash
git rev-parse HEAD
git status
git branch -a
```

MASTER IS READ-ONLY.

Create an isolated worktree from the latest `origin/master`.

Suggested branch:

```text
grok/unified-mission-workbench-d18
```

Suggested worktree:

```text
../exp-rs-unified-mission-workbench-d18
```

Example:

```bash
git worktree add ../exp-rs-unified-mission-workbench-d18 \
  -b grok/unified-mission-workbench-d18 origin/master
```

Never develop directly in master.

---

# 2. Before writing code: deeply re-analyse the current repository

Do NOT trust historical summaries.

Before implementation, deeply inspect the latest master.

At minimum read:

```text
README.md
PROJECT.md
CONTEXT.md
CHANGELOG.md
CLAUDE.md
.agents/AGENTS.md

docs/adr/**
docs/verification/**
docs/processing/**
docs/temporal/**
docs/labs/**

review/**
ISSUES.md

src/app/**
src/workflow/**
src/agent/**
src/operators/**
src/processing/**
src/geospatial/**
src/runtime/**
src/jobs/**
src/experiment/**
src/dataset/**
src/contracts/**
src/help/**

data/labs/**
data/agent/**
data/processing/**
data/products/**
```

Also inspect:

- latest merged PRs;
- open PRs;
- open issues;
- recent closed issues;
- remote branches;
- recent 50–100 commits;
- D14;
- D15;
- D16;
- D17;
- Workbench 9/10;
- Scientific Workflow Compiler 10;
- Large-Scale Execution 10;
- Data Fabric 10;
- Cartography 10;
- Model Runtime 10;
- temporal / spectral / SAR platform tracks.

Explicitly identify functionality that has already landed.

Do NOT reimplement it.

---

# 3. Pay special attention to recently merged D14–D17

Current master recently absorbed multiple very large vertical tracks.

Deeply inspect their implementation, not only their PR descriptions.

## D14

Geometric registration workbench:

- GCP manager;
- geometric transform;
- TPS;
- feature matcher;
- RANSAC;
- resampler;
- pan sharpening;
- dual-map comparison;
- geometric Agent tool;
- lab integration.

## D15

Classification/change studio:

- spatial leakage prevention;
- GLCM;
- classifiers;
- post-processing;
- change detection;
- accuracy assessment;
- studio UI;
- Agent diagnostics;
- labs.

## D16

Temporal phenology timeline:

- temporal cube;
- smoothing;
- phenology;
- BFAST-like breakpoint analysis;
- trend analysis;
- STARFM;
- temporal profile/timeline UI;
- temporal Agent tools;
- lab integration.

## D17

Visual workflow pipeline designer:

- Workflow IR 2.0;
- DAG analyzer;
- contract checker;
- repair engine;
- plan optimizer;
- cost estimator;
- PipelineRunCoordinator;
- workflow checkpoint;
- node canvas;
- guided workbench;
- Agent orchestrator.

Do not assume these four tracks are already properly integrated simply because they individually pass tests.

Their recent independent development makes cross-track architectural drift one of the primary risks for D18.

---

# 4. Phase A — Perform a cross-track architecture convergence audit

Before building new features, answer from the code:

## 4.1 How many competing workflow representations now exist?

Locate and compare at least:

```text
WorkflowDefinition
Workflow IR 1.x
Workflow IR 2.0
AgentPlan
compiled workflow JSON
LabSpec workflow representation
guided workflow representation
recipe representation
cartography workflow representation
```

Build an explicit conversion graph.

Example conceptual form:

```text
Natural Language
      ↓
Scientific Goal
      ↓
?
      ↓
Workflow IR
      ↓
?
      ↓
WorkflowDefinition
      ↓
ExecutionPlane
```

Do not assume the correct answer.

Find the actual current graph.

Document:

- authority;
- projections;
- adapters;
- duplicate semantic fields;
- information loss;
- schema divergence;
- unnecessary round trips;
- execution-only vs editor-only models.

The desired result is NOT necessarily “delete Workflow IR 1 immediately”.

The desired result is:

> one explicit authority model with clearly defined compatibility projections.

If two representations have legitimate different responsibilities, document the boundary and mechanically test the projection.

---

# 5. Phase B — Introduce a Mission / Scientific Workspace Context

The current system has many powerful domain-specific tools.

Create or converge toward a project-level scientific context.

The context should be able to reference, without duplicating underlying objects:

```text
Project
Mission / Analysis Session
AOI
Temporal extent
Datasets
Assets
Layers
Results
Models
Workflow runs
Experiments
Map state
Selections
Artifacts
Cartographic products
Reports
Agent decisions
```

Do NOT create a giant mutable God Object.

Use stable identities and typed references to existing authorities.

Prefer:

```text
MissionContext
  -> ProjectRef
  -> DatasetRef[]
  -> LayerRef[]
  -> ResultRef[]
  -> WorkflowRunRef[]
  -> ExperimentRef[]
  -> ModelRef[]
  -> SpatialContext
  -> TemporalContext
  -> SelectionContext
```

The Mission Context should be serializable enough for:

- save;
- restore;
- checkpoint;
- Agent grounding;
- session continuation.

But it must NOT serialize live QObject/QGIS pointers.

---

# 6. Phase C — Unify Workbench object identity

Audit current identities for:

```text
asset
dataset
layer
result
experiment
model
workflow
workflow run
artifact
lab
cartography document
```

Determine which already have stable IDs.

Never create duplicate IDs when a canonical store already owns identity.

Extend the unified object-reference system where necessary.

The workbench should understand relationships such as:

```text
source asset
   ↓
processing result
   ↓
display layer
   ↓
workflow run
   ↓
experiment
   ↓
MapSpec
   ↓
exported map
```

and:

```text
temporal cube
   ↓
phenology result
   ↓
classification features
   ↓
model
   ↓
classification output
```

Agent and UI should refer to the same identities.

---

# 7. Phase D — Integrate D14/D15/D16 as workspace modes, not islands

Inspect how each current studio is mounted.

The goal is NOT to remove specialist interfaces.

The goal is:

> specialist workspaces should operate on the same Mission Context.

## Geometric Registration

A user should be able to:

```text
select source image
→ open registration workspace
→ define/import GCPs
→ perform registration
→ publish aligned product
→ aligned product becomes a normal Result/Layer
→ continue into classification / temporal / workflow processing
```

No manual re-import should be required if the output already exists in the workspace.

## Classification / Change Detection

Inputs should come from current workspace selections or workflow products.

Outputs should:

- publish Result identity;
- optionally publish Layer;
- include accuracy/provenance;
- become immediately available to cartography and Agent.

## Temporal Studio

Timeline state should understand:

- active temporal collection;
- active acquisition;
- active AOI;
- current map view;
- current profile;
- selected phenology/change result.

Timeline → map synchronization should be explicit.

Avoid private state that Agent cannot inspect.

---

# 8. Phase E — Visual Workflow Designer becomes a real application surface

D17 delivered the core.

Now complete professional integration.

Audit:

```text
node canvas
guided workbench
PipelineRunCoordinator
WorkflowIR 2.0
checkpoint
contract checker
repair engine
cost estimator
Agent orchestrator
```

Integrate it into the main Workbench.

Required behavior:

```text
Workbench selection
    ↓
Create/open workflow
    ↓
Visual DAG
    ↓
Schema parameter editing
    ↓
Static validation
    ↓
Cost/resource preview
    ↓
Execute
    ↓
TaskCenter
    ↓
Node live status
    ↓
Results
    ↓
Workspace
```

Add:

- node state;
- queued/running/success/failure/cancelled/cache-hit;
- error badges;
- repair suggestions;
- execution evidence;
- output inspection;
- resume state.

Do NOT create a second scheduler.

Execution must remain on the authoritative runtime / TaskCenter / WorkflowRunCoordinator path.

---

# 9. Phase F — Agent ↔ Workspace bidirectional context

This is one of the most important D18 goals.

Agent must be able to understand:

```text
which project is open
which AOI is active
which datasets exist
which layers are visible
which result is selected
which temporal acquisition is active
which workflow is open
which node failed
which model is available
which analysis just completed
which MapSpec exists
```

Create deterministic workspace/context tools or extend existing tools.

Avoid dumping huge GUI state into the LLM.

Use bounded machine-readable summaries.

Example:

```json
{
  "mission": "...",
  "selection": {...},
  "visible_layers": [...],
  "active_workflow": {...},
  "active_temporal_context": {...},
  "recent_results": [...],
  "pending_failures": [...]
}
```

Agent should be able to:

```text
inspect
plan
compile
execute
observe
diagnose
repair
continue
```

while retaining the original scientific intention.

---

# 10. Phase G — Human ↔ Agent workflow editing round trip

This is a major acceptance target.

Required flow:

```text
User:
“Use the aligned GF imagery to classify cultivated land and compare it with last year.”

Agent
→ creates Workflow IR

Visual Workflow Designer
→ displays same workflow

User
→ modifies a node / parameter

Agent
→ sees the modified workflow

Execution
→ runs the modified graph

One node fails

Agent
→ diagnoses the actual run

Agent
→ proposes/executes a bounded repair

Visual graph
→ shows the repaired version
```

The Agent must not operate on a hidden private copy of the workflow.

The UI must not operate on another divergent copy.

Use explicit document/version/fingerprint semantics.

---

# 11. Phase H — Analysis → Cartography → Export

Use the existing Cartography / MapSpec platform.

Do NOT rebuild map layout composition.

The Mission Workspace should allow:

```text
analysis result
  ↓
style
  ↓
MapSpec
  ↓
layout
  ↓
preflight
  ↓
repair
  ↓
export
```

Map products must preserve links back to:

- source datasets;
- workflow;
- experiment;
- analysis results;
- style/template;
- export settings.

Agent should be able to answer:

“这张图是怎么来的？”

from actual provenance.

---

# 12. Phase I — Scientific Report / Mission Summary

Create a structured mission summary model or reuse an existing report model.

It should be possible to generate:

```text
Inputs
AOI
Dates
Sensors
Processing workflow
Parameters
Models
Scientific assumptions
Outputs
Accuracy / QA
Known limitations
Maps
Tables / charts
Provenance
```

Do not build a full word processor.

The target is a structured report artifact suitable for:

- HTML;
- Markdown;
- Lab report;
- reproducibility evidence;
- later export.

Reuse current Lab report / Experiment lineage infrastructure where appropriate.

---

# 13. Phase J — Persistence and session continuation

Test real workflows across restart.

Mission state must not depend on live widget pointers.

Support:

```text
save project
close application
reopen project
restore mission
restore workflow
restore analysis result links
restore map context
restore timeline context
resume unfinished workflow when valid
invalidate stale state when underlying data changed
```

Checkpoint validation must include enough identity information to avoid replaying against changed inputs.

Reuse existing execution fingerprints and artifact identities.

---

# 14. Resolve recent cross-track architectural drift

The recent independent tracks were intentionally additive.

Now explicitly look for:

- duplicate helper classes;
- duplicate math kernels;
- duplicate temporal types;
- duplicate workflow types;
- duplicate selection state;
- duplicate execution state;
- duplicate provenance builders;
- duplicated Agent schemas;
- duplicated UI data models;
- duplicated static registries;
- duplicated path resolution;
- parallel report formats.

Do NOT blindly delete duplicates.

For each candidate:

```text
A and B exist
→ identify authority
→ compare contracts
→ migrate call sites
→ add regression
→ deprecate/remove duplicate only when safe
```

Record all convergence decisions.

---

# 15. Integration regression review after the D14–D17 merge wave

Because multiple large additive PRs were merged closely together, run a dedicated post-integration audit.

Inspect especially:

```text
.gitignore
tests/CMakeLists.txt
src/**/CMakeLists.txt
registration tables
operator registries
capability knowledge
LabSpec
ADR numbering
generated docs
workflow catalogs
help catalogs
Agent tool catalogs
translations
```

Look for:

- lost entries during conflict resolution;
- duplicate registration;
- stale descriptor counts;
- generator drift;
- test name collision;
- conflicting Lab IDs;
- duplicate ADR IDs;
- unreachable UI;
- code compiled in tests but not production;
- production code compiled but never wired into UI.

This is a critical task.

---

# 16. UI rules

This is NOT a “make it prettier” project.

UI changes must improve the actual scientific workflow.

Priorities:

1. coherent workspace;
2. state visibility;
3. selection consistency;
4. tool availability;
5. workflow observability;
6. linked views;
7. Agent coexistence;
8. error/recovery UX;
9. performance;
10. visual consistency.

Maintain:

- QGIS-native map interaction;
- model/view for large data;
- QPointer lifecycle safety;
- no heavy raster processing on the UI thread;
- bounded chart samples;
- HiDPI;
- Chinese/English text;
- keyboard/focus correctness.

---

# 17. Performance and resource policy

Compilation:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
```

Use:

```text
-j2 normally
-j1 for heavy compile/link stages
```

Never:

```text
-j$(nproc)
```

Prefer targeted builds/tests.

Qt tests:

```bash
QT_QPA_PLATFORM=offscreen
```

Do not overload the machine.

The system may contain very large logical datasets.

Do NOT require huge committed fixtures.

Use synthetic/sparse/generated datasets to test:

- 100k records;
- thousands of temporal scenes logically;
- large workflow DAGs;
- many workspace objects;
- long session histories.

---

# 18. No online CI dependency

Do not wait for GitHub Actions.

Do not use remote CI green status as the definition of completion.

Use local evidence.

If CI configuration is unrelated, do not expand scope.

---

# 19. Planning files

Create a planning directory such as:

```text
.planning/unified-mission-workbench-d18/
```

Maintain at least:

```text
GOAL.md
BASELINE.md
CURRENT_ARCHITECTURE.md
AUTHORITY_MAP.md
WORKFLOW_MODEL_MAP.md
DUPLICATION_AUDIT.md
PLAN.md
MILESTONES.md
DECISIONS.md
INTEGRATION_AUDIT.md
TEST_MATRIX.md
PERFORMANCE.md
REVIEW_LOG.md
EVIDENCE.md
PR_BODY.md
```

Make sure `.planning` files are actually tracked.

Do not discover at the end that `.gitignore` discarded the evidence.

---

# 20. Development method

Work in vertical slices.

For each package:

```text
inspect
→ define contract
→ write/adjust test
→ implement
→ targeted build
→ targeted test
→ integration test
→ commit
```

Prefer small coherent commits.

Do not place unrelated modifications into one commit.

Shared integration files should preferably be separate commits.

---

# 21. Independent review before PR

After the implementation is feature-complete, stop adding features.

Perform a deep independent review of the complete branch diff.

Review:

## Architecture

- duplicate authorities;
- cycles;
- ownership;
- incorrect dependency direction;
- hidden state;
- inappropriate global state.

## Lifecycle

- QObject ownership;
- QPointer;
- callbacks after destruction;
- queued signals;
- cancellation;
- shutdown.

## Workflow

- stale IR;
- mismatched version;
- checkpoint corruption;
- cache identity;
- execution duplication;
- retry loops.

## Agent

- hallucinated facts;
- unbounded context;
- stale project state;
- unsafe repair;
- hidden workflow copies;
- infinite retry.

## Scientific semantics

- CRS;
- grid;
- temporal alignment;
- data domain;
- model assumptions;
- output meaning;
- provenance.

## UI

- unavailable commands;
- stale selection;
- disabled-state errors;
- blocking operations;
- large data performance.

## Tests

Find:

- vacuous assertions;
- tests only checking non-null;
- mocks that bypass production;
- duplicated production logic used as oracle;
- timing-flaky tests;
- tests compiled differently from production.

Classify findings:

```text
P0
P1
P2
P3
```

P0/P1 must be fixed before PR.

High-value P2 should also be fixed.

Record all dispositions in:

```text
REVIEW_LOG.md
```

---

# 22. Critical E2E acceptance scenarios

At least implement and verify several mission-level scenarios.

## Scenario 1 — Registration → Classification → Cartography

```text
raw images
→ geometric registration
→ aligned result
→ classification
→ accuracy assessment
→ workbench result
→ MapSpec
→ export
```

No manual file re-import between stages.

## Scenario 2 — Temporal analysis

```text
catalog/data cube
→ temporal collection
→ regularization
→ smoothing
→ phenology/change
→ timeline visualization
→ Agent interpretation
→ report artifact
```

## Scenario 3 — Agent + Visual Workflow roundtrip

```text
natural-language request
→ Agent WorkflowIR
→ visual graph
→ human edit
→ execute
→ node failure
→ Agent diagnose
→ repair
→ resume
→ results
```

## Scenario 4 — Save/reopen/replay

```text
mission
→ run workflow
→ save
→ restart
→ restore
→ verify provenance
→ replay or resume
```

## Scenario 5 — failure honesty

Inject:

```text
missing CRS
wrong band
stale input
deleted artifact
model mismatch
NoData-only raster
worker crash
cancel
```

Ensure the system:

- refuses honestly;
- preserves context;
- does not fabricate success;
- suggests valid next actions.

---

# 23. Definition of Done

Do NOT stop merely because several features have landed.

The task is complete only when:

- the latest master was re-analysed;
- D14–D17 integration was audited;
- workflow authority is explicitly documented;
- conflicting workflow representations have a convergence strategy;
- Mission Context exists and is used;
- specialist studios operate on common project state;
- visual workflow is integrated into the main workbench;
- Agent and UI operate on the same workflow identity;
- major outputs publish into unified Result/Layer/Artifact identities;
- analysis → cartography is connected;
- provenance remains traceable;
- save/reopen/session continuation works for the implemented scope;
- mission-level E2E scenarios are green;
- P0/P1 review findings are zero;
- final local evidence is recorded;
- branch is rebased/updated against latest origin/master when safe;
- conflicts are resolved and affected tests rerun;
- branch is pushed;
- an independent PR targeting `master` is created.

Do NOT merge the PR yourself.

PR body must include:

```text
baseline SHA
architecture
authority/convergence decisions
major deliverables
D14–D17 integration findings
compatibility
tests
performance/resource evidence
review findings
known limitations
follow-ups
local evidence only; no online CI dependency
```

---

# 24. Long-run autonomy instruction

This is intentionally a very large task.

Do not interpret the prompt as a short checklist.

After each milestone:

1. reassess current implementation;
2. inspect remaining integration gaps;
3. select the highest-value in-scope next item;
4. implement it;
5. test it;
6. continue.

Do not stop merely because the original obvious tasks are finished.

Continue until the **Unified Remote-Sensing Mission Workbench** is a coherent platform rather than a collection of independently successful vertical demos.