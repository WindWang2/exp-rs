# Plan — RS14-07 Parameter Sensitivity & Uncertainty Studio

## Problem statement

Students (and future agents) can change an operator parameter and click "run", but cannot see *how* a parameter shapes the spatial result and accuracy: no controlled sweep, no aggregated evidence, no honest failure accounting. Today's `Experiment Matrix` (M5) provides a cartesian descriptor + ledger + aggregator but no study spec, no one-at-a-time/Latin-hypercube sampling, no runner, no sensitivity curves, no uncertainty envelope, and no exportable teaching document.

## User stories

**Undergraduate (teaching mode)**
- As a student I load an exemplar study ("NDVI threshold sweep"), provide my raster, run it, and see: the run table, the curve `maskedPercent` vs `threshold` with an honest spread band, a spatial difference summary vs the baseline run, and a mechanical trend note — so I experience the parameter→result→interpretation chain instead of a single button press.
- As a student, when a point fails or is cancelled, I see it in the table with the typed error — nothing is silently dropped or auto-retried.

**AI Agent (agent mode)**
- As an agent I read a versioned `sicnu.studyreport.v1` JSON document (paths recorded in `ExperimentStore` artifacts) to consume machine-readable evidence: per-point parameters, metrics, status accounting, Pareto set, envelopes. The report NEVER declares "the best parameters" unless the spec declared an explicit task metric; then it labels the front-runner as `declaredBest` with its evidence, not as a recommendation.
- As an agent I create a study programmatically from a spec JSON, run it through the same runner, and get deterministic point identity (same spec+seed ⇒ same `pointId`s) so my evidence is replayable.

## Architecture

New module `src/study/` (library `sicnu_study`, static, namespace `sicnu::study`), following the `sicnu_experiment` layer contract (Qt Core only; no Widgets/GUI/Network — enforced by a CMake layer guard).

```
spec ──► sampler ──► runner ──►(ExecutionPlane/TaskCenter spine)──► recorder/ledger ──► analysis ──► export
  ParameterStudySpec   StudyPoint      IStudyExecutionBackend port                     StudyReport
  (versioned)          (matrix         prod adapter: ExecutionPlaneStudyBackend        (versioned,
                        cellIds)        study-owned output committer policy             atomic JSON)
```

- **Identity**: study points reuse the matrix `cellId` content hash. One additive public helper `matrixCellId(assignments)` is exposed by `experiment_matrix.h` (its `enumerateCells` is refactored to call it) — single fact source for point identity; grid sampling literally goes through `MatrixDescriptor::enumerateCells` (cap + refusal included).
- **Execution**: the runner holds a bounded in-flight submission window (`maxInFlight`, cap 8) over an injected `IStudyExecutionBackend` port. The production adapter wraps `ExecutionPlane::submit/awaitResult` (source tag `"study"` → Background lane; `correlationId` = `studyId/pointId#rep`). **No thread pool, no second scheduler** — admission/concurrency/RAM stay in TaskCenter.
- **Truth**: every point is an `ExperimentRun` (recorder: `startRun`→`markSucceeded/Failed/Cancelled`) in the existing `ExperimentStore`; point↔run links are `MatrixLedger` lineage edges. Analysis reads stores only; aggregates are projections, never a parallel store.
- **Spatial differences**: `ISpatialDifferenceSummarizer` port; pure buffer-level implementation reusing `ChangeDetection::difference/statistics` semantics and NaN conventions; production GDAL adapter checks alignment/CRS and refuses mismatches with typed errors.
- **Outputs**: sweep rasters are written to a study-owned run directory via a commit-policy handler (temp→stable atomic move; *no* catalog asset registration — catalog flooding is worse for a 100-point sweep; provenance lives in the experiment run artifacts + lineage edges). Report JSON via `writeFileAtomic`.

## Public API / data schema (v1)

`kStudySpecSchemaVersion = 1`, `kStudyReportSchemaVersion = 1` (documents carry `"schema_version"`; readers refuse foreign versions with typed errors).

```cpp
namespace sicnu::study {

enum class SamplingStrategy { Grid, OneAtATime, LatinHypercube };

struct ParameterDimension {   // one operator parameter path, numeric ladder
  QString parameterPath;      // flat operator param key, v1 (e.g. "threshold")
  double minValue, maxValue;  // inclusive
  int stepCount;              // >= 2 (value count incl. both ends)
};

struct StudyBudget {
  qint64 maxRuns = 100;       // hard cap: kMaxMatrixCells (1000); refusal, not truncation
  int maxInFlight = 2;        // bounded submission window, 1..8
  qint64 perRunTimeoutMs = 600000;
  int seedReplicates = 1;     // >= 1; replicate r uses seed = seed + r
  quint64 seed = 0;           // sampler + replicate seed base
};

struct StudyMetricSpec { QString name; bool maximize; };

struct ParameterStudySpec {
  QString studyId, experimentId, algorithmId;   // experimentId auto-created if missing
  QJsonObject baseParameters;                   // shared params ("output" is runner-managed)
  SamplingStrategy strategy = SamplingStrategy::Grid;
  QVector<ParameterDimension> dimensions;
  StudyBudget budget;
  QStringList metricNames;                      // payload keys to aggregate
  QVector<StudyMetricSpec> objectiveMetrics;    // Pareto directions
  QString objectiveMetric;                      // empty = never declare a "best"
  bool spatialComparison = false;               // run-output vs baseline-output summary
  double spatialEpsilon = 0.0;
  Result<void> validate() const;                // typed: study.spec_*
  QJsonObject toJson() const; static Result<ParameterStudySpec> fromJson(const QJsonObject&);
};

struct StudyPoint { QString pointId; QHash<QString,QString> assignments; QJsonObject parameters; quint64 seed; int replicateIndex; };
Result<QVector<StudyPoint>> sampleStudyPoints(const ParameterStudySpec&); // Grid→MatrixDescriptor; OAT/LHS→matrixCellId()
// OAT reference value per dimension = ladder[floor((stepCount-1)/2)] (documented, deterministic)

class IStudyExecutionBackend {   // port; prod adapter over ExecutionPlane
public:
  struct Outcome { bool ok; QString status, errorCode, errorMessage, executionRef;
                   QJsonObject metrics; QString outputAssetPath; };
  virtual ~IStudyExecutionBackend() = default;
  virtual Result<std::unique_ptr<StudySubmission>> submit(
      const QJsonObject& pointParams, const QString& correlationId,
      std::chrono::milliseconds timeout) = 0;
};
struct StudySubmission { virtual ~StudySubmission() = default;
  virtual IStudyExecutionBackend::Outcome wait(std::chrono::milliseconds) = 0;
  virtual void cancel() = 0; };

class StudyRunner {
public:
  StudyRunner(ExperimentStore&, IStudyExecutionBackend&);
  void setProgressCallback(std::function<void(const StudyProgress&)>);
  Result<StudyRunSummary> run(const ParameterStudySpec&, const std::atomic<bool>& cancelFlag);
};

struct SensitivityCurvePoint { double dimensionValue; qint64 runCount; double mean, populationStdDev, min, max; };
struct SensitivityCurve      { QString parameterPath, metricName; QVector<SensitivityCurvePoint> points; QString trend; };
struct UncertaintyEnvelope   { QString metricName; QJsonObject toJson() const; /* per distinct assignment: mean/σ/min/max across seed replicates */ };
struct SpatialDifferenceSummary { qint64 totalPixels, validPixels, changedPixels; double changedPercent, meanAbsDiff, maxAbsDiff, rmsDiff; /* + paths */ };

struct StudyReport { /* spec echo, status accounting (recorded/failed/cancelled/missing), run table,
                       curves, envelopes, paretoPointIds, declaredBest? (only when objectiveMetric set),
                       spatialSummaries, trendTriples (parameter→observation, mechanical), generatedAtUtc */ };
Result<void> writeStudyReport(const StudyReport&, const QString& path);  // atomic, sicnu.studyreport.v1
}
```

Machine-readable conventions: typed `Result` + dotted codes (`study.spec_invalid_dimension`, `study.budget_exceeded`, `study.experiment_missing`, `study.spatial_mismatch`, `study.run_submit_refused`, …); failure evidence stored on the run (recorder contract); report carries explicit status accounting — **no silent fallbacks anywhere**.

### Determinism contract

- Same spec (+seed) ⇒ same point set, same `pointId`s, same order (deterministic on all platforms: `std::mt19937_64` + hand-rolled bounded rejection sampling; no `std::uniform_int_distribution`).
- Double→text canonicalization for assignments is a single shared function (ladder values are textually stable).
- Replay test: sample twice, deep-compare; runner re-run on a fresh store yields identical pointIds and run-parameter documents.

## Compatibility / migration

Purely additive: one new library, one new `tests` helper (`sicnu_add_study_test`), a small refactor inside `experiment_matrix.cpp` (extract `matrixCellId`), exemplar files under `examples/studies/`, docs. No existing schema changes; experiment store stays v1 (lineage edges + existing tables only).

## Observability

`StudyProgress` callbacks (phase, pointId, counts); progress also lands in the run table rows. The report records elapsedMs and per-run timing from recorder timestamps. TaskCenter keeps its own task-level progress/log surface (unchanged).

## Security / trust boundary

- Spec JSON parsed with Qt JSON; unknown `schema_version` refused; unknown keys refused (typed) — no dynamic-code surface.
- Paths: study output dir is caller-provided; backend refuses empty/relative output dir (`study.output_dir_invalid`). No network anywhere in the module.
- Report/redaction: run environments already pass `RunEnvironment::redactSecretKeys` in the recorder (reused, not reimplemented).

## Performance budget

- Point count ≤ 1000 (`kMaxMatrixCells`); in-flight window ≤ 8 submissions; analysis is O(points × metrics + edges reads) with bounded page reads (`runsForCell` limit reused); spatial summary is O(pixels) streaming per pair, one pair per point at most; export is one bounded JSON document (curves are ≤ dims × stepCount entries).
- No module-owned threads; the runner thread blocks on windowed waits only.

## Test strategy

Light pure tests (`sicnu_add_study_test`: Catch2 + Sicnu::experiment + sicnu_study; offline, no QGIS):
- A spec: schema roundtrip, foreign-version refusal, every invalid-field typed error, budget caps.
- B samplers: grid identity == MatrixDescriptor cells (cross-oracle), OAT shape/baseline rule, LHS stratification+permutation+determinism (same seed ⇒ identical; different seed ⇒ different), budget refusals, replicate seeding.
- C runner: fake backend (scriptable outcomes; records in-flight concurrency) — happy path, per-point failure, cancel mid-run (truthful Cancelled states), submit-refusal, budget refusal, window bound never exceeded, run↔point links recorded.
- D analysis: curve slicing from OAT/grid point sets, envelope across replicates, Pareto reuse (cross-checked vs MatrixAggregator::paretoCellIds), declaredBest only with objectiveMetric.
- E spatial: buffer summaries (identical/disjoint/NaN cases), typed mismatch errors, GDAL roundtrip on synthetic GeoTIFFs (offline).
- F export: schema-versioned report, atomic write, run-row DTO projection, trend triples honest (monotone/decreasing/non-monotone), agent consumption test parses the document standalone.
- G replay/cancel/budget: determinism replay across two stores, cancellation leaves truthfully-terminated runs only, timeout path.
Full-stack e2e (`sicnu_add_test`, built once): synthetic raster → real `rs:threshold_raster` sweep through ExecutionPlane → store → report; verifies committed outputs, teaching value, agent value.

## Work packages (slices) — see slices.md for the TDD loop

A spec/schema/budget → B samplers → C runner+backend port (+prod adapter) → D analysis → E spatial → F export/UI DTO → G replay/cancel/budget hardening → e2e → review.

## Rollback / kill-switch

The module is inert unless linked/called; no registration side effects (no static registrars, no singletons). Removing the `src/study` directory + the `tests` helper + exemplar files restores baseline exactly; `experiment_matrix.cpp` refactor is behavior-preserving (single extracted function) and can be reverted independently.

## Definition of Done

1. All slices green; every core behavior has happy/invalid/boundary/persistence/deterministic-replay/compat/cancel-budget tests (teaching- vs agent-mode consistency asserted in export tests).
2. Light test suite runs offline; e2e runs once locally on the full stack and is green.
3. No new compiler warnings; no module-owned threads; no second registry/store/provenance; no silent fallbacks; typed errors everywhere.
4. Track DoD: teaching e2e scenario demonstrable; machine-readable report consumable standalone by an agent test; single-fact-source respected (identity/ledger/aggregation/Pareto reuse); offline; bounded resources; dynamic dedup re-run at PR time with union-safe rebase.
