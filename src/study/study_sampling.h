// study_sampling.h — deterministic study-point samplers (RS14-07).
//
// Turns a ParameterStudySpec into the concrete set of study points. Every
// sampler here is DETERMINISTIC: the same spec (including budget.seed)
// yields the same points in the same order on every platform. Cross-platform
// determinism forbids std::uniform_int_distribution (implementation-defined)
// — bounded values come from explicit rejection sampling over the
// standard-defined std::mt19937_64 stream.
//
// Identity contract: every point's pointId is the matrix authority's cellId
// (sicnu::experiment::matrixCellId) inside the identity space
// "study:<studyId>" — study points are matrix cells, so the MatrixLedger and
// aggregators apply verbatim. No second identity hash exists.
//
// Honesty contract: a sampled point count above budget.maxRuns is a typed
// refusal (study.budget_exceeded) — never a silent truncation.
#pragma once

#include "study/study_spec.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::study
{

/// One sampled parameter set: identity, canonical assignments, the concrete
/// operator parameters (baseParameters + swept values), and the replicate seed.
struct StudyPoint
{
    QString pointId;                    ///< matrix cellId in the "study:<studyId>" space
    QHash<QString, QString> assignments; ///< dimension path → canonical value text; "seed" → replicate seed
    QJsonObject parameters;             ///< what the runner submits (baseParameters + dimensions)
    quint64 seed = 0;                   ///< replicate seed recorded on the run
    int replicateIndex = 0;             ///< 0-based replicate of the same parameter set

    friend bool operator==( const StudyPoint &, const StudyPoint & ) = default;
};

/// The evenly spaced ladder of one dimension: stepCount values, both ends
/// inclusive, endpoints exact.
QVector<double> dimensionLadder( const ParameterDimension &dimension );

/// Canonical text of a ladder value — the assignments/identity basis.
/// Roundtrips exactly through QString::toDouble on every platform.
QString canonicalValueText( double value );

/// The matrix identity space of a study's points ("study:<studyId>").
QString studyMatrixId( const ParameterStudySpec &spec );

/// Sample the spec's points. Typed refusals:
///   study.budget_exceeded — sampled point count exceeds budget.maxRuns
///   experiment.matrix_*   — surfaced from the grid enumeration authority
Result<QVector<StudyPoint>> sampleStudyPoints( const ParameterStudySpec &spec );

} // namespace sicnu::study
