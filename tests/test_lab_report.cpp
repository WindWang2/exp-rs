// test_lab_report.cpp — D5: Lab Report & Lineage. Schema contract of
// `sicnu.labreport.v1`, determinism, cross-format parity, secret hygiene
// (issue #789 semantics), replay readiness surfacing, thumbnail bounds, the
// typed grade seam, step attribution, lineage degradation, and the
// desktop lab auto-recorder end-to-end through the real coordinator.
#include <catch2/catch_test_macros.hpp>

#include "experiment/bridge/execution_event_conversion.h"
#include "experiment/bridge/lab_report.h"
#include "experiment/bridge/lab_report_writers.h"
#include "experiment/bridge/lab_run_recorder.h"

#include "dataset/dataset_store.h"
#include "dataset/split.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include "jobs/job_engine.h"
#include "operators/framework/rs_operation_logger.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_coordinator.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <json/json.h>

#include <chrono>
#include <thread>

using namespace sicnu::dataset;
using namespace sicnu::experiment;
namespace workflow = sicnu::workflow;
namespace jobs = sicnu::jobs;

namespace
{

// A canary that must NEVER appear in any exported artifact when a test
// plants secrets.
constexpr const char *kPlantedToken = "SICNU-PLANTED-TOKEN-7f3a";
constexpr const char *kPlantedPassword = "hunter2-secret";

// Mirrors RSOperationLogger::toJson()'s ACTUAL vocabulary ("operator",
// "startTime", "endTime" — rs_operation_logger.cpp), so the attribution
// tests exercise what production feeds the builder, not an idealized shape.
QJsonObject operationRecord( const QString &operatorName, const QDateTime &started,
                             double durationMs, bool success, QJsonObject params = {},
                             QJsonObject result = {} )
{
    QJsonObject record;
    record.insert( QStringLiteral( "operator" ), operatorName );
    record.insert( QStringLiteral( "parameters" ), params );
    record.insert( QStringLiteral( "result" ), result );
    record.insert( QStringLiteral( "success" ), success );
    if ( !success )
    {
        record.insert( QStringLiteral( "errorCode" ), 42 );
        record.insert( QStringLiteral( "errorMessage" ),
                       QStringLiteral( "operator exploded" ) );
    }
    record.insert( QStringLiteral( "startTime" ), started.toString( Qt::ISODateWithMs ) );
    record.insert( QStringLiteral( "endTime" ),
                   started.addMSecs( static_cast<qint64>( durationMs ) )
                       .toString( Qt::ISODateWithMs ) );
    record.insert( QStringLiteral( "durationMs" ), durationMs );
    return record;
}

QByteArray pngBytes( int width, int height )
{
    QImage image( width, height, QImage::Format_RGB32 );
    image.fill( QColor( 30, 90, 140 ) );
    QBuffer buffer;
    buffer.open( QIODevice::WriteOnly );
    image.save( &buffer, "PNG" );
    return buffer.data();
}

/// Builder-level fixture: both stores open, one dataset version + split
/// manifest committed, experiment + one completed run carrying pins, a
/// planted-secret environment, and a metric record.
struct ReportFixture
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    QString datasetVersionId;
    QString datasetFingerprint;
    QString splitManifestId = QStringLiteral( "22222222-2222-4222-8222-222222222222" );
    QString splitFingerprint = QStringLiteral( "sf1" );
    QString runId = QStringLiteral( "run-lab-1" );
    QString experimentId = QStringLiteral( "lab-exp-1" );
    QDateTime started = QDateTime::currentDateTimeUtc().addSecs( -60 );
    QDateTime finished = QDateTime::currentDateTimeUtc().addSecs( -10 );

    ReportFixture()
    {
        REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

        const DatasetId datasetId = DatasetId::generate();
        REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc08" ) ).has_value() );
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        const auto draft = datasets.createDraftVersion( manifest );
        REQUIRE( draft.has_value() );
        const auto versionId =
            DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
        REQUIRE( datasets.stageVersion( versionId ).has_value() );
        const auto committed = datasets.commitVersion( versionId );
        REQUIRE( committed.has_value() );
        datasetVersionId = committed->versionId();
        datasetFingerprint = committed->fingerprint();

        // A manifest that round-trips: config + determinism + assignments
        // (SplitManifest::fromJson refuses degenerate documents).
        SplitManifest split;
        split.setManifestId( splitManifestId );
        split.setDatasetVersionId( datasetVersionId );
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 7;
        split.setConfig( config );
        SplitAssignment assignment;
        assignment.sampleId = QStringLiteral( "sample-1" );
        assignment.role = SplitRole::Train;
        split.assignments().append( assignment );
        split.setFingerprint( splitFingerprint );
        REQUIRE( datasets.saveSplitManifest( split ).has_value() );
        REQUIRE( datasets.splitManifestById( splitManifestId ).has_value() );

        Experiment experiment;
        experiment.setExperimentId( experimentId );
        experiment.setName( QStringLiteral( " OBIA 教学实验" ) );
        experiment.setObjective( QStringLiteral( "理解面向对象分类的流程" ) );
        REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

        ExperimentRun run;
        run.setRunId( runId );
        run.setExperimentId( experimentId );
        run.setAlgorithmId( QStringLiteral( "rs:spectral_index" ) );
        run.setAlgorithmVersion( QStringLiteral( "2.1" ) );
        QJsonObject parameters;
        parameters.insert( QStringLiteral( "index" ), QStringLiteral( "ndvi" ) );
        parameters.insert( QStringLiteral( "password" ), QString::fromUtf8( kPlantedPassword ) );
        run.setParameters( parameters );
        run.setDatasetVersionId( datasetVersionId );
        run.setDatasetFingerprint( datasetFingerprint );
        run.setSplitManifestId( splitManifestId );
        run.setSplitFingerprint( splitFingerprint );
        run.setSeed( 7 );
        run.setSoftwareRevision( QStringLiteral( "rev-lab" ) );
        run.setExecutionRef( QStringLiteral( "wf-run-1" ) );
        QJsonObject envFields;
        envFields.insert( QStringLiteral( "platform" ), QStringLiteral( "test" ) );
        QHash<QString, QString> envVariables;
        envVariables.insert( QStringLiteral( "SICNU_LAB_TOKEN" ),
                             QString::fromUtf8( kPlantedToken ) );
        run.setEnvironment( RunEnvironment::fromFields( envFields, envVariables ) );
        run.setStartedAtUtc( started );
        run.setFinishedAtUtc( finished );

        // Truthful lifecycle: Created → Running → Completed.
        REQUIRE( experiments.upsertRun( run ).has_value() );
        run.setStatus( RunStatus::Running );
        REQUIRE( experiments.upsertRun( run ).has_value() );
        run.setStatus( RunStatus::Completed );
        ExperimentRun::Artifact artifact;
        artifact.path = dir.filePath( QStringLiteral( "ndvi.tif" ) );
        artifact.role = QStringLiteral( "step-1" );
        artifact.digest = QStringLiteral( "deadbeef" );
        artifact.sizeBytes = 1024;
        run.artifacts().append( artifact );
        QJsonObject metrics;
        metrics.insert( QStringLiteral( "accuracy" ), 0.9 );
        run.setMetrics( metrics );
        REQUIRE( experiments.upsertRun( run ).has_value() );

        MetricRecord record;
        record.runId = runId;
        record.protocol.setDatasetVersionId( datasetVersionId );
        record.protocol.setSplitManifestId( splitManifestId );
        QJsonObject document;
        document.insert( QStringLiteral( "kappa" ), 0.87 );
        record.metrics = document;
        REQUIRE( experiments.saveMetricRecord( record ).has_value() );

        REQUIRE( experiments
                     .addLineageEdge( QStringLiteral( "run" ), runId, QStringLiteral( "produced" ),
                                      QStringLiteral( "artifact" ), QStringLiteral( "ndvi.tif" ) )
                     .has_value() );
    }

    LabReportRequest baseRequest() const
    {
        LabReportRequest request;
        request.labId = experimentId;
        request.student = QStringLiteral( "张三" );
        request.session = QStringLiteral( "2026 春" );
        request.gitSha = QStringLiteral( "abc1234" );
        request.generatedAtUtc = QStringLiteral( "2026-09-12T08:00:00.000Z" );
        return request;
    }
};

} // namespace

TEST_CASE( "lab_report: builds the complete sicnu.labreport.v1 document",
           "[lab_report][schema]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();
    request.operationTrail.append( operationRecord( QStringLiteral( "rs:spectral_index" ),
                                                    fx.started.addSecs( 5 ), 120.0, true ) );

    LabReportBuilder builder( fx.experiments, &fx.datasets );
    auto built = builder.build( request );
    REQUIRE( built.has_value() );
    const QJsonObject document = built.value();

    CHECK( document.value( QStringLiteral( "schema" ) ).toString()
           == QString::fromUtf8( kLabReportSchemaId ) );
    for ( const QString &section :
          { QStringLiteral( "header" ), QStringLiteral( "runs" ), QStringLiteral( "steps" ),
            QStringLiteral( "statistics" ), QStringLiteral( "thumbnails" ),
            QStringLiteral( "grade" ), QStringLiteral( "lineage" ),
            QStringLiteral( "environment" ), QStringLiteral( "replay" ) } )
        CHECK( document.contains( section ) );

    const QJsonObject header = document.value( QStringLiteral( "header" ) ).toObject();
    CHECK( header.value( QStringLiteral( "reportId" ) ).toString()
           == QStringLiteral( "labreport-%1-%2" ).arg( fx.experimentId, fx.runId ) );
    CHECK( header.value( QStringLiteral( "student" ) ).toString() == QStringLiteral( "张三" ) );
    CHECK( header.value( QStringLiteral( "gitSha" ) ).toString() == QLatin1String( "abc1234" ) );
    CHECK( header.value( QStringLiteral( "generatedAtUtc" ) ).toString()
           == QLatin1String( "2026-09-12T08:00:00.000Z" ) );

    const QJsonArray runs = document.value( QStringLiteral( "runs" ) ).toArray();
    REQUIRE( runs.size() == 1 );
    const QJsonObject run = runs.at( 0 ).toObject();
    CHECK( run.value( QStringLiteral( "runId" ) ).toString() == fx.runId );
    CHECK( run.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
    CHECK( run.value( QStringLiteral( "datasetFingerprint" ) ).toString()
           == fx.datasetFingerprint );
    CHECK( run.value( QStringLiteral( "configHash" ) ).toString().size() == 64 );

    const QJsonArray steps = document.value( QStringLiteral( "steps" ) ).toArray();
    REQUIRE( steps.size() == 1 );
    const QJsonObject step = steps.at( 0 ).toObject();
    CHECK( step.value( QStringLiteral( "operator" ) ).toString()
           == QLatin1String( "rs:spectral_index" ) );
    CHECK( step.value( QStringLiteral( "success" ) ).toBool() );
    const QJsonObject attribution = step.value( QStringLiteral( "attribution" ) ).toObject();
    CHECK( attribution.value( QStringLiteral( "runId" ) ).toString() == fx.runId );
    CHECK( attribution.value( QStringLiteral( "quality" ) ).toString() == QLatin1String( "exact" ) );
    CHECK( attribution.value( QStringLiteral( "policy" ) ).toString()
           == QString::fromUtf8( kLabStepAttributionPolicy ) );

    const QJsonArray statistics = document.value( QStringLiteral( "statistics" ) ).toArray();
    REQUIRE( statistics.size() == 1 );

    const QJsonObject grade = document.value( QStringLiteral( "grade" ) ).toObject();
    CHECK( grade.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "unavailable" ) );

    const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
    CHECK( replay.value( QStringLiteral( "level" ) ).toString().isEmpty() == false );

    CHECK( LabReportBuilder::validate( document ).has_value() );
}

TEST_CASE( "lab_report: generation is deterministic modulo the injected timestamp",
           "[lab_report][determinism]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();
    request.operationTrail.append( operationRecord( QStringLiteral( "rs:spectral_index" ),
                                                    fx.started.addSecs( 5 ), 120.0, true ) );

    LabReportBuilder builder( fx.experiments, &fx.datasets );
    const QJsonObject first = builder.build( request ).value();
    const QJsonObject second = builder.build( request ).value();

    const QByteArray firstBytes =
        QJsonDocument( first ).toJson( QJsonDocument::Indented );
    const QByteArray secondBytes =
        QJsonDocument( second ).toJson( QJsonDocument::Indented );
    CHECK( firstBytes == secondBytes );

    const QString firstMd = labReportMarkdown( first ).value();
    const QString secondMd = labReportMarkdown( second ).value();
    CHECK( firstMd == secondMd );
    const QString firstHtml = labReportHtml( first ).value();
    const QString secondHtml = labReportHtml( second ).value();
    CHECK( firstHtml == secondHtml );
}

TEST_CASE( "lab_report: markdown and html carry the same facts as the json document",
           "[lab_report][parity]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();

    LabReportThumbnail thumbnail;
    thumbnail.sourcePath = fx.dir.filePath( QStringLiteral( "ndvi.tif" ) );
    thumbnail.sourceSizeBytes = 1024;
    thumbnail.pngBytes = pngBytes( 256, 128 );
    request.thumbnails.append( thumbnail );

    LabReportBuilder builder( fx.experiments, &fx.datasets );
    const QJsonObject document = builder.build( request ).value();
    const QString json = QString::fromUtf8( QJsonDocument( document ).toJson() );
    const QString md = labReportMarkdown( document ).value();
    const QString html = labReportHtml( document ).value();

    for ( const QString &needle :
          { fx.runId, fx.datasetVersionId, QStringLiteral( "rs:spectral_index" ),
            QStringLiteral( "ndvi.tif" ), fx.datasetFingerprint } )
    {
        INFO( "needle: " << needle.toStdString() );
        CHECK( json.contains( needle ) );
        CHECK( md.contains( needle ) );
        CHECK( html.contains( needle ) );
    }
    // The thumbnail data URL (inline imagery) must reach every format.
    const QString dataUrl =
        document.value( QStringLiteral( "thumbnails" ) ).toArray().at( 0 ).toObject().value(
            QStringLiteral( "dataUrl" ) )
            .toString();
    CHECK( md.contains( dataUrl ) );
    CHECK( html.contains( dataUrl ) );
    // Replay blockers and grade status visible in renderings.
    CHECK( md.contains( QStringLiteral( "unavailable" ) ) );
    CHECK( html.contains( QStringLiteral( "unavailable" ) ) );
    // The HTML is self-contained: no external resource references.
    CHECK_FALSE( html.contains( QStringLiteral( "http://" ) ) );
    CHECK_FALSE( html.contains( QStringLiteral( "https://" ) ) );
    CHECK_FALSE( html.contains( QStringLiteral( "<link" ) ) );
    CHECK_FALSE( html.contains( QStringLiteral( "<script" ) ) );
}

TEST_CASE( "lab_report: no planted secret survives any rendering", "[lab_report][secrets]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();

    QJsonObject params;
    params.insert( QStringLiteral( "api_key" ), QString::fromUtf8( kPlantedToken ) );
    QJsonObject result;
    result.insert( QStringLiteral( "service_password" ),
                   QString::fromUtf8( kPlantedPassword ) );
    request.operationTrail.append(
        operationRecord( QStringLiteral( "rs:upload" ), fx.started.addSecs( 3 ), 40.0, true,
                         params, result ) );

    LabReportBuilder builder( fx.experiments, &fx.datasets );
    const QJsonObject document = builder.build( request ).value();
    const QString json = QString::fromUtf8( QJsonDocument( document ).toJson() );
    const QString md = labReportMarkdown( document ).value();
    const QString html = labReportHtml( document ).value();

    const QString tokenString = QString::fromUtf8( kPlantedToken );
    const QString passwordString = QString::fromUtf8( kPlantedPassword );
    for ( const QString *secret : { &tokenString, &passwordString } )
    {
        CHECK_FALSE( json.contains( *secret ) );
        CHECK_FALSE( md.contains( *secret ) );
        CHECK_FALSE( html.contains( *secret ) );
    }
}

TEST_CASE( "lab_report: replay readiness: blockers surfaced without pins, exact with a wired world",
           "[lab_report][replay]" )
{
    ReportFixture fx;

    SECTION( "no dataset store and no hooks → impossible with explicit blockers" )
    {
        // A run with NO dataset/split pins: the blockers must name them.
        const QDateTime started = QDateTime::currentDateTimeUtc().addSecs( -30 );
        Experiment bare;
        bare.setExperimentId( QStringLiteral( "lab-exp-bare" ) );
        bare.setName( QStringLiteral( "bare lab" ) );
        REQUIRE( fx.experiments.upsertExperiment( bare ).has_value() );
        ExperimentRun run;
        run.setRunId( QStringLiteral( "run-bare" ) );
        run.setExperimentId( QStringLiteral( "lab-exp-bare" ) );
        run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
        run.setStartedAtUtc( started );
        run.setFinishedAtUtc( started.addSecs( 20 ) );
        run.setStatus( RunStatus::Running );
        REQUIRE( fx.experiments.upsertRun( run ).has_value() );
        run.setStatus( RunStatus::Completed );
        REQUIRE( fx.experiments.upsertRun( run ).has_value() );

        LabReportRequest request = fx.baseRequest();
        request.labId = QStringLiteral( "lab-exp-bare" );
        request.operationTrail.append(
            operationRecord( QStringLiteral( "rs:classify" ), started.addSecs( 5 ), 1.0, true ) );
        LabReportBuilder builder( fx.experiments, nullptr );
        const QJsonObject document = builder.build( request ).value();
        const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
        CHECK( replay.value( QStringLiteral( "level" ) ).toString() == QLatin1String( "impossible" ) );
        const QJsonArray blockers = replay.value( QStringLiteral( "blockers" ) ).toArray();
        REQUIRE( blockers.size() >= 2 );
        bool namesDataset = false;
        bool namesSplit = false;
        for ( const QJsonValue &blocker : blockers )
        {
            namesDataset = namesDataset || blocker.toString().contains( QStringLiteral( "dataset_version" ) );
            namesSplit = namesSplit || blocker.toString().contains( QStringLiteral( "split_manifest" ) );
        }
        CHECK( namesDataset );
        CHECK( namesSplit );
    }

    SECTION( "wired dataset store + pins + hooks → exact" )
    {
        LabReportRequest request = fx.baseRequest();
        request.hooks.algorithmAvailable = []( const QString &algorithmId ) {
            return algorithmId == QLatin1String( "rs:spectral_index" );
        };
        request.hooks.artifactAvailable = []( const QString &, qint64 ) { return true; };
        LabReportBuilder builder( fx.experiments, &fx.datasets );
        const QJsonObject document = builder.build( request ).value();
        const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
        CHECK( replay.value( QStringLiteral( "level" ) ).toString() == QLatin1String( "exact" ) );
        CHECK( replay.value( QStringLiteral( "blockers" ) ).toArray().isEmpty() );
    }
}

TEST_CASE( "lab_report: thumbnail bounds are enforced by the builder, not the caller",
           "[lab_report][thumbnails]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();

    LabReportThumbnail oversized;
    oversized.sourcePath = fx.dir.filePath( QStringLiteral( "big.tif" ) );
    oversized.pngBytes = pngBytes( 600, 400 ); // long edge 600 > 512
    request.thumbnails.append( oversized );

    LabReportThumbnail fine;
    fine.sourcePath = fx.dir.filePath( QStringLiteral( "ndvi.tif" ) );
    fine.pngBytes = pngBytes( 256, 128 );
    request.thumbnails.append( fine );

    LabReportThumbnail garbage;
    garbage.sourcePath = fx.dir.filePath( QStringLiteral( "fake.png" ) );
    garbage.pngBytes = QByteArray( "definitely not a png" );
    request.thumbnails.append( garbage );

    LabReportBuilder builder( fx.experiments, &fx.datasets );
    const QJsonObject document = builder.build( request ).value();
    const QJsonArray thumbnails = document.value( QStringLiteral( "thumbnails" ) ).toArray();
    REQUIRE( thumbnails.size() == 1 );
    const QJsonObject embedded = thumbnails.at( 0 ).toObject();
    CHECK( embedded.value( QStringLiteral( "widthPx" ) ).toInt() == 256 );
    CHECK( embedded.value( QStringLiteral( "heightPx" ) ).toInt() == 128 );
    CHECK( embedded.value( QStringLiteral( "dataUrl" ) ).toString().startsWith(
        QLatin1String( "data:image/png;base64," ) ) );
    CHECK( embedded.value( QStringLiteral( "sha256" ) ).toString().size() == 64 );

    const QJsonArray warnings = document.value( QStringLiteral( "warnings" ) ).toArray();
    REQUIRE( warnings.size() == 2 );

    // The document cannot be sneaked past validation with a lying thumbnail.
    QJsonObject doctored = document;
    QJsonArray doctoredThumbnails;
    QJsonObject fake;
    fake.insert( QStringLiteral( "sourcePath" ), QStringLiteral( "x.tif" ) );
    fake.insert( QStringLiteral( "widthPx" ), 100 );
    fake.insert( QStringLiteral( "heightPx" ), 100 );
    fake.insert( QStringLiteral( "dataUrl" ),
                 QStringLiteral( "data:image/png;base64,QUJD" ) );
    doctoredThumbnails.append( fake );
    doctored.insert( QStringLiteral( "thumbnails" ), doctoredThumbnails );
    CHECK( LabReportBuilder::validate( doctored ).has_value() );
    QJsonObject lying = doctored;
    QJsonArray lyingThumbnails;
    lyingThumbnails.append( request.thumbnails.at( 0 ).toJson() );
    lying.insert( QStringLiteral( "thumbnails" ), lyingThumbnails );
    // Oversized (no widthPx declared) must fail validation.
    CHECK_FALSE( LabReportBuilder::validate( lying ).has_value() );
}

TEST_CASE( "lab_report: grade seam: recorded requires a grading ref; unavailable explains itself",
           "[lab_report][grade]" )
{
    LabGradeEmbedding unavailable = LabGradeEmbedding::unavailable();
    CHECK( unavailable.toJson().value( QStringLiteral( "status" ) ).toString()
           == QLatin1String( "unavailable" ) );
    CHECK_FALSE( unavailable.toJson().value( QStringLiteral( "reason" ) ).toString().isEmpty() );

    LabGradeEmbedding recorded;
    recorded.status = QStringLiteral( "recorded" );
    recorded.inlineResult = QJsonObject{ { QStringLiteral( "score" ), 88.5 } };
    // No gradingRef → the document must be refused.
    const QJsonObject withoutRef = recorded.toJson();
    QJsonObject document;
    document.insert( QStringLiteral( "schema" ), QString::fromUtf8( kLabReportSchemaId ) );
    document.insert( QStringLiteral( "grade" ), withoutRef );
    CHECK_FALSE( LabReportBuilder::validate( document ).has_value() );

    recorded.gradingRef = QStringLiteral( "grading/run-lab-1" );
    const QJsonObject withRef = recorded.toJson();
    CHECK( withRef.value( QStringLiteral( "gradingRef" ) ).toString()
           == QLatin1String( "grading/run-lab-1" ) );
    CHECK( withRef.contains( QStringLiteral( "inline" ) ) );
}

TEST_CASE( "lab_report: step attribution: exact inside one window, unattributed outside",
           "[lab_report][attribution]" )
{
    ReportFixture fx;
    // A second, later run of the same experiment.
    ExperimentRun run2;
    run2.setRunId( QStringLiteral( "run-lab-2" ) );
    run2.setExperimentId( fx.experimentId );
    run2.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run2.setStartedAtUtc( fx.finished.addSecs( 30 ) );
    run2.setFinishedAtUtc( fx.finished.addSecs( 90 ) );
    run2.setStatus( RunStatus::Running );
    REQUIRE( fx.experiments.upsertRun( run2 ).has_value() );
    run2.setStatus( RunStatus::Completed );
    REQUIRE( fx.experiments.upsertRun( run2 ).has_value() );

    LabReportRequest request = fx.baseRequest();
    request.operationTrail.append( operationRecord( QStringLiteral( "rs:spectral_index" ),
                                                    fx.started.addSecs( 5 ), 10.0, true ) );
    request.operationTrail.append( operationRecord( QStringLiteral( "rs:classify" ),
                                                    fx.finished.addSecs( 40 ), 10.0, true ) );
    request.operationTrail.append( operationRecord( QStringLiteral( "rs:unrelated" ),
                                                    fx.finished.addSecs( 300 ), 10.0, true ) );

    LabReportBuilder builder( fx.experiments, nullptr );
    const QJsonObject document = builder.build( request ).value();
    const QJsonArray steps = document.value( QStringLiteral( "steps" ) ).toArray();
    REQUIRE( steps.size() == 3 );
    CHECK( steps.at( 0 ).toObject().value( QStringLiteral( "attribution" ) )
               .toObject()
               .value( QStringLiteral( "runId" ) )
               .toString() == fx.runId );
    CHECK( steps.at( 1 ).toObject().value( QStringLiteral( "attribution" ) )
               .toObject()
               .value( QStringLiteral( "runId" ) )
               .toString() == QLatin1String( "run-lab-2" ) );
    CHECK( steps.at( 2 ).toObject().value( QStringLiteral( "attribution" ) )
               .toObject()
               .value( QStringLiteral( "runId" ) )
               .isNull() );
}

TEST_CASE( "lab_report: lineage degrades to experiment edges without a dataset store",
           "[lab_report][lineage]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();

    SECTION( "with a dataset store: full joined graph slice" )
    {
        // An input edge so the ancestor slice has something to traverse.
        REQUIRE( fx.experiments
                     .addLineageEdge( QStringLiteral( "dataset_version" ),
                                      fx.datasetVersionId, QStringLiteral( "snapshot_of" ),
                                      QStringLiteral( "run" ), fx.runId )
                     .has_value() );
        LabReportBuilder builder( fx.experiments, &fx.datasets );
        const QJsonObject document = builder.build( request ).value();
        const QJsonObject lineage = document.value( QStringLiteral( "lineage" ) ).toObject();
        CHECK( lineage.value( QStringLiteral( "startId" ) ).toString() == fx.runId );
        CHECK( lineage.value( QStringLiteral( "existence" ) ).toString()
               == QLatin1String( "not-checked" ) );
        CHECK( lineage.value( QStringLiteral( "nodes" ) ).toArray().size() >= 1 );
    }

    SECTION( "without: experiment-store edges only, honestly labelled" )
    {
        LabReportBuilder builder( fx.experiments, nullptr );
        const QJsonObject document = builder.build( request ).value();
        const QJsonObject lineage = document.value( QStringLiteral( "lineage" ) ).toObject();
        CHECK( lineage.value( QStringLiteral( "source" ) ).toString()
               == QLatin1String( "experiment-edges-only" ) );
        bool foundProduced = false;
        for ( const QJsonValue &value : lineage.value( QStringLiteral( "edges" ) ).toArray() )
        {
            const QJsonObject edge = value.toObject();
            foundProduced =
                foundProduced
                || ( edge.value( QStringLiteral( "edge" ) ).toString() == QLatin1String( "produced" )
                     && edge.value( QStringLiteral( "to_id" ) ).toString()
                            == QLatin1String( "ndvi.tif" ) );
        }
        CHECK( foundProduced );
    }
}

TEST_CASE( "lab_report: writeLabReportFiles writes three sibling files and refuses invalid documents",
           "[lab_report][writers]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();
    LabReportBuilder builder( fx.experiments, &fx.datasets );
    const QJsonObject document = builder.build( request ).value();

    const QString base = fx.dir.filePath( QStringLiteral( "report" ) );
    auto written = writeLabReportFiles( document, base );
    REQUIRE( written.has_value() );
    CHECK( written.value().size() == 3 );
    for ( const QString &path : written.value() )
        CHECK( QFile::exists( path ) );
    CHECK( written.value().at( 0 ).endsWith( QLatin1String( ".json" ) ) );
    CHECK( written.value().at( 1 ).endsWith( QLatin1String( ".md" ) ) );
    CHECK( written.value().at( 2 ).endsWith( QLatin1String( ".html" ) ) );

    QJsonObject broken = document;
    broken.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.labreport.v99" ) );
    auto refused = writeLabReportFiles( broken, base );
    CHECK_FALSE( refused.has_value() );
}

// --- desktop lab auto-recorder (end-to-end through the coordinator) ------------

namespace
{

struct RecorderFixture
{
    QTemporaryDir checkpointDir;
    QTemporaryDir storeDir;
    workflow::WorkflowRunCoordinator &coordinator = workflow::WorkflowRunCoordinator::instance();

    RecorderFixture()
    {
        int argc = 1;
        static char arg0[] = "test_lab_report";
        char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
        coordinator.setCheckpointDirectory( checkpointDir.path() );
    }

    static void waitTerminal( workflow::WorkflowRunCoordinator &coordinator, long pipelineId )
    {
        std::shared_ptr<workflow::WorkflowRun> snapshot;
        for ( int attempt = 0; attempt < 600; ++attempt )
        {
            snapshot = coordinator.runForPipeline( pipelineId );
            REQUIRE( snapshot != nullptr );
            if ( workflow::isTerminalRunState( snapshot->state() ) )
                return;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        FAIL( "pipeline never reached a terminal state" );
    }

    static workflow::WorkflowDefinition twoStepDefinition( const std::string &prefix )
    {
        workflow::WorkflowDefinition def;
        def.id = prefix + "_def";
        def.title = "Lab report tracked pipeline";

        workflow::StepDef first;
        first.id = "first";
        first.title = "First";
        first.kind = workflow::StepKind::Operator;
        first.operatorId = prefix + ":first";
        first.params["output"] = "/tmp/" + prefix + "_first.tif";

        workflow::StepDef second;
        second.id = "second";
        second.title = "Second";
        second.kind = workflow::StepKind::Operator;
        second.operatorId = prefix + ":second";
        second.params["input"] = "$first.output";
        second.params["output"] = "/tmp/" + prefix + "_second.tif";
        workflow::StepConnection conn;
        conn.fromStepId = "first";
        conn.fromPort = "output";
        conn.toPort = "input";
        second.inputs.push_back( conn );

        def.steps.push_back( first );
        def.steps.push_back( second );
        return def;
    }

    static void registerExecutors( const std::string &prefix )
    {
        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.registerExecutor( prefix + ":first",
                                 [prefix]( const sicnu::jobs::JobRequest &,
                                           sicnu::operators::RSOperatorContext & ) {
                                     Json::Value r( Json::objectValue );
                                     r["output"] = "/tmp/" + prefix + "_first.tif";
                                     return r;
                                 } );
        engine.registerExecutor(
            prefix + ":second",
            []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & )
                -> Json::Value { return Json::Value( Json::objectValue ); } );
    }
};

} // namespace

TEST_CASE( "secrets nested in arrays-of-arrays do not survive", "[lab_report][secrets]" )
{
    ReportFixture fx;
    LabReportRequest request = fx.baseRequest();

    // redactSecretKeys recurses objects and one array level; the deep pass
    // must reach objects behind NESTED arrays under innocuous keys.
    QJsonObject inner;
    inner.insert( QStringLiteral( "api_key" ), QString::fromUtf8( kPlantedToken ) );
    QJsonArray outer;
    outer.append( QJsonValue( QJsonArray{ inner } ) );
    QJsonObject params;
    params.insert( QStringLiteral( "layers" ), outer );
    request.operationTrail.append(
        operationRecord( QStringLiteral( "rs:mosaic" ), fx.started.addSecs( 3 ), 40.0, true,
                         params ) );

    LabReportBuilder builder( fx.experiments, nullptr );
    const QJsonObject document = builder.build( request ).value();
    const QString json = QString::fromUtf8( QJsonDocument( document ).toJson() );
    const QString md = labReportMarkdown( document ).value();
    const QString html = labReportHtml( document ).value();
    for ( const QString *rendering : { &json, &md, &html } )
        CHECK_FALSE( rendering->contains( QString::fromUtf8( kPlantedToken ) ) );
}

TEST_CASE( "lab_report: a report exported after a restart reads the trail from stored evidence",
           "[lab_report][recorder][e2e]" )
{
    ReportFixture fx;
    const QDateTime started = QDateTime::currentDateTimeUtc().addSecs( -30 );
    // A separate experiment whose run carries the trail the recorder would
    // have persisted inside its workflow evidence. The live-session logger
    // is empty here — exactly the "export after an app restart" shape.
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "lab-exp-stored" ) );
    experiment.setName( QStringLiteral( "stored lab" ) );
    REQUIRE( fx.experiments.upsertExperiment( experiment ).has_value() );
    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-stored" ) );
    run.setExperimentId( QStringLiteral( "lab-exp-stored" ) );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setStartedAtUtc( started );
    run.setFinishedAtUtc( started.addSecs( 20 ) );
    run.setStatus( RunStatus::Running );
    REQUIRE( fx.experiments.upsertRun( run ).has_value() );

    QJsonObject trailRecord;
    trailRecord.insert( QStringLiteral( "operator" ), QStringLiteral( "rs:stored" ) );
    trailRecord.insert( QStringLiteral( "startTime" ),
                        started.addSecs( 5 ).toString( Qt::ISODateWithMs ) );
    trailRecord.insert( QStringLiteral( "endTime" ),
                        started.addSecs( 15 ).toString( Qt::ISODateWithMs ) );
    trailRecord.insert( QStringLiteral( "success" ), true );
    QJsonObject evidence;
    evidence.insert( QStringLiteral( "extra" ),
                     QJsonObject{ { QStringLiteral( "operationTrail" ),
                                    QJsonArray{ trailRecord } } } );
    QJsonObject metrics;
    metrics.insert( QStringLiteral( "workflow" ), evidence );
    run.setMetrics( metrics );
    run.setStatus( RunStatus::Completed );
    REQUIRE( fx.experiments.upsertRun( run ).has_value() );

    LabReportRequest request = fx.baseRequest();
    request.labId = QStringLiteral( "lab-exp-stored" ); // operationTrail left EMPTY
    LabReportBuilder builder( fx.experiments, nullptr );
    const QJsonObject document = builder.build( request ).value();
    const QJsonArray steps = document.value( QStringLiteral( "steps" ) ).toArray();
    REQUIRE( steps.size() == 1 );
    CHECK( steps.at( 0 ).toObject().value( QStringLiteral( "operator" ) ).toString()
           == QLatin1String( "rs:stored" ) );
    CHECK( steps.at( 0 ).toObject().value( QStringLiteral( "attribution" ) )
               .toObject()
               .value( QStringLiteral( "runId" ) )
               .toString() == QLatin1String( "run-stored" ) );
}

TEST_CASE( "lab_report: recorder auto-registers a tracked pipeline as an experiment run",
           "[lab_report][recorder]" )
{
    RecorderFixture fx;
    const std::string prefix = "labrec";
    fx.registerExecutors( prefix );

    LabRunRecorder recorder( fx.coordinator );
    QJsonArray trail;
    recorder.setOperationTrailSource( [&trail]() { return trail; } );
    QString error;
    REQUIRE( recorder.enable( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ),
                              QStringLiteral( "lab-exp" ), QStringLiteral( "教学实验" ),
                              QStringLiteral( "课堂闭环" ), QString(), &error ) );
    REQUIRE( recorder.isBound() );
    REQUIRE( recorder.recordingEnabled() );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( fx.twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto run = fx.coordinator.runForPipeline( pipelineId );
    REQUIRE( run != nullptr );
    const QString executionRef = QString::fromStdString( run->runId() );

    RecorderFixture::waitTerminal( fx.coordinator, pipelineId );
    recorder.flush();

    ExperimentStore reader;
    REQUIRE( reader.open( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ) ) );
    const auto runs = reader.listRuns( QStringLiteral( "lab-exp" ) );
    REQUIRE( runs.has_value() );
    REQUIRE( runs.value().second.size() == 1 );
    const ExperimentRun recorded = runs.value().second.first();
    CHECK( recorded.executionRef() == executionRef );
    CHECK( recorded.status() == RunStatus::Completed );
    CHECK( recorded.algorithmId() == prefix + "_def" );
    CHECK( recorder.runIdForExecution( executionRef ) == recorded.runId() );
    CHECK( recorder.recordedExecutionRefs().contains( executionRef ) );
}

TEST_CASE( "lab_report: recorder opt-out records nothing new but closes nothing behind "
           "its back either",
           "[lab_report][recorder]" )
{
    RecorderFixture fx;
    const std::string prefix = "labopt";
    fx.registerExecutors( prefix );

    LabRunRecorder recorder( fx.coordinator );
    QString error;
    REQUIRE( recorder.enable( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ),
                              QStringLiteral( "lab-exp" ), QStringLiteral( "教学实验" ),
                              QString(), QString(), &error ) );
    recorder.setRecordingEnabled( false );
    CHECK_FALSE( recorder.recordingEnabled() );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( fx.twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    RecorderFixture::waitTerminal( fx.coordinator, pipelineId );
    recorder.flush();

    ExperimentStore reader;
    REQUIRE( reader.open( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ) ) );
    const auto runs = reader.listRuns( QStringLiteral( "lab-exp" ) );
    REQUIRE( runs.has_value() );
    CHECK( runs.value().second.isEmpty() );
}

TEST_CASE( "lab_report: recorder embeds a redacted operation trail as run evidence",
           "[lab_report][recorder]" )
{
    RecorderFixture fx;
    const std::string prefix = "labtrail";
    fx.registerExecutors( prefix );

    LabRunRecorder recorder( fx.coordinator );
    recorder.setOperationTrailSource( []() {
        QJsonArray trail;
        QJsonObject record;
        record.insert( QStringLiteral( "operatorName" ), QStringLiteral( "rs:upload" ) );
        QJsonObject params;
        params.insert( QStringLiteral( "api_key" ), QString::fromUtf8( kPlantedToken ) );
        record.insert( QStringLiteral( "parameters" ), params );
        record.insert( QStringLiteral( "success" ), true );
        trail.append( record );
        return trail;
    } );
    QString error;
    REQUIRE( recorder.enable( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ),
                              QStringLiteral( "lab-exp" ), QStringLiteral( "教学实验" ),
                              QString(), QString(), &error ) );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( fx.twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    RecorderFixture::waitTerminal( fx.coordinator, pipelineId );
    recorder.flush();

    ExperimentStore reader;
    REQUIRE( reader.open( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ) ) );
    const auto runs = reader.listRuns( QStringLiteral( "lab-exp" ) );
    REQUIRE( runs.has_value() );
    REQUIRE( runs.value().second.size() == 1 );
    const QJsonObject workflow = runs.value().second.first().metrics().value(
                                     QStringLiteral( "workflow" ) )
                                     .toObject();
    const QJsonObject extra = workflow.value( QStringLiteral( "extra" ) ).toObject();
    const QJsonArray trail = extra.value( QStringLiteral( "operationTrail" ) ).toArray();
    REQUIRE( trail.size() == 1 );
    const QString trailJson = QString::fromUtf8(
        QJsonDocument( QJsonObject{ { QStringLiteral( "trail" ), trail.at( 0 ) } } )
            .toJson() );
    CHECK_FALSE( trailJson.contains( QString::fromUtf8( kPlantedToken ) ) );
}

TEST_CASE( "lab_report: an end-to-end lab run produces a report whose blockers are explicit",
           "[lab_report][e2e]" )
{
    RecorderFixture fx;
    const std::string prefix = "labe2e";
    fx.registerExecutors( prefix );

    LabRunRecorder recorder( fx.coordinator );
    QString error;
    REQUIRE( recorder.enable( fx.storeDir.filePath( QStringLiteral( "lab.db" ) ),
                              QStringLiteral( "lab-exp" ), QStringLiteral( "教学实验" ),
                              QString(), QString(), &error ) );

    const long pipelineId =
        fx.coordinator.startTrackedPipeline( fx.twoStepDefinition( prefix ), /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    RecorderFixture::waitTerminal( fx.coordinator, pipelineId );
    recorder.flush();

    ExperimentStore *store = recorder.store();
    REQUIRE( store != nullptr );
    LabReportRequest request;
    request.labId = QStringLiteral( "lab-exp" );
    request.generatedAtUtc = QStringLiteral( "2026-09-12T09:30:00.000Z" );
    LabReportBuilder builder( *store, nullptr );
    auto built = builder.build( request );
    REQUIRE( built.has_value() );
    const QJsonObject document = built.value();
    CHECK( LabReportBuilder::validate( document ).has_value() );
    const QJsonArray runs = document.value( QStringLiteral( "runs" ) ).toArray();
    REQUIRE( runs.size() == 1 );
    CHECK( runs.at( 0 ).toObject().value( QStringLiteral( "status" ) ).toString()
           == QLatin1String( "completed" ) );
    // No pins were supplied: the report says exactly that instead of pretending.
    const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
    CHECK( replay.value( QStringLiteral( "level" ) ).toString() == QLatin1String( "impossible" ) );
    CHECK_FALSE( replay.value( QStringLiteral( "blockers" ) ).toArray().isEmpty() );
}
