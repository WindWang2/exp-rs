// test_study_export.cpp — `sicnu.studyreport.v1` contracts (RS14-07 Slice F).
//
// The report must serve BOTH audiences: a student reads run table + trend
// triples; an agent parses the standalone JSON (the consumption test uses
// only QJsonDocument — no study types) and finds versioned, explicit evidence.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "study/study_export.h"

#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/run_recorder.h"

#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <limits>

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

    QString recordRun( const ParameterStudySpec &spec, const StudyPoint &point,
                       const QJsonObject &metrics )
    {
        sicnu::experiment::RunStartRequest request;
        request.experimentId = spec.experimentId;
        request.algorithmId = spec.algorithmId;
        request.parameters = point.parameters;
        request.parameters.insert(
            QStringLiteral( "output" ),
            dir.filePath( point.pointId + QStringLiteral( "/output.tif" ) ) );
        request.seed = point.seed;
        request.executionRef = QStringLiteral( "export-%1" ).arg( point.pointId );
        const auto runId = recorder.startRun( request );
        REQUIRE( runId.has_value() );
        REQUIRE( recorder.markSucceeded( runId.value(), {}, metrics ).has_value() );
        REQUIRE( ledger.link( point.pointId, runId.value() ).has_value() );
        return runId.value();
    }
};

ParameterStudySpec specWithObjective()
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "export-contract" );
    spec.experimentId = QStringLiteral( "exp-export" );
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
    spec.budget.seedReplicates = 1;
    spec.budget.seed = 5;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    spec.objectiveMetric = QStringLiteral( "maskedPercent" );
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "maskedPercent" ), false } );
    return spec;
}

QDateTime frozenTime()
{
    return QDateTime( QDate( 2026, 9, 22 ), QTime( 12, 0, 0 ), QTimeZone::utc() );
}

} // namespace

TEST_CASE( "study report carries the full teaching + agent evidence",
           "[study][export]" )
{
    Fixture fix;
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-export" ) );
    experiment.setName( QStringLiteral( "export" ) );
    experiment.setCreatedAtUtc( frozenTime() );
    REQUIRE( fix.store.upsertExperiment( experiment ).has_value() );

    const auto spec = specWithObjective();
    const auto points = sampleStudyPoints( spec ).value();
    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "maskedPercent" ), threshold * 100.0 );
        fix.recordRun( spec, point, metrics );
    }

    const StudyRunSummary runnerSummary{ /*experimentId*/ QStringLiteral( "exp-export" ),
                                         /*studyId*/ spec.studyId,
                                         /*totalPoints*/ 5, /*recordedCount*/ 5,
                                         /*failedCount*/ 0, /*cancelledCount*/ 0,
                                         /*runIds*/ {}, /*stoppedReason*/ QString(),
                                         /*elapsedMs*/ 1234 };
    const StudyReport report =
        buildStudyReport( fix.store, fix.ledger, spec, points, {}, &runnerSummary,
                          frozenTime() );

    // Run table: one row per point, all recorded.
    REQUIRE( report.runTable.size() == 5 );
    REQUIRE( report.recordedCount == 5 );
    REQUIRE( report.failedCount == 0 );
    REQUIRE( report.missingCount == 0 );

    // Teaching narrative: one factual triple, increasing, with numbers.
    REQUIRE( report.narrative.size() == 1 );
    REQUIRE( report.narrative.first().trend == QStringLiteral( "increasing" ) );
    REQUIRE( report.narrative.first().observation.contains( QStringLiteral( "rise" ) ) );

    // Declared best exists (minimize 100*threshold → threshold 0 wins).
    REQUIRE( report.declaredBest.contains( QStringLiteral( "point_id" ) ) );
    REQUIRE( report.declaredBest.value( QStringLiteral( "value" ) ).toDouble()
             == Catch::Approx( 0.0 ) );
    REQUIRE( report.declaredBest.value( QStringLiteral( "basis" ) ).toString().contains(
        QStringLiteral( "not a recommendation" ) ) );

    // Roundtrip through versioned JSON.
    const auto json = report.toJson();
    const auto parsed = StudyReport::fromJson( json );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed.value().studyId == report.studyId );
    REQUIRE( parsed.value().runTable == report.runTable );
    REQUIRE( parsed.value().curves.size() == report.curves.size() );
    REQUIRE( parsed.value().curves.first().trend == report.curves.first().trend );
    REQUIRE( parsed.value().narrative == report.narrative );
    REQUIRE( parsed.value().declaredBest == report.declaredBest );
    REQUIRE( parsed.value().recordedCount == report.recordedCount );
}

TEST_CASE( "failed runs appear in the table with their typed evidence",
           "[study][export]" )
{
    Fixture fix;
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-export" ) );
    experiment.setName( QStringLiteral( "export" ) );
    experiment.setCreatedAtUtc( frozenTime() );
    REQUIRE( fix.store.upsertExperiment( experiment ).has_value() );

    const auto spec = specWithObjective();
    const auto points = sampleStudyPoints( spec ).value();
    const auto pointsSampled = sampleStudyPoints( spec );
    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        if ( threshold == 0.5 )
        {
            sicnu::experiment::RunStartRequest request;
            request.experimentId = spec.experimentId;
            request.algorithmId = spec.algorithmId;
            request.parameters = point.parameters;
            request.seed = point.seed;
            request.executionRef = QStringLiteral( "export-failed" );
            const auto runId = fix.recorder.startRun( request );
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
        fix.recordRun( spec, point, metrics );
    }

    const StudyReport report = buildStudyReport( fix.store, fix.ledger, spec, points, {},
                                                 nullptr, frozenTime() );
    REQUIRE( report.recordedCount == 4 );
    REQUIRE( report.failedCount == 1 );
    bool sawFailedRow = false;
    for ( const StudyRunRow &row : report.runTable )
    {
        if ( row.status == QStringLiteral( "failed" ) )
        {
            sawFailedRow = true;
            REQUIRE( row.errorSummary.contains( QStringLiteral( "study.operator_failed" ) ) );
            REQUIRE( row.errorSummary.contains( QStringLiteral( "boom" ) ) );
        }
    }
    REQUIRE( sawFailedRow );
}

TEST_CASE( "report document is agent-consumable standalone", "[study][export][agent]" )
{
    Fixture fix;
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-export" ) );
    experiment.setName( QStringLiteral( "export" ) );
    experiment.setCreatedAtUtc( frozenTime() );
    REQUIRE( fix.store.upsertExperiment( experiment ).has_value() );

    const auto spec = specWithObjective();
    const auto points = sampleStudyPoints( spec ).value();
    for ( const StudyPoint &point : points )
    {
        const double threshold =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble();
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "maskedPercent" ), threshold * 100.0 );
        fix.recordRun( spec, point, metrics );
    }
    const StudyReport report =
        buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr, frozenTime() );
    const QString path = fix.dir.filePath( QStringLiteral( "study.sicnu-studyreport.json" ) );
    REQUIRE( writeStudyReport( report, path ).has_value() );

    // The agent reads the FILE with nothing but a JSON parser.
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QJsonDocument document = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( document.isObject() );
    const QJsonObject json = document.object();
    REQUIRE( json.value( QStringLiteral( "document_type" ) ).toString()
             == QStringLiteral( "sicnu.studyreport.v1" ) );
    REQUIRE( json.value( QStringLiteral( "schema_version" ) ).toInt()
             == kStudyReportSchemaVersion );
    REQUIRE( json.value( QStringLiteral( "algorithm_id" ) ).toString()
             == QStringLiteral( "rs:threshold_raster" ) );

    const QJsonArray runTable = json.value( QStringLiteral( "run_table" ) ).toArray();
    REQUIRE( runTable.size() == 5 );
    const QJsonArray curves = json.value( QStringLiteral( "curves" ) ).toArray();
    REQUIRE( curves.size() == 1 );
    REQUIRE( curves.at( 0 ).toObject().value( QStringLiteral( "trend" ) ).toString()
             == QStringLiteral( "increasing" ) );
    REQUIRE( json.value( QStringLiteral( "declared_best" ) ).toObject().contains(
        QStringLiteral( "point_id" ) ) );
    REQUIRE( json.contains( QStringLiteral( "usage_notes" ) ) );
    const QJsonObject accounting =
        json.value( QStringLiteral( "status_accounting" ) ).toObject();
    REQUIRE( accounting.value( QStringLiteral( "recorded" ) ).toInt() == 5 );
    REQUIRE( accounting.value( QStringLiteral( "total_points" ) ).toInt() == 5 );

    // Byte-stable persistence for identical inputs (deterministic document).
    const QString secondPath =
        fix.dir.filePath( QStringLiteral( "study-again.sicnu-studyreport.json" ) );
    REQUIRE( writeStudyReport( report, secondPath ).has_value() );
    QFile first( path );
    QFile second( secondPath );
    REQUIRE( first.open( QIODevice::ReadOnly ) );
    REQUIRE( second.open( QIODevice::ReadOnly ) );
    REQUIRE( first.readAll() == second.readAll() );
}

TEST_CASE( "foreign report versions are refused", "[study][export]" )
{
    auto json = StudyReport{}.toJson();
    json.insert( QStringLiteral( "schema_version" ), 42 );
    REQUIRE( !StudyReport::fromJson( json ).has_value() );
}

TEST_CASE( "unwritable report paths are typed failures", "[study][export]" )
{
    Fixture fix;
    const auto spec = specWithObjective();
    const auto points = sampleStudyPoints( spec ).value();
    const StudyReport report =
        buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr, frozenTime() );
    const auto result = writeStudyReport( report, QStringLiteral( "/proc/definitely/not/writable" ) );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code
             == QStringLiteral( "study.report_write_failed" ) );
}

namespace
{

StudyRunRow recordedRowWithSeed( quint64 seed )
{
    StudyRunRow row;
    row.pointId = QStringLiteral( "p0000" );
    row.replicateIndex = 0;
    row.runId = QStringLiteral( "run-0" );
    row.status = QStringLiteral( "recorded" );
    row.seed = seed;
    return row;
}

void recordScaledRun( sicnu::experiment::ExperimentRunRecorder &recorder,
                      sicnu::experiment::MatrixLedger &ledger, const ParameterStudySpec &spec,
                      const StudyPoint &point, const QTemporaryDir &dir )
{
    sicnu::experiment::RunStartRequest request;
    request.experimentId = spec.experimentId;
    request.algorithmId = spec.algorithmId;
    request.parameters = point.parameters;
    request.parameters.insert(
        QStringLiteral( "output" ),
        dir.filePath( point.pointId + QStringLiteral( "/output.tif" ) ) );
    request.seed = point.seed;
    request.executionRef = QStringLiteral( "scaling-%1" ).arg( point.pointId );
    const auto runId = recorder.startRun( request );
    REQUIRE( runId.has_value() );
    REQUIRE( recorder.markSucceeded( runId.value(), {}, QJsonObject{} ).has_value() );
    REQUIRE( ledger.link( point.pointId, runId.value() ).has_value() );
}

} // namespace

TEST_CASE( "64-bit seeds round-trip the report JSON without double rounding",
           "[study][export][seed]" )
{
    // budget.seed is a quint64 and the report is the record other tools
    // reload. Serializing it through a JSON double silently corrupts every
    // seed above 2^53 (~91% of the 64-bit space): the reloaded study would
    // replay with a DIFFERENT seed while claiming the original. The same
    // applies to 2^63+ seeds, whose double form also flips sign on read.
    for ( const quint64 seed :
          { quint64{ 9007199254740993ULL },   // 2^53 + 1 — first broken value
            quint64{ 0x8000000000000001ULL }, // 2^63 + 1 — sign-reinterpreted
            quint64{ 0xFFFFFFFFFFFFFFFFULL } } )
    {
        const auto row = recordedRowWithSeed( seed );
        const auto parsed = StudyRunRow::fromJson( row.toJson() );
        REQUIRE( parsed.has_value() );
        INFO( "seed = " << seed );
        CHECK( parsed.value().seed == seed );
    }

    // Small seeds keep their legacy lossless form (no behavior change).
    const auto small = recordedRowWithSeed( 42 );
    const auto parsedSmall = StudyRunRow::fromJson( small.toJson() );
    REQUIRE( parsedSmall.has_value() );
    CHECK( parsedSmall.value().seed == 42 );
}

TEST_CASE( "run table seeds survive a full report round-trip at 64-bit width",
           "[study][export][seed]" )
{
    Fixture fix;
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-export" ) );
    experiment.setName( QStringLiteral( "export" ) );
    experiment.setCreatedAtUtc( frozenTime() );
    REQUIRE( fix.store.upsertExperiment( experiment ).has_value() );

    // The spec validation caps budget.seed at 2^53-1 for JSON fidelity — but
    // replicate seeds are budget.seed + replicateIndex, so a legal maximum
    // budget with a full replicate count lands ABOVE 2^53: exactly the range
    // a JSON double corrupts on the report round-trip.
    auto spec = specWithObjective();
    spec.budget.seed = ( quint64{ 1 } << 53 ) - 1; // legal maximum
    spec.budget.seedReplicates = 32; // budget.seedReplicates bound
    spec.budget.maxRuns = 160; // 5 OAT parameter sets x 32 replicates
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 160 );
    const quint64 widest = points.last().seed;
    REQUIRE( widest > quint64{ 1 } << 53 ); // the fixture really breaks doubles

    for ( const StudyPoint &point : points )
        fix.recordRun( spec, point, QJsonObject{ { QStringLiteral( "maskedPercent" ), 1.0 } } );

    const StudyReport report =
        buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr, frozenTime() );
    REQUIRE( report.runTable.size() == 160 );
    for ( int i = 0; i < report.runTable.size(); ++i )
        REQUIRE( report.runTable.at( i ).seed == points.at( i ).seed );

    const auto parsed = StudyReport::fromJson( report.toJson() );
    REQUIRE( parsed.has_value() );
    for ( int i = 0; i < parsed.value().runTable.size(); ++i )
        CHECK( parsed.value().runTable.at( i ).seed == points.at( i ).seed );
}

namespace
{

// Assembles a fresh store with @p n recorded LHS points and times ONLY the
// report assembly (setup cost is not part of the complexity claim).
// @p budgetSeed must satisfy the spec's JSON-fidelity cap (<= 2^53-1).
qint64 timedReportAssemblyMs( int n, quint64 budgetSeed )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::experiment::ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
    sicnu::experiment::MatrixLedger ledger{ store };
    sicnu::experiment::ExperimentRunRecorder recorder{ store };

    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "report-scaling" );
    spec.experimentId = QStringLiteral( "exp-report-scaling" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.strategy = SamplingStrategy::LatinHypercube;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.0;
    dim.maxValue = 1.0;
    dim.stepCount = 2;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = n;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 1000;
    spec.budget.seedReplicates = 1;
    spec.budget.seed = budgetSeed;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );

    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( spec.experimentId );
    experiment.setName( QStringLiteral( "scaling" ) );
    experiment.setCreatedAtUtc( frozenTime() );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    const auto sampled = sampleStudyPoints( spec );
    REQUIRE( sampled.has_value() );
    const QVector<StudyPoint> points = sampled.has_value() ? sampled.value()
                                                           : QVector<StudyPoint>{};
    REQUIRE( points.size() == n );
    for ( const StudyPoint &point : points )
        recordScaledRun( recorder, ledger, spec, point, dir );

    QElapsedTimer timer;
    timer.start();
    const StudyReport report =
        buildStudyReport( store, ledger, spec, points, {}, nullptr, frozenTime() );
    const qint64 ms = timer.elapsed();
    REQUIRE( report.runTable.size() == n );
    return ms;
}

} // namespace

TEST_CASE( "study sweep cap refuses oversized specs instead of truncating",
           "[study][export][perf]" )
{
    // The bound that keeps the Studio's model updates affordable is the
    // matrix authority's sweep cap: a 10k-point study never exists — the
    // spec is refused up front, before any sampling or store write.
    auto oversize = specWithObjective();
    oversize.budget.maxRuns = 10000;
    const auto sampled = sampleStudyPoints( oversize );
    REQUIRE( !sampled.has_value() );
    REQUIRE( sampled.diagnostics().first().code
             == QStringLiteral( "study.spec_budget_over_cap" ) );
}

TEST_CASE( "report assembly at the sweep cap stays within the UI budget",
           "[study][export][perf]" )
{
    // The report builder indexes the sampled points once (O(n)); the earlier
    // form rescanned the full list per run-table row (O(n²) in the cap-bound
    // worst case). Profile reference (this machine, Debug): see PR notes.
    const qint64 capMs = timedReportAssemblyMs( 1000, 7 );
    WARN( "report assembly profile: 1000 points = " << capMs << " ms" );
    CHECK( capMs < 5000 ); // absolute teaching-study budget at the cap
}
