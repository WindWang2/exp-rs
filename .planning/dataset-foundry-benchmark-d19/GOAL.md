# exp-rs D19 — Remote-Sensing Dataset Foundry & Scientific Benchmark Platform

Repository:

https://github.com/WindWang2/exp-rs

You are the primary autonomous engineering agent for this development track.

This is a **large, long-running 300M–500M+ token engineering task**.

The purpose of this track is to build a production-quality:

# Remote-Sensing Dataset Foundry & Scientific Benchmark Platform

for `exp-rs`.

This task runs **in parallel** with another development track:

```text
D18 — Unified Remote-Sensing Mission Workbench / Scientific Workspace
```

Therefore D19 must deliberately minimize overlap with D18.

D19 owns:

```text
dataset
samples
labels
dataset versions
splits
patch extraction
feature tables
benchmark definitions
evaluation
dataset QA
experiment linkage
model evaluation
reproducibility
data lineage
```

D18 owns:

```text
main Workbench integration
MissionContext
workspace UI
visual workflow editing
cross-studio UI integration
Agent ↔ GUI workspace state
map/timeline/classification/registration shell integration
```

Do NOT turn D19 into another Workbench redesign.

---

# 0. Mandatory operating mode

Run completely autonomously.

Do not ask the user to make routine implementation decisions.

When multiple valid approaches exist:

1. inspect current master;
2. inspect current ADRs;
3. inspect existing implementations;
4. find the current authority;
5. choose the least duplicative architecture;
6. document the decision;
7. continue.

Do not stop after a small patch.

This track is expected to be a **platform-scale implementation**, not a collection of isolated fixes.

---

# 1. Protect master and create an independent worktree

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

Master is READ-ONLY.

Create:

```text
branch:
grok/dataset-foundry-benchmark-d19

worktree:
../exp-rs-dataset-foundry-benchmark-d19
```

Example:

```bash
git worktree add ../exp-rs-dataset-foundry-benchmark-d19 \
  -b grok/dataset-foundry-benchmark-d19 \
  origin/master
```

All development must happen in this worktree.

---

# 2. Resource restrictions

This repository is large.

Set:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
```

Rules:

```text
normal compile: -j2
heavy compile/link: -j1
tests: serial or extremely low parallelism
Qt: QT_QPA_PLATFORM=offscreen
```

Never use:

```bash
-j$(nproc)
```

Never intentionally saturate RAM/CPU.

Prefer:

```text
targeted build
→ targeted tests
→ focused regression
→ larger integration validation only when justified
```

No online CI dependency.

Do not wait for GitHub Actions.

---

# 3. Before coding, re-read the latest repository

Do not rely on historical summaries.

At minimum inspect:

```text
README.md
PROJECT.md
CONTEXT.md
CHANGELOG.md
CLAUDE.md
.agents/AGENTS.md

ISSUES.md
review/**
docs/adr/**
docs/verification/**

src/dataset/**
src/experiment/**
src/data/**
src/contracts/**
src/operators/**
src/processing/**
src/runtime/**
src/jobs/**
src/models/**
src/agent/**

data/**
tests/**
```

Also inspect current:

```text
open PRs
closed/merged recent PRs
open issues
recent closed issues
branches
last 50–100 commits
```

Pay special attention to recently landed capabilities related to:

```text
D15 classification/change
D16 temporal analysis
Scientific MLOps 9/10
Model Runtime 10
Temporal Platform 10
Spectral Platform 10
Data Fabric 10
Scientific Contract 10
LabSpec / auto-grading
Experiment lineage
dataset version DAG
model promotion
spatial split / leakage defense
```

Do not reimplement them.

---

# 4. Primary architecture question

The core question of D19 is:

> What is the authoritative representation of a remote-sensing dataset used for scientific training, evaluation and reproducibility?

Determine from the current code whether dataset concepts are currently fragmented across:

```text
workspace data
DatasetStore
DataManager
LabSpec
Experiment
feature artifacts
temporal tables
training samples
classification labels
model manifests
benchmark fixtures
file paths
STAC assets
```

Build a current-state architecture map.

Create:

```text
.planning/dataset-foundry-benchmark-d19/
```

with:

```text
GOAL.md
BASELINE.md
CURRENT_ARCHITECTURE.md
DATASET_AUTHORITY.md
ARTIFACT_MATRIX.md
SPLIT_MODEL.md
BENCHMARK_MODEL.md
PLAN.md
MILESTONES.md
DECISIONS.md
PROGRESS.md
TEST_MATRIX.md
PERFORMANCE.md
REVIEW_LOG.md
EVIDENCE.md
PR_BODY.md
```

Verify they are actually Git-tracked.

---

# 5. Package A — Canonical Dataset Manifest 2.0

Create or extend the existing dataset authority.

Do NOT invent a second dataset database if one already exists.

A scientific dataset version should be able to describe:

```text
dataset_id
version_id
parent_version
creation reason

assets
scenes
bands
sensors
modalities

spatial extent
CRS/grid
temporal extent

label schema
class vocabulary

sample units

split definition

feature artifacts

quality masks

provenance

licenses

source identities

digests

transform lineage
```

The manifest must distinguish:

```text
source dataset
derived dataset
training dataset
benchmark dataset
evaluation dataset
```

without requiring five unrelated implementations.

Prefer one versioned model with typed roles.

---

# 6. Dataset version DAG

There should be no silent in-place mutation of scientific datasets.

Implement or harden:

```text
DatasetVersion
```

with parentage such as:

```text
Raw Dataset v1
     ↓
QA-filtered v2
     ↓
Aligned v3
     ↓
Feature-enhanced v4
     ↓
Training snapshot v5
```

Every derived version should record:

```text
parent ids
operation
parameters
software identity
timestamp
input digests
output digests
label schema
split identity
```

Support:

```text
branch
compare
lineage
freeze
reproduce
```

Do not build Git inside Git.

Use the current project storage/metadata architecture.

---

# 7. Package B — Scientific sample model

Remote-sensing samples are not all simple rows.

Design one sample abstraction capable of referencing:

```text
pixel
patch
polygon
object
scene
time series
temporal region
spectral signature
paired before/after sample
optical/SAR pair
multimodal sample
```

Avoid storing giant image patches directly inside JSON metadata.

Prefer sample references:

```text
source asset
window / geometry
time
bands
mask
label
quality
```

Example conceptual structure:

```json
{
  "sample_id": "...",
  "dataset_version": "...",
  "unit": "patch",
  "geometry": "...",
  "asset_refs": ["..."],
  "window": {...},
  "label": {...},
  "quality": {...},
  "provenance": {...}
}
```

Use stable identities.

---

# 8. Package C — Label Schema & Taxonomy

Create or converge a single label schema authority.

Support:

```text
class id
canonical name
localized names
parent/child hierarchy
ignore label
NoData label
unknown label
color/style hint
description
semantic version
```

Never silently remap class IDs.

A class remapping must become an explicit transformation.

Support:

```text
taxonomy migration
class merge
class split
class ignore
class alias
```

and record it in provenance.

Protect against the historical class-id/NoData collision class of bugs.

---

# 9. Package D — Spatially correct split engine

D15 already introduced spatial leakage prevention.

Do NOT create another split implementation.

Promote/reuse the authoritative algorithm.

Create a generalized split framework supporting:

```text
random
spatial block
spatial buffered
region holdout
scene holdout
temporal holdout
sensor holdout
cross-region
cross-year
cross-sensor
```

Every split should have:

```text
split_id
algorithm
seed
parameters
sample assignments
excluded buffer
audit metrics
```

Scientific rules:

```text
train
validation
test
```

must remain isolated where requested.

Add explicit leakage audits.

---

# 10. Spatial leakage audit

Provide diagnostics such as:

```text
minimum Train↔Test distance
shared scene count
shared polygon count
temporal overlap
sensor overlap
duplicate patch overlap
parent-scene overlap
near-duplicate spectral samples
Moran-like spatial dependence indicators
```

Do not assert independence when it cannot be demonstrated.

Produce:

```text
PASS
WARN
FAIL
UNKNOWN
```

not only boolean success.

---

# 11. Package E — Patch / Sample extraction engine

Build a common deterministic extraction engine.

Support:

```text
point-centered patches
polygon chips
object bounding patches
sliding-window samples
stratified sampling
class-balanced sampling
hard-negative sampling
temporal stacks
multimodal paired patches
```

Use existing raster/vector readers.

Do NOT duplicate GDAL I/O.

Patch extraction must:

```text
respect NoData
respect masks
respect CRS/grid
record source windows
record resampling
record calibration state
record band order
```

No hidden reprojection/resampling.

If data are incompatible, use explicit upstream transformation or refusal.

---

# 12. Large sample catalogs

The system should support:

```text
100k+
1M logical samples
```

without loading every sample's heavy metadata into UI/process memory.

Implement or improve:

```text
paging
streaming
index
bounded cache
query/filter
```

Possible filters:

```text
class
sensor
year
region
quality
split
source dataset
modality
```

Test with generated sample records rather than huge committed datasets.

---

# 13. Package F — Feature Artifact Integration

Current exp-rs now produces many useful feature artifacts.

Audit at least:

```text
spectral feature tables
temporal region feature tables
classification features
texture features
SAR features
phenology metrics
change metrics
```

Create a clean dataset-level mechanism to attach derived features to samples.

Avoid arbitrary CSV-by-path coupling.

Use typed feature schema:

```text
feature_set_id
schema_version
sample_key
columns
units
domains
producer
input dataset version
digest
```

Support safe joins by stable sample identity.

Refuse ambiguous joins.

---

# 14. Package G — Annotation and review state model

Do not build a complete annotation GUI unless needed.

Build the underlying model first.

Support label states:

```text
unlabeled
machine proposed
human accepted
human corrected
conflicted
reviewed
rejected
```

Store:

```text
label source
confidence
annotator identity reference
timestamp
model proposal identity
review status
```

Do not silently treat model predictions as ground truth.

Machine labels must remain distinguishable.

---

# 15. Weak / pseudo-label support

Because remote-sensing workflows increasingly use AI-assisted labeling, support explicit pseudo-label provenance.

Example:

```text
source=model
model_id
model_digest
confidence
threshold
postprocessing
human_review=false
```

Pseudo-labels should be filterable from manually verified labels.

Scientific evaluations should be able to refuse pseudo-labeled test sets.

---

# 16. Package H — Dataset Quality Assurance

Create a Dataset QA subsystem.

Potential diagnostics:

```text
missing files
unreadable assets
CRS disagreement
grid disagreement
band mismatch
time duplication
class imbalance
label coverage
NoData percentage
cloud percentage
duplicate samples
near-duplicate samples
spectral outliers
geometry invalidity
tiny polygons
extreme patch imbalance
broken provenance
split leakage
```

Produce a structured QA report.

Avoid one giant opaque score.

Use categories and evidence.

---

# 17. Package I — Scientific Benchmark Definition

Create a formal benchmark definition.

A benchmark should identify:

```text
benchmark_id
benchmark_version

dataset_version
split_id
label_schema_version

task
metric definitions
evaluation rules

allowed preprocessing
forbidden leakage

model/input requirements
seed policy
determinism policy

environment pins

expected outputs
```

Supported task families should be extensible:

```text
classification
segmentation
change detection
object detection
regression
temporal prediction
spectral matching
```

Do not hardcode benchmark logic into individual model adapters.

---

# 18. Metric authority

Audit all current metric implementations.

Examples:

```text
OA
Kappa
precision
recall
F1
IoU
Dice
per-class PA/UA
RMSE
MAE
R²
change Dice
confusion matrix
```

Determine existing authorities.

Do NOT implement duplicate confusion-matrix/evaluation code.

Create a normalized metric result model.

Every metric should expose:

```text
name
definition/version
value
scope
class
support/count
warnings
```

---

# 19. Package J — Benchmark Runner

Build a headless benchmark runner.

Conceptually:

```text
Benchmark Definition
       ↓
Dataset Snapshot
       ↓
Model / Algorithm
       ↓
Execution
       ↓
Predictions
       ↓
Evaluation
       ↓
Evidence
```

The benchmark runner must be usable from:

```text
CLI
experiment system
Agent tool seam
future Workbench D18
```

But D19 should not implement the D18 UI.

Create a clean API that D18 can consume later.

---

# 20. Reproducibility

A benchmark result must record at minimum:

```text
dataset version
split id
sample catalog digest
label schema version
model id
model digest
model manifest
parameters
seed
software commit
provider
device
relevant environment pins
metrics
outputs
```

If required identity cannot be verified, report incomplete reproducibility.

Do not fabricate a reproducibility score.

---

# 21. Package K — Experiment System Integration

Inspect current:

```text
src/experiment/**
model promotion
replay
experiment comparison
lineage
```

Do not replace it.

Integrate Dataset Foundry so an Experiment Run references:

```text
dataset_version
split
benchmark_definition
model
result
```

Enable comparisons such as:

```text
same model / different dataset version
same dataset / different model
same model / different seed
same model / different sensor
same model / cross-region
same model / cross-year
```

---

# 22. Cross-region / cross-year / cross-sensor benchmarks

Remote sensing scientific evaluation should not stop at random train/test split.

Provide first-class benchmark modes for:

```text
cross-region
cross-year
cross-season
cross-sensor
cross-resolution
cross-modality
```

Example:

```text
Train:
Sichuan region A, 2025

Test:
Sichuan region B, 2026
```

or:

```text
Train:
GF optical

Test:
Sentinel-2
```

when scientifically valid.

If domains are incompatible, report that explicitly.

---

# 23. Package L — Benchmark comparison & statistical summaries

Create structured comparisons.

Support:

```text
absolute metric difference
relative difference
per-class delta
variance across seeds
mean/std
confidence intervals where appropriate
```

Avoid pretending one run is statistically conclusive.

If enough repeated runs exist, support paired comparisons.

Do not introduce questionable significance tests without documenting assumptions.

---

# 24. Model promotion integration

Current Model Runtime/MLOps already has promotion-related seams.

Inspect and reuse them.

A model promotion decision should be able to require:

```text
benchmark family A passed
benchmark family B passed
no leakage finding
minimum metric thresholds
provenance complete
dataset frozen
```

D19 should provide evidence.

Do not make the Dataset Foundry directly own production model deployment.

---

# 25. Agent-facing dataset intelligence

Expose bounded deterministic tools such as conceptually:

```text
dataset:inspect
dataset:versions
dataset:qa
dataset:splits
dataset:sample_query
benchmark:list
benchmark:inspect
benchmark:compare
```

Agent must be able to answer:

```text
What data was this model trained on?
Is this benchmark spatially leaked?
Which classes are underrepresented?
Why did the benchmark fail?
Which regions are in test?
Are pseudo-labels present?
```

from deterministic metadata.

Do not send million-row sample catalogs into LLM context.

Return summaries and pagination.

---

# 26. Keep D19 independent from D18

To permit parallel development:

Avoid major changes under:

```text
src/app/workbench/**
main window
MissionContext
visual workflow canvas
main workspace state
```

If D19 needs future UI integration:

create a clean service/interface.

Do not prematurely wire it deeply into D18-owned code.

Prefer:

```text
DatasetFoundryService
BenchmarkService
DatasetQuery
BenchmarkResult
```

which D18 can consume later.

Any unavoidable shared-file change should be:

```text
minimal
append-only when possible
isolated in its own commit
```

---

# 27. Performance constraints

Test using generated logical scale:

```text
100k datasets/assets
1M logical samples
100k regions
large label schemas
many dataset versions
large benchmark histories
```

Memory should scale according to:

```text
page
window
chunk
selected result
```

not:

```text
all samples × all metadata
```

Avoid GUI-level tests here unless strictly necessary.

Focus on headless services and contracts.

---

# 28. Failure matrix

Test:

```text
deleted source asset
changed source digest
broken label file
duplicate sample id
duplicate version id
cyclic dataset DAG
invalid parent
split leakage
unknown class
class-id collision
NoData-only sample
missing band
wrong sensor
wrong CRS
stale feature table
pseudo-label in protected test set
model incompatible with benchmark
aborted benchmark
worker crash
cancel
corrupt result
```

All failures must be typed and honest.

---

# 29. Security and privacy

Dataset metadata may eventually contain sensitive paths or organizational information.

Ensure:

```text
no credential persistence
no cloud-token logging
bounded diagnostics
path-safe serialization
no arbitrary command execution from manifests
```

Benchmark definitions are data, not shell scripts.

---

# 30. Testing philosophy

Prioritize:

```text
closed-form truths
invariants
independent references
property tests
metamorphic tests
negative tests
failure injection
scale tests
```

Do not use production logic as its own oracle.

Important scientific invariants include:

```text
split membership disjointness
stable seeded split
dataset version immutability
digest stability
sample identity stability
class map injectivity where required
no hidden pseudo-label promotion
no train/test overlap
metric reproducibility
```

---

# 31. Review phase

After feature implementation, stop adding scope.

Run an independent deep review.

Maximum 2 subagents.

Prefer:

### Reviewer A

```text
dataset architecture
scientific methodology
split correctness
label semantics
benchmark integrity
```

### Reviewer B

```text
performance
concurrency
persistence
failure recovery
test credibility
security
```

Review the actual full branch diff.

Classify:

```text
P0
P1
P2
P3
```

P0/P1 must all be resolved before PR.

High-value P2 should be fixed.

Record every finding and disposition.

---

# 32. Required E2E scenarios

## E2E 1 — Classification benchmark

```text
source imagery
→ sample generation
→ spatial split
→ feature extraction
→ classification/model
→ prediction
→ confusion matrix
→ benchmark result
→ experiment record
```

Validate zero split leakage.

---

## E2E 2 — Temporal land-use benchmark

```text
multi-temporal scenes
→ temporal region features
→ labels
→ cross-year split
→ model
→ evaluation
→ comparison
```

---

## E2E 3 — Dataset version evolution

```text
dataset v1
→ QA/filter transformation
→ v2
→ label correction
→ v3
→ benchmark rerun
→ compare results
```

Old benchmark must remain reproducible against frozen v1.

---

## E2E 4 — Pseudo-label safety

```text
model proposals
→ pseudo labels
→ mixed training set
→ test benchmark
```

Ensure pseudo-labels cannot silently enter a benchmark ground-truth test partition when policy forbids it.

---

## E2E 5 — Cross-region generalization

```text
Region A train
Region B test
```

Generate:

```text
split evidence
distribution summary
metrics
per-class results
provenance
```

---

# 33. Completion definition

Do not declare completion until:

- latest master has been re-read;
- no major recent implementation is duplicated;
- canonical dataset version model exists or existing one is properly extended;
- dataset lineage is explicit;
- sample model supports remote-sensing units;
- label schema is versioned;
- scientific split framework is implemented;
- leakage auditing is available;
- feature artifacts join through stable identities;
- pseudo-label provenance is explicit;
- Dataset QA exists;
- Benchmark Definition exists;
- Benchmark Runner works headlessly;
- benchmark results integrate with Experiment/MLOps;
- current model provenance can link to dataset/benchmark identity;
- major failure modes are covered;
- large logical sample catalogs remain bounded-memory;
- P0/P1 review findings are zero;
- final local evidence is complete;
- branch has been updated against latest master safely;
- affected tests have been rerun after conflict resolution;
- branch is pushed;
- PR targeting `master` is created.

Do NOT merge the PR yourself.

---

# 34. PR requirements

PR body must include:

```text
baseline SHA

existing capabilities reused

dataset authority decision

version DAG architecture

sample model

label schema

split/leakage design

benchmark architecture

Experiment/MLOps integration

tests

scale evidence

review findings

known limitations

follow-ups

local evidence only;
no online CI dependency
```

---

# 35. Long-running autonomy

After each milestone:

```text
inspect current state
→ locate the highest-value remaining in-scope gap
→ implement
→ test
→ review
→ continue
```

Do not stop simply because the first implementation plan has been exhausted.

The target is not:

> “some dataset utilities were added.”

The target is:

> exp-rs has a scientifically trustworthy Dataset Foundry in which samples, labels, splits, dataset versions, benchmarks, experiment results and model evidence form one reproducible chain.