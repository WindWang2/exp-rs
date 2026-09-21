// study_spec.h — ParameterStudySpec (RS14-07 Parameter Sensitivity & Uncertainty Studio).
//
// A study spec DESCRIBES a bounded parameter sensitivity experiment: the
// parameter space, the sampling strategy, the run budget and the metrics to
// collect. It is a value object — it never executes anything. Execution goes
// through the existing ExecutionPlane/TaskCenter spine (study_runner.h); run
// truth is recorded in the existing ExperimentStore (single fact source).
//
// Honesty contracts:
//   - the document is versioned; readers refuse foreign versions instead of
//     guessing (house convention, cf. kEvidenceSchemaVersion);
//   - unknown fields are REFUSED, not ignored — a mistyped spec must never
//     silently change the meaning of a study;
//   - budget overflow is a typed refusal, never a silent truncation;
//   - the module declares no "best parameters" unless the spec explicitly
//     names an objective metric (the report may then flag the front-runner
//     as evidence, never as a recommendation).
#pragma once

#include "data/data_result.h"
#include "experiment/experiment_matrix.h" // kMaxMatrixCells — the sweep cap authority

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::study
{

using sicnu::data::Diagnostic;
using sicnu::data::Result;

/// Bump on any breaking change to the serialized spec document.
inline constexpr int kStudySpecSchemaVersion = 1;

/// Hard upper bound of the runner's in-flight submission window. The window
/// is a submission bound only — admission/concurrency stay with TaskCenter.
inline constexpr int kStudyMaxInFlightBound = 8;

/// Where values land on the sampler's ladder.
enum class SamplingStrategy
{
    Grid,           ///< full cartesian product over dimension ladders
    OneAtATime,     ///< baseline point + single-dimension variations
    LatinHypercube, ///< seeded, stratified small sample (controlled scale)
};

QString samplingStrategyToString( SamplingStrategy strategy );
std::optional<SamplingStrategy> samplingStrategyFromString( const QString &text );

/// One swept operator parameter: a numeric ladder over [minValue, maxValue]
/// with @p stepCount values including both ends. Flat parameter keys only in
/// schema v1 (operator parameters are flat JSON keys).
struct ParameterDimension
{
    QString parameterPath; ///< operator parameter key, e.g. "threshold"
    double minValue = 0.0;
    double maxValue = 0.0;
    int stepCount = 2; ///< ladder value count, >= 2

    friend bool operator==( const ParameterDimension &, const ParameterDimension & ) = default;
};

/// Resource & scale budget. Enforced with typed refusals — the runner never
/// truncates a study silently and never queues without bound.
struct StudyBudget
{
    qint64 maxRuns = 100;          ///< sampled points cap (<= kMaxMatrixCells)
    int maxInFlight = 2;           ///< in-flight submission window (1..kStudyMaxInFlightBound)
    qint64 perRunTimeoutMs = 600000; ///< per-point execution deadline (cancels on expiry)
    int seedReplicates = 1;        ///< replicate points per sampled parameter set (>= 1)
    quint64 seed = 0;              ///< sampler + replicate seed base (determinism)

    friend bool operator==( const StudyBudget &, const StudyBudget & ) = default;
};

/// One metric with a declared optimization direction (Pareto comparisons).
struct StudyMetricSpec
{
    QString name;
    bool maximize = true;

    friend bool operator==( const StudyMetricSpec &, const StudyMetricSpec & ) = default;
};

struct ParameterStudySpec
{
    QString studyId;       ///< caller-assigned, stable identity of the study
    QString experimentId;  ///< experiment the runs record into (created if missing)
    QString algorithmId;   ///< the operator each point submits, e.g. "rs:threshold_raster"
    QJsonObject baseParameters; ///< parameters shared by every point ("output" is runner-managed)

    SamplingStrategy strategy = SamplingStrategy::Grid;
    QVector<ParameterDimension> dimensions; ///< >= 1
    StudyBudget budget;
    QStringList metricNames; ///< operator payload keys aggregated per point (>= 1)

    /// Pareto directions; every name must appear in @p metricNames.
    QVector<StudyMetricSpec> objectiveMetrics;
    /// Single declared task metric. EMPTY by contract: without it, no consumer
    /// (student UI or agent) may present any point as "the best".
    QString objectiveMetric;

    bool spatialComparison = false; ///< summarize each run output vs the baseline output
    double spatialEpsilon = 0.0;    ///< |a-b| tolerance for "changed" pixels (>= 0)

    Result<void> validate() const;
    QJsonObject toJson() const;
    static Result<ParameterStudySpec> fromJson( const QJsonObject &json );

    friend bool operator==( const ParameterStudySpec &, const ParameterStudySpec & ) = default;
};

/// Typed diagnostic factory for this module (dotted "study.*" codes).
Diagnostic studyError( const QString &code, const QString &message );

} // namespace sicnu::study
