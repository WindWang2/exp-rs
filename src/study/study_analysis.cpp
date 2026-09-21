// study_analysis.cpp — projections over recorded runs (store + ledger reads).
#include "study/study_analysis.h"

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QHash>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <utility>

namespace sicnu::study
{

QJsonObject PointAggregate::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "point_id" ), pointId );
    QJsonObject assignmentsJson;
    for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
        assignmentsJson.insert( it.key(), it.value() );
    json.insert( QStringLiteral( "assignments" ), assignmentsJson );
    json.insert( QStringLiteral( "status" ), status );
    QJsonArray runsJson;
    for ( const QString &runId : runIds )
        runsJson.append( runId );
    json.insert( QStringLiteral( "run_ids" ), runsJson );
    QJsonObject metricsJson;
    for ( auto it = metrics.constBegin(); it != metrics.constEnd(); ++it )
        metricsJson.insert( it.key(), it.value().toJson() );
    json.insert( QStringLiteral( "metrics" ), metricsJson );
    return json;
}

namespace
{

experiment::MetricAggregate pooledAggregate( const QVector<double> &values )
{
    experiment::MetricAggregate aggregate;
    aggregate.runCount = values.size();
    if ( values.isEmpty() )
        return aggregate;
    double sum = 0.0;
    double mean = 0.0;
    double m2 = 0.0; // Welford
    aggregate.min = values.first();
    aggregate.max = values.first();
    for ( int i = 0; i < values.size(); ++i )
    {
        const double v = values.at( i );
        sum += v;
        aggregate.min = std::min( aggregate.min, v );
        aggregate.max = std::max( aggregate.max, v );
        const double delta = v - mean;
        mean += delta / static_cast<double>( i + 1 );
        m2 += delta * ( v - mean );
    }
    aggregate.mean = sum / static_cast<double>( values.size() );
    aggregate.populationStdDev = std::sqrt( m2 / static_cast<double>( values.size() ) );
    return aggregate;
}

int referenceLadderIndex( const ParameterDimension &dim )
{
    return ( dim.stepCount - 1 ) / 2;
}

/// Mechanical trend label of measured means (factual, never explanatory).
QString trendLabel( const QVector<CurvePoint> &points )
{
    if ( points.size() < 2 )
        return QStringLiteral( "insufficient-data" );
    bool increasing = true;
    bool decreasing = true;
    for ( int i = 1; i < points.size(); ++i )
    {
        const double previous = points.at( i - 1 ).mean;
        const double current = points.at( i ).mean;
        if ( current < previous )
            increasing = false;
        if ( current > previous )
            decreasing = false;
    }
    if ( increasing )
        return QStringLiteral( "increasing" );
    if ( decreasing )
        return QStringLiteral( "decreasing" );
    return QStringLiteral( "non-monotone" );
}

QString assignmentsKey( const QHash<QString, QString> &assignments )
{
    QStringList pairs;
    for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
        pairs.append( QStringLiteral( "%1=%2" ).arg( it.key(), it.value() ) );
    pairs.sort();
    return pairs.join( QLatin1Char( '|' ) );
}

} // namespace

StudyAnalysis analyzeStudy( experiment::ExperimentStore &store,
                            experiment::MatrixLedger &ledger, const ParameterStudySpec &spec,
                            const QVector<StudyPoint> &points )
{
    // Pass 1: per-point truth projection + run-level metric values.
    QVector<PointAggregate> pointAggregates;
    // pointId → metric name → every recorded value (for pooling in curves/bands)
    QHash<QString, QHash<QString, QVector<double>>> pointValues;
    pointAggregates.reserve( points.size() );

    for ( const StudyPoint &point : points )
    {
        PointAggregate aggregate;
        aggregate.pointId = point.pointId;
        aggregate.assignments = point.assignments;

        const QStringList linkedRuns = ledger.runsForCell( point.pointId );
        bool anyRecorded = false;
        bool anyFailed = false;
        bool anyInFlight = false;
        for ( const QString &runId : linkedRuns )
        {
            const auto run = store.runById( runId );
            if ( !run )
                continue; // dangling edge — counts as no evidence, never as success
            aggregate.runIds.append( runId );
            const dataset::RunStatus status = run.value().status();
            if ( status == dataset::RunStatus::Completed )
                anyRecorded = true;
            else if ( status == dataset::RunStatus::Failed
                      || status == dataset::RunStatus::Cancelled )
                anyFailed = true;
            else
                anyInFlight = true;
            if ( status != dataset::RunStatus::Completed )
                continue;
            const QJsonObject recorded = run.value().metrics();
            for ( const QString &metric : spec.metricNames )
            {
                const auto value = recorded.value( metric );
                if ( value.isDouble() )
                    pointValues[point.pointId][metric].append( value.toDouble() );
            }
        }
        aggregate.status =
            anyRecorded
                ? ( anyFailed ? QStringLiteral( "partial" ) : QStringLiteral( "recorded" ) )
                : ( anyFailed ? QStringLiteral( "failed" )
                              : ( anyInFlight ? QStringLiteral( "in_progress" )
                                              : QStringLiteral( "missing" ) ) );
        for ( const QString &metric : spec.metricNames )
            aggregate.metrics.insert( metric,
                                      pooledAggregate(
                                          pointValues.value( point.pointId ).value( metric ) ) );
        pointAggregates.append( aggregate );
    }

    StudyAnalysis analysis;
    analysis.points = pointAggregates;

    // Pass 2: sensitivity curves on the reference slice (not for LHS — no
    // reference slice exists in a scattered sample).
    if ( spec.strategy != SamplingStrategy::LatinHypercube )
    {
        for ( int d = 0; d < spec.dimensions.size(); ++d )
        {
            const ParameterDimension &dimension = spec.dimensions.at( d );
            const auto ladder = dimensionLadder( dimension );
            QHash<QString, QString> reference; // other dimensions at their reference value
            for ( int o = 0; o < spec.dimensions.size(); ++o )
            {
                if ( o == d )
                    continue;
                const ParameterDimension &other = spec.dimensions.at( o );
                reference.insert( other.parameterPath,
                                  canonicalValueText(
                                      dimensionLadder( other )
                                          .at( referenceLadderIndex( other ) ) ) );
            }
            for ( const QString &metric : spec.metricNames )
            {
                SensitivityCurve curve;
                curve.parameterPath = dimension.parameterPath;
                curve.metricName = metric;
                for ( const double value : ladder )
                {
                    QVector<double> pooled;
                    for ( const PointAggregate &aggregate : pointAggregates )
                    {
                        if ( aggregate.assignments.value( dimension.parameterPath )
                             != canonicalValueText( value ) )
                            continue;
                        bool onSlice = true;
                        for ( auto it = reference.constBegin();
                              it != reference.constEnd(); ++it )
                        {
                            if ( aggregate.assignments.value( it.key() ) != it.value() )
                            {
                                onSlice = false;
                                break;
                            }
                        }
                        if ( !onSlice )
                            continue;
                        pooled.append(
                            pointValues.value( aggregate.pointId ).value( metric ) );
                    }
                    if ( pooled.isEmpty() )
                        continue; // measured points only — no imputed rungs
                    CurvePoint curvePoint;
                    curvePoint.dimensionValue = value;
                    const experiment::MetricAggregate aggregate = pooledAggregate( pooled );
                    curvePoint.runCount = aggregate.runCount;
                    curvePoint.mean = aggregate.mean;
                    curvePoint.populationStdDev = aggregate.populationStdDev;
                    curvePoint.min = aggregate.min;
                    curvePoint.max = aggregate.max;
                    curve.points.append( curvePoint );
                }
                curve.trend = trendLabel( curve.points );
                analysis.curves.append( curve );
            }
        }
    }

    // Pass 3: uncertainty envelopes across seed replicates (parameter sets
    // grouped with the seed key excluded).
    {
        QHash<QString, QVector<const PointAggregate *>> groups;
        QVector<QString> groupOrder;
        for ( const PointAggregate &aggregate : pointAggregates )
        {
            QHash<QString, QString> withoutSeed = aggregate.assignments;
            withoutSeed.remove( QStringLiteral( "seed" ) );
            const QString key = assignmentsKey( withoutSeed );
            if ( !groups.contains( key ) )
                groupOrder.append( key );
            groups[key].append( &aggregate );
        }
        for ( const QString &metric : spec.metricNames )
        {
            UncertaintyEnvelope envelope;
            envelope.metricName = metric;
            for ( const QString &key : groupOrder )
            {
                QVector<double> pooled;
                QHash<QString, QString> representative;
                for ( const PointAggregate *aggregate : groups.value( key ) )
                {
                    if ( representative.isEmpty() )
                    {
                        representative = aggregate->assignments;
                        representative.remove( QStringLiteral( "seed" ) );
                    }
                    pooled.append( pointValues.value( aggregate->pointId ).value( metric ) );
                }
                UncertaintyBand band;
                band.parameterAssignments = representative;
                const experiment::MetricAggregate aggregate = pooledAggregate( pooled );
                band.runCount = aggregate.runCount;
                band.mean = aggregate.mean;
                band.populationStdDev = aggregate.populationStdDev;
                band.min = aggregate.min;
                band.max = aggregate.max;
                envelope.bands.append( band );
            }
            analysis.envelopes.append( envelope );
        }
    }

    // Pass 4: Pareto set — the matrix authority's dominance logic on the
    // declared objective metrics. Minimized metrics enter as inverted values
    // (mean → −mean, [min,max] → [−max,−min]); the authority compares means.
    if ( !spec.objectiveMetrics.isEmpty() )
    {
        experiment::MatrixAggregate aggregate;
        aggregate.matrixId = studyMatrixId( spec );
        aggregate.totalCells = pointAggregates.size();
        for ( const PointAggregate &pointAggregate : pointAggregates )
        {
            experiment::CellAggregate cell;
            cell.cellId = pointAggregate.pointId;
            cell.assignments = pointAggregate.assignments;
            cell.status = pointAggregate.status;
            cell.runIds = pointAggregate.runIds;
            for ( const StudyMetricSpec &objective : spec.objectiveMetrics )
            {
                experiment::MetricAggregate metric =
                    pointAggregate.metrics.value( objective.name );
                if ( !objective.maximize )
                {
                    const double min = metric.min;
                    const double max = metric.max;
                    metric.mean = -metric.mean;
                    metric.min = -max;
                    metric.max = -min;
                }
                cell.metrics.insert( objective.name, metric );
            }
            aggregate.cells.append( cell );
        }
        QStringList objectiveNames;
        for ( const StudyMetricSpec &objective : spec.objectiveMetrics )
            objectiveNames.append( objective.name );
        analysis.paretoPointIds =
            experiment::MatrixAggregator::paretoCellIds( aggregate, objectiveNames );
    }

    // Pass 5: declared "best" — ONLY on an explicitly declared task metric
    // with a declared direction (validation enforces the direction entry).
    if ( !spec.objectiveMetric.isEmpty() )
    {
        const auto direction = std::find_if(
            spec.objectiveMetrics.cbegin(), spec.objectiveMetrics.cend(),
            [&]( const StudyMetricSpec &m ) { return m.name == spec.objectiveMetric; } );
        const bool maximize =
            direction != spec.objectiveMetrics.cend() ? direction->maximize : true;
        const PointAggregate *best = nullptr;
        for ( const PointAggregate &aggregate : pointAggregates )
        {
            const auto metric = aggregate.metrics.constFind( spec.objectiveMetric );
            if ( metric == aggregate.metrics.constEnd() || metric->runCount == 0 )
                continue;
            if ( !best )
            {
                best = &aggregate;
                continue;
            }
            const double bestMean = best->metrics.value( spec.objectiveMetric ).mean;
            const double candidateMean = metric->mean;
            if ( maximize ? ( candidateMean > bestMean ) : ( candidateMean < bestMean ) )
                best = &aggregate;
        }
        if ( best )
        {
            analysis.declaredBestPointId = best->pointId;
            analysis.declaredBestValue =
                best->metrics.value( spec.objectiveMetric ).mean;
            analysis.declaredBestBasis =
                QStringLiteral( "declared task metric '%1' (%2): %3 recorded mean across "
                                "%4 runs — evidence, not a recommendation" )
                    .arg( spec.objectiveMetric, maximize ? QStringLiteral( "maximize" )
                                                         : QStringLiteral( "minimize" ),
                          QString::number( analysis.declaredBestValue ),
                          QString::number( best->metrics.value( spec.objectiveMetric )
                                               .runCount ) );
        }
    }

    return analysis;
}

} // namespace sicnu::study
