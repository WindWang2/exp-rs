// study_analysis.h — sensitivity curves, uncertainty envelopes, Pareto (RS14-07).
//
// Pure projection over the existing truth: reads ExperimentStore runs through
// the MatrixLedger and aggregates. Writes nothing. All aggregates are
// recomputed from recorded runs on every call — a study has NO parallel
// aggregate store.
//
// Semantics honesty:
//   - a metric a run did not report is never imputed (runCount reflects it);
//   - status vocabulary matches the matrix authority ("recorded", "missing",
//     "failed", "partial", "in_progress");
//   - the Pareto set is the matrix authority's dominance logic (reused, not
//     reimplemented); minimized objective metrics are compared on inverted
//     values (documented);
//   - a "best point" is declared ONLY when the spec names an objectiveMetric
//     with a declared direction — otherwise the report carries the Pareto
//     set and no recommendation.
#pragma once

#include "study/study_sampling.h"

#include "experiment/experiment_matrix.h"

#include <QStringList>
#include <QVector>

namespace sicnu::study
{

/// Aggregate of one metric at one point of the ladder.
struct CurvePoint
{
    double dimensionValue = 0.0;
    qint64 runCount = 0;
    double mean = 0.0;
    double populationStdDev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

/// How one metric responds to one dimension, restricted to the slice where
/// every OTHER dimension sits at its reference ladder value (the same
/// median-rule reference as the OAT sampler; grid slices follow it too).
/// Latin-hypercube studies produce no curves (no reference slice exists).
struct SensitivityCurve
{
    QString parameterPath;
    QString metricName;
    QVector<CurvePoint> points; ///< ascending by dimensionValue, measured points only
    /// Mechanical trend of the measured means: "increasing", "decreasing",
    /// "non-monotone", or "insufficient-data". A factual label — never an
    /// explanation.
    QString trend;
};

/// Spread of one metric across seed replicates of one parameter set.
struct UncertaintyBand
{
    QHash<QString, QString> parameterAssignments; ///< dimension → value text (seed excluded)
    qint64 runCount = 0;
    double mean = 0.0;
    double populationStdDev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

struct UncertaintyEnvelope
{
    QString metricName;
    QVector<UncertaintyBand> bands;
};

/// Per-point truth projection (the run-table row behind the report).
struct PointAggregate
{
    QString pointId;
    QHash<QString, QString> assignments; ///< full identity, "seed" included
    QString status;                      ///< matrix vocabulary (see header contract)
    QStringList runIds;
    QHash<QString, experiment::MetricAggregate> metrics;

    QJsonObject toJson() const;
};

struct StudyAnalysis
{
    QVector<PointAggregate> points;       ///< one per sampled study point, sample order
    QVector<SensitivityCurve> curves;     ///< per (dimension, metric) — not for LHS
    QVector<UncertaintyEnvelope> envelopes; ///< per metric, across seed replicates
    QStringList paretoPointIds;           ///< matrix-authority dominance on objective metrics
    QString declaredBestPointId;          ///< empty unless spec.objectiveMetric is declared
    double declaredBestValue = 0.0;       ///< mean of the objective metric at the declared point
    /// Human-readable declaration of what "best" meant ("" when undeclared).
    QString declaredBestBasis;
};

/// Projects the recorded truth of a study. Reads stores only.
StudyAnalysis analyzeStudy( experiment::ExperimentStore &store,
                            experiment::MatrixLedger &ledger, const ParameterStudySpec &spec,
                            const QVector<StudyPoint> &points );

} // namespace sicnu::study
