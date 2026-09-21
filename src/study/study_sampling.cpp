// study_sampling.cpp — deterministic samplers: grid, one-at-a-time, LHS.
#include "study/study_sampling.h"

#include "experiment/experiment_matrix.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace sicnu::study
{
namespace
{

Diagnostic samplingError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

/// Uniformly distributed value in [0, 1) from the standard-defined 64-bit
/// stream: 53 random bits scaled into a double. No std::uniform_*_distribution
/// anywhere — its algorithm is implementation-defined.
double unitUniform( std::mt19937_64 &rng )
{
    return static_cast<double>( rng() >> 11 ) * 0x1.0p-53;
}

/// Uniform integer in [0, bound) via rejection sampling (Lemire-free, exact).
quint64 boundedRandom( std::mt19937_64 &rng, quint64 bound )
{
    const quint64 limit = std::numeric_limits<quint64>::max() - std::numeric_limits<quint64>::max() % bound;
    for ( ;; )
    {
        const quint64 draw = rng();
        if ( draw < limit )
            return draw % bound;
    }
}

QJsonObject pointParameters( const ParameterStudySpec &spec,
                             const QHash<QString, double> &values )
{
    QJsonObject parameters = spec.baseParameters;
    for ( auto it = values.constBegin(); it != values.constEnd(); ++it )
        parameters.insert( it.key(), it.value() );
    return parameters;
}

QHash<QString, QString> pointAssignments( const QHash<QString, double> &values, quint64 seed )
{
    QHash<QString, QString> assignments;
    for ( auto it = values.constBegin(); it != values.constEnd(); ++it )
        assignments.insert( it.key(), canonicalValueText( it.value() ) );
    assignments.insert( QStringLiteral( "seed" ), QString::number( seed ) );
    return assignments;
}

StudyPoint makePoint( const ParameterStudySpec &spec, const QHash<QString, double> &values,
                      quint64 seed, int replicateIndex )
{
    StudyPoint point;
    point.assignments = pointAssignments( values, seed );
    point.pointId = experiment::matrixCellId( studyMatrixId( spec ), point.assignments );
    point.parameters = pointParameters( spec, values );
    point.seed = seed;
    point.replicateIndex = replicateIndex;
    return point;
}

/// Appends every replicate (seed = budget.seed + replicateIndex) of one
/// parameter set.
void appendReplicates( const ParameterStudySpec &spec, const QHash<QString, double> &values,
                       QVector<StudyPoint> &points )
{
    for ( int r = 0; r < spec.budget.seedReplicates; ++r )
        points.append( makePoint( spec, values, spec.budget.seed + static_cast<quint64>( r ), r ) );
}

Result<QVector<StudyPoint>> sampleGrid( const ParameterStudySpec &spec )
{
    // The grid IS a cartesian matrix: enumeration, cap enforcement and cell
    // identity come from the matrix authority, not from this module.
    experiment::MatrixDescriptor descriptor;
    descriptor.matrixId = studyMatrixId( spec );
    descriptor.experimentId = spec.experimentId;
    // Sampling-device placeholder: this descriptor enumerates cells only, it
    // is never submitted anywhere (the runner submits per-point executions).
    descriptor.workflowId = QStringLiteral( "study-sampler" );
    for ( const ParameterDimension &dim : spec.dimensions )
    {
        experiment::MatrixAxis axis;
        axis.name = dim.parameterPath;
        axis.role = experiment::AxisRole::Tag;
        const auto ladder = dimensionLadder( dim );
        for ( const double value : ladder )
            axis.values.append( canonicalValueText( value ) );
        descriptor.axes.append( axis );
    }

    const auto cells = descriptor.enumerateCells();
    if ( !cells )
        return Result<QVector<StudyPoint>>::failure( cells.diagnostics() );

    QVector<StudyPoint> points;
    points.reserve( cells.value().size() * spec.budget.seedReplicates );
    for ( const experiment::MatrixCell &cell : cells.value() )
    {
        QHash<QString, double> values;
        for ( auto it = cell.assignments.constBegin(); it != cell.assignments.constEnd(); ++it )
            values.insert( it.key(), it.value().toDouble() );
        appendReplicates( spec, values, points );
    }
    return Result<QVector<StudyPoint>>::success( points );
}

int referenceLadderIndex( const ParameterDimension &dim )
{
    // Median-ish reference: lower-middle for even ladders. Deterministic and
    // explainable ("the middle of the range").
    return ( dim.stepCount - 1 ) / 2;
}

QVector<StudyPoint> sampleOneAtATime( const ParameterStudySpec &spec )
{
    QVector<QVector<double>> ladders;
    ladders.reserve( spec.dimensions.size() );
    for ( const ParameterDimension &dim : spec.dimensions )
        ladders.append( dimensionLadder( dim ) );

    // Baseline: every dimension at its reference ladder value.
    QHash<QString, double> baseline;
    for ( int d = 0; d < spec.dimensions.size(); ++d )
        baseline.insert( spec.dimensions.at( d ).parameterPath,
                         ladders.at( d ).at( referenceLadderIndex( spec.dimensions.at( d ) ) ) );

    QVector<StudyPoint> points;
    appendReplicates( spec, baseline, points );
    // Vary ONE dimension at a time, skipping the reference value (the
    // baseline already carries it).
    for ( int d = 0; d < spec.dimensions.size(); ++d )
    {
        const QString &path = spec.dimensions.at( d ).parameterPath;
        const int refIndex = referenceLadderIndex( spec.dimensions.at( d ) );
        for ( int i = 0; i < ladders.at( d ).size(); ++i )
        {
            if ( i == refIndex )
                continue;
            QHash<QString, double> values = baseline;
            values.insert( path, ladders.at( d ).at( i ) );
            appendReplicates( spec, values, points );
        }
    }
    return points;
}

QVector<StudyPoint> sampleLatinHypercube( const ParameterStudySpec &spec )
{
    // Controlled scale: N parameter sets, each replicated R times, so the
    // total stays inside budget.maxRuns.
    const quint64 replicateCount = static_cast<quint64>( spec.budget.seedReplicates );
    const int n = std::max<qint64>(
        1, static_cast<qint64>( spec.budget.maxRuns / replicateCount ) );

    // Deterministic RNG stream with a FIXED draw order: per dimension, the
    // stratum permutation first (Fisher-Yates over rejection-sampled bounds),
    // then the per-point within-stratum offsets. Same seed ⇒ same draws.
    std::mt19937_64 rng( spec.budget.seed );

    QVector<QVector<int>> strataPerDimension; // [d][pointIndex] → stratum
    strataPerDimension.reserve( spec.dimensions.size() );
    for ( const ParameterDimension &dim : spec.dimensions )
    {
        Q_UNUSED( dim );
        QVector<int> strata( n );
        for ( int i = 0; i < n; ++i )
            strata[i] = i;
        for ( int i = n - 1; i > 0; --i )
        {
            const quint64 j = boundedRandom( rng, static_cast<quint64>( i ) + 1 );
            std::swap( strata[i], strata[static_cast<int>( j )] );
        }
        strataPerDimension.append( strata );
    }

    QVector<QVector<double>> offsetsPerDimension; // [d][pointIndex] → u in [0,1)
    offsetsPerDimension.reserve( spec.dimensions.size() );
    for ( const ParameterDimension &dim : spec.dimensions )
    {
        Q_UNUSED( dim );
        QVector<double> offsets( n );
        for ( int i = 0; i < n; ++i )
            offsets[i] = unitUniform( rng );
        offsetsPerDimension.append( offsets );
    }

    QVector<StudyPoint> points;
    points.reserve( n * static_cast<int>( replicateCount ) );
    for ( int i = 0; i < n; ++i )
    {
        QHash<QString, double> values;
        for ( int d = 0; d < spec.dimensions.size(); ++d )
        {
            const ParameterDimension &dim = spec.dimensions.at( d );
            // Stratified, jittered sample of [minValue, maxValue]: exactly one
            // value per stratum per dimension.
            const double unit = ( static_cast<double>( strataPerDimension.at( d ).at( i ) )
                                  + offsetsPerDimension.at( d ).at( i ) )
                                / static_cast<double>( n );
            values.insert( dim.parameterPath,
                           dim.minValue + unit * ( dim.maxValue - dim.minValue ) );
        }
        appendReplicates( spec, values, points );
    }
    return points;
}

} // namespace

QVector<double> dimensionLadder( const ParameterDimension &dimension )
{
    QVector<double> ladder;
    ladder.reserve( dimension.stepCount );
    const double span = dimension.maxValue - dimension.minValue;
    const double step = span / static_cast<double>( dimension.stepCount - 1 );
    for ( int i = 0; i < dimension.stepCount; ++i )
    {
        if ( i == dimension.stepCount - 1 )
        {
            // The upper end is exact — no accumulated rounding.
            ladder.append( dimension.maxValue );
            break;
        }
        ladder.append( dimension.minValue + static_cast<double>( i ) * step );
    }
    return ladder;
}

QString canonicalValueText( double value )
{
    // 17 significant digits: every double roundtrips exactly.
    return QString::number( value, 'g', 17 );
}

QString studyMatrixId( const ParameterStudySpec &spec )
{
    return QStringLiteral( "study:" ) + spec.studyId;
}

Result<QVector<StudyPoint>> sampleStudyPoints( const ParameterStudySpec &spec )
{
    const auto validated = spec.validate();
    if ( !validated )
        return Result<QVector<StudyPoint>>::failure( validated.diagnostics() );

    if ( spec.strategy == SamplingStrategy::Grid || spec.strategy == SamplingStrategy::OneAtATime )
    {
        // Point count is knowable up front — refuse over-budget studies
        // instead of sampling and truncating.
        qint64 parameterSets = 0;
        if ( spec.strategy == SamplingStrategy::Grid )
        {
            parameterSets = 1;
            for ( const ParameterDimension &dim : spec.dimensions )
                parameterSets *= dim.stepCount;
        }
        else
        {
            parameterSets = 1;
            for ( const ParameterDimension &dim : spec.dimensions )
                parameterSets += dim.stepCount - 1;
        }
        const qint64 total = parameterSets * spec.budget.seedReplicates;
        if ( total > spec.budget.maxRuns )
            return Result<QVector<StudyPoint>>::failure( samplingError(
                QStringLiteral( "study.budget_exceeded" ),
                QStringLiteral( "study samples %1 points but budget.maxRuns is %2 — "
                                 "narrow the study or raise the budget" )
                    .arg( total )
                    .arg( spec.budget.maxRuns ) ) );
    }

    QVector<StudyPoint> points;
    switch ( spec.strategy )
    {
        case SamplingStrategy::Grid:
        {
            auto grid = sampleGrid( spec );
            if ( !grid )
                return Result<QVector<StudyPoint>>::failure( grid.diagnostics() );
            points = grid.take();
            break;
        }
        case SamplingStrategy::OneAtATime:
            points = sampleOneAtATime( spec );
            break;
        case SamplingStrategy::LatinHypercube:
            points = sampleLatinHypercube( spec );
            break;
    }
    if ( points.size() > spec.budget.maxRuns )
        return Result<QVector<StudyPoint>>::failure( samplingError(
            QStringLiteral( "study.budget_exceeded" ),
            QStringLiteral( "sampler produced %1 points but budget.maxRuns is %2" )
                .arg( points.size() )
                .arg( spec.budget.maxRuns ) ) );
    return Result<QVector<StudyPoint>>::success( points );
}

} // namespace sicnu::study
