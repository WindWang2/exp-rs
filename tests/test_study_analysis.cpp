// test_study_analysis.cpp — analysis projection contracts (RS14-07 Slice D).
//
// The store is populated DIRECTLY through the recorder (no runner): analysis
// is a pure read model, so its oracles are hand-computed aggregates over
// synthetic recorded runs.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "study/study_analysis.h"

#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/run_recorder.h"

#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::study;

namespace
{

struct Fixture
{
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    sicnu::experiment::MatrixLedger ledger{ store };
    sicnu::experiment::ExperimentRunRecorder recorder{ store };

    Fixture()
    {
        REQUIRE( dir.isValid() );
        REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
    }

    void ensureExperiment( const QString &experimentId )
    {
        sicnu::experiment::Experiment experiment;
        experiment.setExperimentId( experimentId );
        experiment.setName( QStringLiteral( "analysis-fixture" ) );
        experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( store.upsertExperiment( experiment ).has_value() );
    }

    /// Records one completed run with the given metrics and links it.
    QString recordRun( const ParameterStudySpec &spec, const StudyPoint &point,
                       const QJsonObject &metrics, quint64 seed )
    {
        sicnu::experiment::RunStartRequest request;
        request.experimentId = spec.experimentId;
        request.algorithmId = spec.algorithmId;
        request.parameters = point.parameters;
        request.seed = seed;
        request.executionRef = QStringLiteral( "fixture-%1-%2" ).arg( point.pointId ).arg( seed );
        const auto runId = recorder.startRun( request );
        REQUIRE( runId.has_value() );
        REQUIRE( recorder.markSucceeded( runId.value(), {}, metrics ).has_value() );
        REQUIRE( ledger.link( point.pointId, runId.value() ).has_value() );
        return runId.value();
    }
};

ParameterStudySpec oatSpec( int replicates = 1 )
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "analysis-contract" );
    spec.experimentId = QStringLiteral( "exp-analysis" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.strategy = SamplingStrategy::OneAtATime;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.0;
    dim.maxValue = 1.0;
    dim.stepCount = 5;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = 100;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 1000;
    spec.budget.seedReplicates = replicates;
    spec.budget.seed = 3;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    return spec;
}

const StudyPoint &pointWithThreshold( const QVector<StudyPoint> &points, double threshold )
{
    static StudyPoint none;
    const QString wanted = canonicalValueText( threshold );
    for ( const StudyPoint &point : points )
        if ( point.assignments.value( QStringLiteral( "threshold" ) ) == wanted )
            return point;
    return none;
}

} // namespace

TEST_CASE( "analysis projects recorded truth with hand-computed aggregates",
           "[study][analysis]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    const auto spec = oatSpec();
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 5 ); // baseline + 4 variations

    // Metric oracle: maskedPercent = 100 * threshold (recorded, not computed).
    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "maskedPercent" ), threshold * 100.0 );
        fix.recordRun( spec, point, metrics, point.seed );
    }

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    REQUIRE( analysis.points.size() == 5 );
    for ( const PointAggregate &aggregate : analysis.points )
        REQUIRE( aggregate.status == QStringLiteral( "recorded" ) );

    // One curve per (dimension, metric) with 5 measured rungs.
    REQUIRE( analysis.curves.size() == 1 );
    const SensitivityCurve &curve = analysis.curves.first();
    REQUIRE( curve.parameterPath == QStringLiteral( "threshold" ) );
    REQUIRE( curve.metricName == QStringLiteral( "maskedPercent" ) );
    REQUIRE( curve.points.size() == 5 );
    REQUIRE( curve.trend == QStringLiteral( "increasing" ) );
    for ( int i = 0; i < 5; ++i )
    {
        REQUIRE( curve.points.at( i ).dimensionValue == Catch::Approx( 0.25 * i ) );
        REQUIRE( curve.points.at( i ).runCount == 1 );
        REQUIRE( curve.points.at( i ).mean == Catch::Approx( 25.0 * i ) );
    }

    // No objective metrics declared → no Pareto, no declared best.
    REQUIRE( analysis.paretoPointIds.isEmpty() );
    REQUIRE( analysis.declaredBestPointId.isEmpty() );
    REQUIRE( analysis.declaredBestBasis.isEmpty() );
}

TEST_CASE( "uncertainty envelope pools seed replicates of one parameter set",
           "[study][analysis][envelope]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    const auto spec = oatSpec( /*replicates=*/2 );
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 10 );

    // Baseline set (threshold 0.5): replicate seeds 3 and 4 record 40 and 60.
    const StudyPoint &baseline = points.at( 0 );
    REQUIRE( baseline.parameters.value( QStringLiteral( "threshold" ) ).toDouble()
             == 0.5 );
    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        const double value = ( threshold == 0.5 )
            ? ( point.seed == 3 ? 40.0 : 60.0 )
            : threshold * 100.0;
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "maskedPercent" ), value );
        fix.recordRun( spec, point, metrics, point.seed );
    }

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    REQUIRE( analysis.envelopes.size() == 1 );
    const UncertaintyEnvelope &envelope = analysis.envelopes.first();
    REQUIRE( envelope.bands.size() == 5 );
    // Bands are keyed by the parameter set WITHOUT the seed.
    bool sawBaselineBand = false;
    for ( const UncertaintyBand &band : envelope.bands )
    {
        REQUIRE( !band.parameterAssignments.contains( QStringLiteral( "seed" ) ) );
        if ( band.parameterAssignments.value( QStringLiteral( "threshold" ) )
             == canonicalValueText( 0.5 ) )
        {
            sawBaselineBand = true;
            REQUIRE( band.runCount == 2 );
            REQUIRE( band.mean == Catch::Approx( 50.0 ) );
            REQUIRE( band.min == Catch::Approx( 40.0 ) );
            REQUIRE( band.max == Catch::Approx( 60.0 ) );
            REQUIRE( band.populationStdDev > 0.0 );
        }
    }
    REQUIRE( sawBaselineBand );
}

TEST_CASE( "failures are truthfully classified, never aggregated as data",
           "[study][analysis]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    auto spec = oatSpec();
    spec.objectiveMetric = QStringLiteral( "maskedPercent" );
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "maskedPercent" ), true } );
    const auto points = sampleStudyPoints( spec ).value();

    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        if ( threshold == 0.5 )
        {
            // The baseline run failed — typed error evidence under metrics["error"].
            const auto runId = fix.recorder.startRun( [&] {
                sicnu::experiment::RunStartRequest request;
                request.experimentId = spec.experimentId;
                request.algorithmId = spec.algorithmId;
                request.parameters = point.parameters;
                request.seed = point.seed;
                request.executionRef = QStringLiteral( "fixture-failed" );
                return request;
            }() );
            REQUIRE( runId.has_value() );
            REQUIRE( fix.recorder
                         .markFailed( runId.value(), QStringLiteral( "study.operator_failed" ),
                                      QStringLiteral( "boom" ) )
                         .has_value() );
            REQUIRE( fix.ledger.link( point.pointId, runId.value() ).has_value() );
            continue;
        }
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "maskedPercent" ), threshold * 100.0 );
        fix.recordRun( spec, point, metrics, point.seed );
    }

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    const StudyPoint &baseline = pointWithThreshold( points, 0.5 );
    const StudyPoint &low = pointWithThreshold( points, 0.0 );
    const StudyPoint &high = pointWithThreshold( points, 1.0 );

    bool sawFailed = false;
    for ( const PointAggregate &aggregate : analysis.points )
    {
        if ( aggregate.pointId == baseline.pointId )
        {
            REQUIRE( aggregate.status == QStringLiteral( "failed" ) );
            REQUIRE( aggregate.metrics.value( QStringLiteral( "maskedPercent" ) ).runCount
                     == 0 ); // a failed run contributes NO data
            sawFailed = true;
        }
    }
    REQUIRE( sawFailed );

    // The curve skips the unmeasured rung (no imputation) and stays
    // increasing over the remaining measured ones.
    const SensitivityCurve &curve = analysis.curves.first();
    REQUIRE( curve.points.size() == 4 );
    REQUIRE( curve.trend == QStringLiteral( "increasing" ) );

    // Declared best: only measured candidates; 1.0 wins (100.0).
    REQUIRE( analysis.declaredBestPointId == high.pointId );
    REQUIRE( analysis.declaredBestValue == Catch::Approx( 100.0 ) );
    REQUIRE( analysis.paretoPointIds.size() == 1 );
    REQUIRE( analysis.paretoPointIds.first() == high.pointId );
    Q_UNUSED( low );
}

TEST_CASE( "missing runs are reported as missing, never silently dropped",
           "[study][analysis]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    const auto spec = oatSpec();
    const auto points = sampleStudyPoints( spec ).value();

    // Record only the first point; everything else stays unexecuted.
    QJsonObject metrics;
    metrics.insert( QStringLiteral( "maskedPercent" ), 50.0 );
    fix.recordRun( spec, points.first(), metrics, points.first().seed );

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    qint64 missing = 0;
    qint64 recorded = 0;
    for ( const PointAggregate &aggregate : analysis.points )
    {
        if ( aggregate.status == QStringLiteral( "missing" ) )
            ++missing;
        if ( aggregate.status == QStringLiteral( "recorded" ) )
            ++recorded;
    }
    REQUIRE( recorded == 1 );
    REQUIRE( missing == 4 );
    // The curve carries the single measured rung only.
    REQUIRE( analysis.curves.first().points.size() == 1 );
    REQUIRE( analysis.curves.first().trend == QStringLiteral( "insufficient-data" ) );
}

TEST_CASE( "pareto dominance handles mixed objective directions",
           "[study][analysis][pareto]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "pareto-contract" );
    spec.experimentId = QStringLiteral( "exp-analysis" );
    spec.algorithmId = QStringLiteral( "rs:supervised_classification" );
    spec.strategy = SamplingStrategy::Grid;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "k" );
    dim.minValue = 2.0;
    dim.maxValue = 4.0;
    dim.stepCount = 3;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = 10;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 1000;
    spec.budget.seedReplicates = 1;
    spec.metricNames.append( QStringLiteral( "overallAccuracy" ) );
    spec.metricNames.append( QStringLiteral( "rejectThreshold" ) );
    // maximize accuracy, minimize the rejection threshold
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "overallAccuracy" ), true } );
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "rejectThreshold" ), false } );

    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 3 );

    // Hand-computed dominance on means (accuracy ↑ is better, threshold ↓ is better):
    //   k=2: acc 70, thr 0.5
    //   k=3: acc 80, thr 0.5  (dominates k=2: accuracy strictly better, threshold equal)
    //   k=4: acc 90, thr 0.7  (not dominated: strictly best accuracy)
    // Expected Pareto set: {k=3, k=4}.
    const double accuracy[3] = { 70.0, 80.0, 90.0 };
    const double threshold[3] = { 0.5, 0.5, 0.7 };
    for ( int i = 0; i < 3; ++i )
    {
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "overallAccuracy" ), accuracy[i] );
        metrics.insert( QStringLiteral( "rejectThreshold" ), threshold[i] );
        fix.recordRun( spec, points.at( i ), metrics, points.at( i ).seed );
    }

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    QStringList pareto = analysis.paretoPointIds;
    pareto.sort();
    REQUIRE( pareto.size() == 2 );
    REQUIRE( pareto.contains( points.at( 1 ).pointId ) );
    REQUIRE( pareto.contains( points.at( 2 ).pointId ) );
}

TEST_CASE( "analysis keeps cancelled as its own point status", "[study][analysis]" )
{
    Fixture fix;
    fix.ensureExperiment( QStringLiteral( "exp-analysis" ) );
    const auto spec = oatSpec();
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 5 );

    // One point's only run is cancelled by the abort drain (the recorder's
    // truthful lifecycle records Cancelled). The report has a dedicated
    // cancelled bucket fed from point statuses — a projection that folds
    // Cancelled into "failed" leaves that bucket dead code while the same
    // document's stopped_reason says "cancelled".
    const StudyPoint &point = points.first();
    sicnu::experiment::RunStartRequest request;
    request.experimentId = spec.experimentId;
    request.algorithmId = spec.algorithmId;
    request.parameters = point.parameters;
    request.seed = point.seed;
    request.executionRef = QStringLiteral( "fixture-%1-cancelled" ).arg( point.pointId );
    const auto runId = fix.recorder.startRun( request );
    REQUIRE( runId.has_value() );
    REQUIRE( fix.recorder
                 .markCancelled( runId.value(), QStringLiteral( "study aborted" ) )
                 .has_value() );
    REQUIRE( fix.ledger.link( point.pointId, runId.value() ).has_value() );

    const StudyAnalysis analysis = analyzeStudy( fix.store, fix.ledger, spec, points );
    REQUIRE( analysis.points.size() == points.size() );
    bool checked = false;
    for ( const PointAggregate &aggregate : analysis.points )
    {
        if ( aggregate.pointId != point.pointId )
            continue;
        REQUIRE( aggregate.status == QStringLiteral( "cancelled" ) );
        REQUIRE( aggregate.metrics.value( QStringLiteral( "maskedPercent" ) ).runCount
                 == 0 ); // a cancelled run contributes NO data
        checked = true;
    }
    REQUIRE( checked );
}
