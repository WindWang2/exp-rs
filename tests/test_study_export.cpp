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
#include <QJsonArray>
#include <QJsonDocument>
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
