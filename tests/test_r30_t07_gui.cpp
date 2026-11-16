// test_r30_t07_gui.cpp — R30-T07 GUI wiring regressions (offscreen Qt).
//
// Real controls, real stores, real label schemas:
//   G1 dock session save failure leaves the previous document intact.
//   G2 editing a field invalidates the validated draft; full v1 spec edit.
//   G2 an expired run callback never overwrites the newer session.
//   G3 typed metric cells + bounded paging through real QTableWidget.
//   G3/G4 comparison with REAL label schema evidence and cross-page pins.
//   G4 recorded-run browse keeps Created evidence honest.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include "app/experiment_studio/experiment_studio_dock.h"
#include "app/workbench/dataset_experiment_panel.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/label_schema.h"
#include "experiment/evaluation.h"
#include "experiment/experiment_store.h"
#include "experiment_studio/studio_session_io.h"
#include "experiment_studio/studio_export.h"
#include "platform/durable_sidecar.h"
#include "study/study_export.h"

#include <gdal.h>

using sicnu::app::DatasetExperimentPanel;
using sicnu::app::ExperimentStudioDock;

namespace
{

void ensureApp()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    QStandardPaths::setTestModeEnabled( true );
    static int argc = 1;
    static char appName[] = "test_r30_t07_gui";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

void resetSessionDir( const char *tag )
{
    QCoreApplication::setApplicationName( QString::fromUtf8( tag ) );
    const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    QDir( root ).removeRecursively();
}

QString writeRaster( const QTemporaryDir &dir )
{
    const QString path = dir.filePath( "input.tif" );
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver ) return {};
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1,
                                  GDT_Float32, nullptr );
    if ( !ds ) return {};
    double geo[6] = { 500000.0, 1.0, 0.0, 4000000.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, geo );
    GDALSetProjection( ds, "EPSG:32648" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const float row[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    for ( int y = 0; y < 8; ++y )
        GDALRasterIO( band, GF_Write, 0, y, 8, 1, const_cast<float *>( row ), 8, 1,
                      GDT_Float32, 0, 0 );
    GDALClose( ds );
    return QFileInfo::exists( path ) ? path : QString();
}

QLabel *summaryOf( ExperimentStudioDock *dock )
{
    return dock->findChild<QLabel *>( QStringLiteral( "rsStudioDesignerSummary" ) );
}

QLineEdit *inputOf( ExperimentStudioDock *dock )
{
    for ( QLineEdit *edit : dock->findChildren<QLineEdit *>() )
        if ( edit->placeholderText().contains( QStringLiteral( "Input raster" ) ) )
            return edit;
    return nullptr;
}

QPushButton *buttonByText( QWidget *parent, const QString &text )
{
    for ( QPushButton *b : parent->findChildren<QPushButton *>() )
        if ( b->text() == text ) return b;
    return nullptr;
}

QPlainTextEdit *advancedSpecOf( ExperimentStudioDock *dock )
{
    return dock->findChild<QPlainTextEdit *>( QStringLiteral( "rsStudioAdvancedSpecEdit" ) );
}

/// A real Schema-1 operator spec document; nested paths and non-numeric
/// sweeps are explicitly refused by the v1 designer boundary.
QJsonObject baseSpec( const QString &raster )
{
    QJsonObject spec;
    spec.insert( QStringLiteral( "schema_version" ), 1 );
    spec.insert( QStringLiteral( "study_id" ), QStringLiteral( "study-gui" ) );
    spec.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-gui" ) );
    spec.insert( QStringLiteral( "algorithm_id" ), QStringLiteral( "rs:threshold_raster" ) );
    QJsonObject params;
    params.insert( QStringLiteral( "input" ), raster );
    spec.insert( QStringLiteral( "base_parameters" ), params );
    QJsonObject sampling;
    sampling.insert( QStringLiteral( "strategy" ), QStringLiteral( "grid" ) );
    spec.insert( QStringLiteral( "sampling" ), sampling );
    QJsonArray dims;
    QJsonObject d;
    d.insert( QStringLiteral( "parameter_path" ), QStringLiteral( "threshold" ) );
    d.insert( QStringLiteral( "min_value" ), 0.1 );
    d.insert( QStringLiteral( "max_value" ), 0.9 );
    d.insert( QStringLiteral( "step_count" ), 3 );
    dims.append( d );
    spec.insert( QStringLiteral( "dimensions" ), dims );
    QJsonObject budget;
    budget.insert( QStringLiteral( "max_runs" ), 9 );
    budget.insert( QStringLiteral( "max_in_flight" ), 2 );
    budget.insert( QStringLiteral( "per_run_timeout_ms" ), 60000 );
    budget.insert( QStringLiteral( "seed_replicates" ), 1 );
    budget.insert( QStringLiteral( "seed" ), 42 );
    spec.insert( QStringLiteral( "budget" ), budget );
    spec.insert( QStringLiteral( "metrics" ), QJsonArray{ QStringLiteral( "maskedPercent" ) } );
    spec.insert( QStringLiteral( "objective_metrics" ), QJsonArray{} );
    spec.insert( QStringLiteral( "objective_metric" ), QString() );
    spec.insert( QStringLiteral( "spatial_comparison" ), false );
    spec.insert( QStringLiteral( "spatial_epsilon" ), 0.0 );
    return spec;
}

sicnu::experiment::MetricRecord makeMetric( const QString &runId,
                                            const QString &datasetVersion,
                                            const QJsonObject &metrics )
{
    sicnu::experiment::MetricRecord record;
    record.runId = runId;
    record.protocol.setDatasetVersionId( datasetVersion );
    record.protocol.setSplitManifestId( QStringLiteral( "sm-1" ) );
    record.protocol.setSubset( QStringLiteral( "test" ) );
    record.protocol.setAggregation( QStringLiteral( "macro" ) );
    record.protocol.setIouThreshold( 0.5 );
    record.metrics = metrics;
    return record;
}

struct CompletedStudioFixture
{
    QString raster;
    QString storePath;
    QString sessionPath;
    QJsonObject design;
    sicnu::experiment_studio::StudioSessionState state;
    sicnu::experiment_studio::StudioSubmission submission;
    sicnu::study::StudyReport report;
};

CompletedStudioFixture seedCompletedStudio( const QTemporaryDir &dir )
{
    CompletedStudioFixture fixture;
    fixture.raster = writeRaster( dir );
    REQUIRE_FALSE( fixture.raster.isEmpty() );
    fixture.storePath = dir.filePath( "experiment.db" );
    {
        sicnu::experiment::ExperimentStore store;
        REQUIRE( store.open( fixture.storePath ) );
        ExperimentStudioDock previous;
        advancedSpecOf( &previous )->setPlainText( QString::fromUtf8(
            QJsonDocument( baseSpec( fixture.raster ) ).toJson() ) );
        previous.applyAdvancedSpec();
        fixture.state = previous.sessionState();
        fixture.sessionPath = previous.studioSessionPath();
    }
    REQUIRE_FALSE( fixture.state.designerDraft.isEmpty() );
    fixture.design = fixture.state.designerDraft;
    auto &submission = fixture.submission;
    submission.submissionId = 100;
    submission.storePath = fixture.storePath;
    submission.studyId = QStringLiteral( "study-gui" );
    submission.experimentId = QStringLiteral( "exp-gui" );
    submission.algorithmId = QStringLiteral( "rs:threshold_raster" );
    submission.specJson = fixture.design.value( QStringLiteral( "draft_spec" ) ).toObject();
    submission.specDigest = sicnu::experiment_studio::studioSpecDigest( submission.specJson );
    submission.designerVm = fixture.design;
    auto &report = fixture.report;
    report.studyId = submission.studyId;
    report.experimentId = submission.experimentId;
    report.algorithmId = submission.algorithmId;
    report.strategy = QStringLiteral( "grid" );
    report.generatedAtUtc = QDateTime::fromString( QStringLiteral( "2026-11-16T00:00:00.000Z" ),
                                                 Qt::ISODateWithMs );
    report.specJson = submission.specJson;
    for ( int i = 0; i < 3; ++i )
    {
        sicnu::study::StudyRunRow row;
        row.pointId = QStringLiteral( "p%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        row.runId = QStringLiteral( "run-A-%1" ).arg( i );
        row.status = QStringLiteral( "recorded" );
        row.seed = 42 + i;
        row.parameterAssignments.insert( QStringLiteral( "threshold" ),
                                          QString::number( 0.1 + 0.4 * i ) );
        row.metrics = QJsonObject{ { QStringLiteral( "maskedPercent" ), 25.0 * ( i + 1 ) },
                                   { QStringLiteral( "maskedPixels" ), 16 * ( i + 1 ) } };
        report.runTable.append( row );
    }
    report.recordedCount = report.runTable.size();
    REQUIRE( sicnu::study::StudyReport::fromJson( report.toJson() ) );
    auto &state = fixture.state;
    state.studyId = report.studyId;
    state.experimentId = report.experimentId;
    state.algorithmId = report.algorithmId;
    state.liveStorePath = fixture.storePath;
    state.reportStorePath = fixture.storePath;
    state.designerStorePath = fixture.storePath;
    state.lastStudyReport = report.toJson();
    state.submissionJson = submission.toJson();
    state.submittedDesignerVm = fixture.design;
    state.submittedSpecDigest = submission.specDigest;
    state.submittedStorePath = fixture.storePath;
    REQUIRE( sicnu::experiment_studio::saveStudioSessionFile( fixture.sessionPath, state ).ok );
    return fixture;
}

sicnu::experiment_studio::StudioExportBundle exportDockToPath(
    ExperimentStudioDock &dock, const QString &path )
{
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs );
    bool chosePath = false;
    QTimer chooser;
    QObject::connect( &chooser, &QTimer::timeout, [&] {
        if ( chosePath ) return;
        for ( QWidget *widget : QApplication::topLevelWidgets() )
        {
            if ( auto *dialog = qobject_cast<QFileDialog *>( widget ) )
            {
                chosePath = true;
                dialog->selectFile( path );
                QMetaObject::invokeMethod( dialog, "accept", Qt::DirectConnection );
                return;
            }
        }
    } );
    chooser.start( 10 );
    dock.exportBundle();
    chooser.stop();
    REQUIRE( chosePath );
    const auto exported = sicnu::experiment_studio::readStudioExportJson( path );
    REQUIRE( exported );
    return exported.value();
}

} // namespace

TEST_CASE( "R30-T07 G1/G2: dock session save failure keeps the previous document",
           "[r30_t07][gui][session]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_session" );
    QTemporaryDir dir;
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );

    QString sessionPath;
    {
        ExperimentStudioDock dock;
        sessionPath = dock.studioSessionPath();
        inputOf( &dock )->setText( raster );
        auto *validate = buttonByText( &dock, QStringLiteral( "Validate Study Design" ) );
        REQUIRE( validate );
        validate->click();
        INFO( summaryOf( &dock )->text().toStdString() );
        REQUIRE( summaryOf( &dock )->text().contains( QStringLiteral( "Valid design" ) ) );
        CHECK( dock.lastSessionSaveError().isEmpty() );
    }
    REQUIRE( QFileInfo::exists( sessionPath ) );
    const QByteArray before = [&] {
        QFile f( sessionPath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    const auto loadedBefore =
        sicnu::experiment_studio::loadStudioSessionFile( sessionPath );
    REQUIRE( loadedBefore.ok() );
    CHECK( loadedBefore.state.designerDraft.value( QStringLiteral( "draft_spec" ) )
               .toObject().value( QStringLiteral( "algorithm_id" ) ).toString()
           == QStringLiteral( "rs:threshold_raster" ) );

    // Failure injection via the sidecar's own typed hooks: a Publish failure
    // leaves the main target untouched (crash-or-valid oracle).
    sicnu::platform::sidecar::WriteRequest request;
    request.targetPath = sessionPath.toUtf8().constData();
    request.bytes = "{\"broken\":true}";
    request.lastGoodSuffix = "";
    request.hooks.failAt = []( sicnu::platform::sidecar::WritePhase phase ) {
        return phase == sicnu::platform::sidecar::WritePhase::Publish;
    };
    const auto publishFailed = sicnu::platform::sidecar::write( request );
    CHECK_FALSE( static_cast<bool>( publishFailed ) );
    CHECK( publishFailed.status == sicnu::platform::sidecar::WriteStatus::PublishFailed );
    QFile after( sessionPath );
    REQUIRE( after.open( QIODevice::ReadOnly ) );
    CHECK( after.readAll() == before );

    // The restored dock still sees the valid, unchanged design.
    ExperimentStudioDock reopened;
    CHECK( reopened.sessionState().designerDraft.value( QStringLiteral( "draft_spec" ) )
               .toObject().value( QStringLiteral( "algorithm_id" ) ).toString()
           == QStringLiteral( "rs:threshold_raster" ) );
    CHECK( summaryOf( &reopened )->text().contains( QStringLiteral( "restored" ) ) );
}

TEST_CASE( "R30-T07 G1: a dock-level save failure is surfaced, old document intact",
           "[r30_t07][gui][session_failure]" )
{
    // R1 P1-8: exercise ExperimentStudioDock::savePersistedSession() itself,
    // not the sidecar in isolation. Turning the session path into a directory
    // makes every subsequent save fail closed; the previous document must
    // remain readable and lastSessionSaveError() must name the typed reason.
    ensureApp();
    resetSessionDir( "r30_t07_gui_dock_save_fail" );
    QTemporaryDir dir;
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );
    ExperimentStudioDock dock;
    const QString sessionPath = dock.studioSessionPath();
    inputOf( &dock )->setText( raster );
    auto *validate = buttonByText( &dock, QStringLiteral( "Validate Study Design" ) );
    REQUIRE( validate );
    validate->click();
    REQUIRE( summaryOf( &dock )->text().contains( QStringLiteral( "Valid design" ) ) );
    REQUIRE( QFileInfo::exists( sessionPath ) );
    const QByteArray before = [&] {
        QFile f( sessionPath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    REQUIRE( QFile::remove( sessionPath ) );
    REQUIRE( QDir().mkdir( sessionPath ) );
    // Real control edit → invalidateDesignerDraft → savePersistedSession.
    inputOf( &dock )->setText( raster + QStringLiteral( "-changed" ) );
    CHECK_FALSE( dock.lastSessionSaveError().isEmpty() );
    CHECK( dock.lastSessionSaveError().contains(
        QStringLiteral( "experiment_studio.session_not_a_file" ) ) );
    CHECK_FALSE( summaryOf( &dock )->text().contains( QStringLiteral( "Valid design" ) ) );
    // A PRE-publish failure keeps the old document: the summary says so.
    CHECK( summaryOf( &dock )->text().contains( QStringLiteral( "left intact" ) ) );
    CHECK_FALSE( summaryOf( &dock )->text().contains(
        QStringLiteral( "Re-open the Studio" ) ) );

    REQUIRE( QDir().rmdir( sessionPath ) );
    QFile restored( sessionPath );
    REQUIRE( restored.open( QIODevice::WriteOnly ) );
    restored.write( before );
    restored.close();
    const auto reopened = sicnu::experiment_studio::loadStudioSessionFile( sessionPath );
    REQUIRE( reopened.ok() );
    CHECK( reopened.state.designerDraft.value( QStringLiteral( "draft_spec" ) )
               .toObject().value( QStringLiteral( "algorithm_id" ) ).toString()
           == QStringLiteral( "rs:threshold_raster" ) );
}

TEST_CASE( "R30-T07 G2: editing a field invalidates the validated draft",
           "[r30_t07][gui][invalidation]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_invalidate" );
    QTemporaryDir dir;
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );
    ExperimentStudioDock dock;
    inputOf( &dock )->setText( raster );
    auto *validate = buttonByText( &dock, QStringLiteral( "Validate Study Design" ) );
    REQUIRE( validate );
    validate->click();
    REQUIRE_FALSE( dock.sessionState().designerDraft.isEmpty() );

    // A single widget edit must clear the stale "valid" marker.
    auto *steps = dock.findChild<QSpinBox *>();
    REQUIRE( steps );
    steps->setValue( steps->value() + 1 );
    CHECK( dock.sessionState().designerDraft.isEmpty() );
    CHECK( summaryOf( &dock )->text().contains( QStringLiteral( "Design changed" ) ) );
}

TEST_CASE( "R30-T07 G2: full v1 spec editing, restore, and nested-dimension refusal",
           "[r30_t07][gui][full_spec]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_full_spec" );
    QTemporaryDir dir;
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );
    QJsonObject spec = baseSpec( raster );
    // Two REAL flat numeric dimensions of the registered operator schema
    // (threshold and percentile) and TWO metrics: the complete spec, not
    // just the first item. 3 x 2 = 6 grid points, within the 9-run budget.
    QJsonArray dims = spec.value( QStringLiteral( "dimensions" ) ).toArray();
    QJsonObject d2;
    d2.insert( QStringLiteral( "parameter_path" ), QStringLiteral( "percentile" ) );
    d2.insert( QStringLiteral( "min_value" ), 20.0 );
    d2.insert( QStringLiteral( "max_value" ), 80.0 );
    d2.insert( QStringLiteral( "step_count" ), 2 );
    dims.append( d2 );
    spec.insert( QStringLiteral( "dimensions" ), dims );
    spec.insert( QStringLiteral( "metrics" ),
                 QJsonArray{ QStringLiteral( "maskedPercent" ),
                             QStringLiteral( "maskedPixels" ) } );

    {
        ExperimentStudioDock dock;
        auto *editor = advancedSpecOf( &dock );
        REQUIRE( editor );
        editor->setPlainText( QString::fromUtf8(
            QJsonDocument( spec ).toJson( QJsonDocument::Indented ) ) );
        auto *apply = dock.findChild<QPushButton *>(
            QStringLiteral( "rsStudioApplyFullSpecBtn" ) );
        REQUIRE( apply );
        apply->click();
        INFO( summaryOf( &dock )->text().toStdString() );
        REQUIRE( summaryOf( &dock )->text().contains( QStringLiteral( "Valid full spec" ) ) );
        REQUIRE_FALSE( dock.sessionState().designerDraft.isEmpty() );
        // The persisted draft carries BOTH dimensions and BOTH metrics.
        const QJsonObject normalized =
            dock.sessionState().designerDraft.value( QStringLiteral( "draft_spec" ) ).toObject();
        CHECK( normalized.value( QStringLiteral( "dimensions" ) ).toArray().size() == 2 );
        CHECK( normalized.value( QStringLiteral( "metrics" ) ).toArray().size() == 2 );
        CHECK( dock.sessionState().designerDraft.value(
                   QStringLiteral( "estimated_points" ) ).toDouble() == 6.0 );
    }

    // A restart must restore the FULL spec, not collapse to one dimension.
    {
        ExperimentStudioDock reopened;
        const auto *editor = advancedSpecOf( &reopened );
        REQUIRE( editor );
        const QJsonObject restored =
            QJsonDocument::fromJson( editor->toPlainText().toUtf8() ).object();
        REQUIRE_FALSE( restored.isEmpty() );
        CHECK( restored.value( QStringLiteral( "dimensions" ) ).toArray().size() == 2 );
        CHECK( restored.value( QStringLiteral( "metrics" ) ).toArray().size() == 2 );
    }

    // Nested / non-numeric sweeps are explicitly refused by the v1 editor.
    ExperimentStudioDock dock2;
    auto *editor2 = advancedSpecOf( &dock2 );
    REQUIRE( editor2 );
    QJsonObject nested = spec;
    QJsonArray nestedDims;
    QJsonObject nd = d2;
    nd.insert( QStringLiteral( "parameter_path" ), QStringLiteral( "params.threshold" ) );
    nestedDims.append( nd );
    nested.insert( QStringLiteral( "dimensions" ), nestedDims );
    editor2->setPlainText( QString::fromUtf8(
        QJsonDocument( nested ).toJson( QJsonDocument::Indented ) ) );
    dock2.findChild<QPushButton *>( QStringLiteral( "rsStudioApplyFullSpecBtn" ) )->click();
    CHECK( summaryOf( &dock2 )->text().contains( QStringLiteral( "unsupported nested dimension" ) ) );
    CHECK( dock2.sessionState().designerDraft.isEmpty() );
}

TEST_CASE( "R30-T07 G2: an unconsumed/expired callback never overwrites the session",
           "[r30_t07][gui][stale_callback]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_stale" );
    QTemporaryDir dir;
    ExperimentStudioDock dock;
    CHECK_FALSE( dock.activeSubmission().has_value() );
    // A STRUCTURALLY complete-looking report still cannot land when no
    // submission is active (R1 P1-1: one delivery consumes one identity).
    QJsonObject spec;
    spec.insert( QStringLiteral( "algorithm_id" ), QStringLiteral( "rs:threshold_raster" ) );
    spec.insert( QStringLiteral( "max_runs" ), 4 );
    QJsonObject report;
    report.insert( QStringLiteral( "document_type" ),
                   QStringLiteral( "sicnu.studyreport.v1" ) );
    report.insert( QStringLiteral( "spec" ), spec );
    report.insert( QStringLiteral( "study_id" ), QStringLiteral( "stale-study" ) );
    report.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "stale-exp" ) );
    report.insert( QStringLiteral( "run_table" ), QJsonArray{} );
    for ( quint64 id : { quint64( 999 ), quint64( 0 ) } )
        CHECK_FALSE( dock.deliverStudyResult( id, report, 3, 0, 0, QString() ) );
    CHECK( dock.sessionState().lastStudyReport.isEmpty() );
    CHECK( dock.sessionState().studyId != QStringLiteral( "stale-study" ) );
    CHECK( dock.sessionState().submittedSpecDigest.isEmpty() );
    CHECK_FALSE( dock.activeSubmission().has_value() );
}

TEST_CASE( "R30-T07 G2: a refused live delivery re-arms the dock and consumes its identity",
           "[r30_t07][gui][delivery_lifecycle]" )
{
    // R2 P1-A regression. runLiveStudy() freezes the submission and starts a
    // worker synchronously; the worker's queued delivery needs an event loop,
    // so a synchronous mismatched delivery is deterministic here. EVERY exit
    // must reap the worker, re-enable the buttons and consume the identity.
    ensureApp();
    resetSessionDir( "r30_t07_gui_delivery_lifecycle" );
    QTemporaryDir dir;
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );
    const QString storePath = dir.filePath( "experiment.db" );
    {
        sicnu::experiment::ExperimentStore store;
        REQUIRE( store.open( storePath ) );
    }
    ExperimentStudioDock dock;
    REQUIRE( dock.openLiveStoreAtPath( storePath ) );
    inputOf( &dock )->setText( raster );
    auto *runBtn = dock.findChild<QPushButton *>( QStringLiteral( "rsStudioRunLiveBtn" ) );
    auto *importBtn =
        dock.findChild<QPushButton *>( QStringLiteral( "rsStudioImportBundleBtn" ) );
    auto *openBtn = buttonByText( &dock, QStringLiteral( "Open experiment store…" ) );
    REQUIRE( runBtn );
    REQUIRE( importBtn );
    REQUIRE( openBtn );

    runBtn->click(); // synchronous: active submission frozen, worker started
    REQUIRE( dock.activeSubmission().has_value() );
    const quint64 id = dock.activeSubmission()->submissionId;
    CHECK_FALSE( runBtn->isEnabled() );
    CHECK_FALSE( openBtn->isEnabled() );
    CHECK_FALSE( importBtn->isEnabled() );
    CHECK_FALSE( dock.sessionState().pendingSubmissionJson.isEmpty() );

    // A report whose spec digest does not match the frozen submission.
    QJsonObject foreignSpec;
    foreignSpec.insert( QStringLiteral( "algorithm_id" ),
                        QStringLiteral( "rs:some_other_operator" ) );
    foreignSpec.insert( QStringLiteral( "max_runs" ), 999 );
    QJsonObject mismatched;
    mismatched.insert( QStringLiteral( "document_type" ),
                       QStringLiteral( "sicnu.studyreport.v1" ) );
    mismatched.insert( QStringLiteral( "spec" ), foreignSpec );
    mismatched.insert( QStringLiteral( "study_id" ), QStringLiteral( "foreign-study" ) );
    CHECK_FALSE( dock.deliverStudyResult( id, mismatched, 0, 0, 0, QString() ) );

    // The refusal must not leave the dock wedged or the identity reusable.
    CHECK_FALSE( dock.activeSubmission().has_value() );
    CHECK( runBtn->isEnabled() );
    CHECK( openBtn->isEnabled() );
    CHECK( importBtn->isEnabled() );
    CHECK( dock.sessionState().submissionJson.isEmpty() );
    CHECK( dock.sessionState().lastStudyReport.isEmpty() );
    CHECK( dock.sessionState().studyId != QStringLiteral( "foreign-study" ) );

    // The same id can never be delivered twice (late/duplicate callback).
    CHECK_FALSE( dock.deliverStudyResult( id, mismatched, 0, 0, 0, QString() ) );
    CHECK_FALSE( dock.activeSubmission().has_value() );
    CHECK( runBtn->isEnabled() );

    // Even an empty payload from another id is refused without re-wedging.
    CHECK_FALSE( dock.deliverStudyResult( id + 1, QJsonObject{}, 0, 0, 0, QString() ) );
    CHECK( runBtn->isEnabled() );
}

TEST_CASE( "R30-T07 G2: a failed next submission exports the last successful design",
           "[r30_t07][gui][failed_submission_export][metric_projection_identity]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_failed_submission_export" );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fixture = seedCompletedStudio( dir );
    const QString raster = fixture.raster;
    const QString storePath = fixture.storePath;
    const QString sessionPath = fixture.sessionPath;
    const QJsonObject designA = fixture.design;
    const auto &submissionA = fixture.submission;
    const auto &reportA = fixture.report;

    ExperimentStudioDock dock;
    REQUIRE( dock.openLiveStoreAtPath( storePath ) );
    REQUIRE( dock.sessionState().lastStudyReport == reportA.toJson() );
    QJsonObject specB = baseSpec( raster );
    specB.insert( QStringLiteral( "study_id" ), QStringLiteral( "study-B" ) );
    specB.insert( QStringLiteral( "sampling" ),
                  QJsonObject{ { QStringLiteral( "strategy" ), QStringLiteral( "oat" ) } } );
    specB.insert( QStringLiteral( "metrics" ), QJsonArray{ QStringLiteral( "maskedPixels" ) } );
    advancedSpecOf( &dock )->setPlainText( QString::fromUtf8( QJsonDocument( specB ).toJson() ) );
    dock.applyAdvancedSpec();
    REQUIRE( dock.sessionState().designerDraft != designA );
    dock.runLiveStudy();
    REQUIRE( dock.activeSubmission().has_value() );
    const quint64 idB = dock.activeSubmission()->submissionId;
    const QJsonObject pendingB = dock.activeSubmission()->toJson();
    const auto inFlight = sicnu::experiment_studio::loadStudioSessionFile( sessionPath );
    REQUIRE( inFlight.ok() );
    CHECK( inFlight.state.toJson().value( QStringLiteral( "pending_submission" ) ).toObject()
           == pendingB );
    {
        // A crash/restart while B is pending must retain A's report binding;
        // restoring a pending DTO neither resumes a worker nor blesses B.
        ExperimentStudioDock recovered;
        CHECK_FALSE( recovered.activeSubmission().has_value() );
        CHECK( recovered.sessionState().lastStudyReport == reportA.toJson() );
        CHECK( recovered.sessionState().submissionJson == submissionA.toJson() );
        CHECK( recovered.openLiveStoreAtPath( storePath ) );
        auto *run = recovered.findChild<QPushButton *>( QStringLiteral( "rsStudioRunLiveBtn" ) );
        REQUIRE( run );
        CHECK( run->isEnabled() );
    }

    SECTION( "worker failure" )
    {
        CHECK_FALSE( dock.deliverStudyResult( idB, {}, 0, 0, 0,
                                              QStringLiteral( "study.run_failed: injected" ) ) );
    }
    SECTION( "empty successful payload" )
    {
        CHECK_FALSE( dock.deliverStudyResult( idB, {}, 0, 0, 0, {} ) );
    }
    SECTION( "foreign report" )
    {
        CHECK_FALSE( dock.deliverStudyResult( idB, reportA.toJson(), 0, 0, 0, {} ) );
    }
    CHECK_FALSE( dock.activeSubmission().has_value() );
    CHECK( dock.sessionState().lastStudyReport == reportA.toJson() );

    // Export the already applied A projection, even while the current draft
    // and its metric field describe B. The rows and their outer identity
    // must be one snapshot, rather than old rows wrapped in current controls.
    const auto exported = exportDockToPath( dock, dir.filePath( "after-failed-B.json" ) );
    CHECK( exported.studyReport == reportA.toJson() );
    CHECK( exported.designer == designA );
    CHECK( exported.submission == submissionA.toJson() );
    const QString protocolA = QStringLiteral( "study/rs:threshold_raster/grid" );
    CHECK( exported.metricCells.value( QStringLiteral( "metric" ) ).toString()
           == QStringLiteral( "maskedPercent" ) );
    CHECK( exported.metricCells.value( QStringLiteral( "protocol" ) ).toString() == protocolA );
    const auto originalCells = exported.metricCells.value( QStringLiteral( "cells" ) ).toArray();
    REQUIRE( originalCells.size() == 3 );
    for ( const auto &entry : originalCells )
    {
        CHECK( entry.toObject().value( QStringLiteral( "metric" ) ).toString()
               == QStringLiteral( "maskedPercent" ) );
        CHECK( entry.toObject().value( QStringLiteral( "protocol" ) ).toString() == protocolA );
    }
    QLineEdit *metric = nullptr;
    QLineEdit *filter = nullptr;
    for ( QLineEdit *edit : dock.findChildren<QLineEdit *>() )
    {
        if ( edit->text() == QStringLiteral( "maskedPixels" ) ) metric = edit;
        if ( edit->placeholderText().contains( QStringLiteral( "Filter point id" ) ) ) filter = edit;
    }
    REQUIRE( metric );
    REQUIRE( filter );
    metric->setText( QStringLiteral( "notAppliedMetric" ) );
    const auto unapplied = exportDockToPath( dock, dir.filePath( "unapplied-metric.json" ) );
    CHECK( unapplied.metricCells == exported.metricCells );

    // Applying the filter may choose another metric, but the report's
    // algorithm/strategy provenance remains A. Editing the filter afterwards
    // is also only a draft until the user applies it.
    metric->setText( QStringLiteral( "maskedPixels" ) );
    filter->setText( QStringLiteral( "p0001" ) );
    auto *filterButton = buttonByText( &dock, QStringLiteral( "Filter" ) );
    REQUIRE( filterButton );
    filterButton->click();
    const auto appliedCell = dock.lastMetricCell( 0 );
    CHECK( appliedCell.value( QStringLiteral( "protocol" ) ).toString() == protocolA );
    CHECK( appliedCell.value( QStringLiteral( "metric" ) ).toString() == QStringLiteral( "maskedPixels" ) );
    CHECK( appliedCell.value( QStringLiteral( "run_id" ) ).toString() == QStringLiteral( "run-A-1" ) );
    auto *table = dock.findChild<QTableWidget *>( QStringLiteral( "rsStudioMatrixTable" ) );
    REQUIRE( table );
    REQUIRE( table->rowCount() == 1 );
    CHECK( table->horizontalHeaderItem( 4 )->text().contains( protocolA ) );
    filter->setText( QStringLiteral( "p0000" ) ); // deliberately NOT applied
    const auto filtered = exportDockToPath( dock, dir.filePath( "applied-filter.json" ) );
    CHECK( filtered.metricCells.value( QStringLiteral( "metric" ) ).toString()
           == QStringLiteral( "maskedPixels" ) );
    CHECK( filtered.metricCells.value( QStringLiteral( "protocol" ) ).toString() == protocolA );
    CHECK( filtered.metricCells.value( QStringLiteral( "cells" ) ).toArray() == QJsonArray{ appliedCell } );
    CHECK( filtered.metricCells.value( QStringLiteral( "page" ) ).toObject() == dock.matrixPage().toJson() );

}

TEST_CASE( "R30-T07 G2: restored submission corruption cannot certify another design",
           "[r30_t07][gui][restored_submission_integrity]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_submission_integrity" );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    auto fixture = seedCompletedStudio( dir );
    auto persisted = fixture.submission.toJson();
    QJsonObject foreignSpec = fixture.submission.specJson;
    foreignSpec.insert( QStringLiteral( "sampling" ),
                        QJsonObject{ { QStringLiteral( "strategy" ), QStringLiteral( "oat" ) } } );
    REQUIRE( sicnu::experiment_studio::studioSpecDigest( foreignSpec )
             != fixture.submission.specDigest );
    bool legacy = false;
    SECTION( "submission spec changed, original digest retained" )
    {
        persisted.insert( QStringLiteral( "spec" ), foreignSpec );
    }
    SECTION( "designer VM spec changed, original submission retained" )
    {
        auto vm = fixture.design;
        vm.insert( QStringLiteral( "draft_spec" ), foreignSpec );
        persisted.insert( QStringLiteral( "designer_vm" ), vm );
    }
    SECTION( "present VM spec has a foreign type" )
    {
        auto vm = fixture.design;
        vm.insert( QStringLiteral( "draft_spec" ), QStringLiteral( "not a spec object" ) );
        persisted.insert( QStringLiteral( "designer_vm" ), vm );
    }
    SECTION( "legacy VM without a spec echo remains readable" )
    {
        auto vm = fixture.design;
        vm.remove( QStringLiteral( "draft_spec" ) );
        persisted.insert( QStringLiteral( "designer_vm" ), vm );
        legacy = true;
    }
    fixture.state.submissionJson = persisted;
    REQUIRE( sicnu::experiment_studio::saveStudioSessionFile(
        fixture.sessionPath, fixture.state ).ok );
    ExperimentStudioDock recovered;
    REQUIRE( recovered.sessionState().lastStudyReport == fixture.report.toJson() );
    const QString path = dir.filePath( "restored-export.json" );
    const auto exported = exportDockToPath( recovered, path );
    CHECK( exported.studyReport == fixture.report.toJson() );
    if ( legacy )
    {
        CHECK( exported.submission == persisted );
        CHECK( exported.designer == persisted.value( QStringLiteral( "designer_vm" ) ).toObject() );
    }
    else
    {
        CHECK( exported.submission.isEmpty() );
        CHECK( exported.designer.isEmpty() );
        QFile file( path );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        CHECK_FALSE( QJsonDocument::fromJson( file.readAll() ).object()
                         .value( QStringLiteral( "issues" ) ).toArray().isEmpty() );
    }
}

TEST_CASE( "R30-T07 G2: restoring only a pending submission never invents a successful report",
           "[r30_t07][gui][pending_restore]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_pending_only_restore" );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeRaster( dir );
    REQUIRE_FALSE( raster.isEmpty() );
    const QString storePath = dir.filePath( "experiment.db" );
    {
        sicnu::experiment::ExperimentStore store;
        REQUIRE( store.open( storePath ) );
    }
    ExperimentStudioDock running;
    REQUIRE( running.openLiveStoreAtPath( storePath ) );
    inputOf( &running )->setText( raster );
    running.runLiveStudy();
    REQUIRE( running.activeSubmission().has_value() );
    const QJsonObject pending = running.activeSubmission()->toJson();
    ExperimentStudioDock recovered;
    CHECK_FALSE( recovered.activeSubmission().has_value() );
    CHECK( recovered.sessionState().lastStudyReport.isEmpty() );
    CHECK( recovered.sessionState().submissionJson.isEmpty() );
    CHECK( recovered.sessionState().toJson()
               .value( QStringLiteral( "pending_submission" ) ).toObject() == pending );
    CHECK( recovered.openLiveStoreAtPath( storePath ) );
    auto *run = recovered.findChild<QPushButton *>( QStringLiteral( "rsStudioRunLiveBtn" ) );
    REQUIRE( run );
    CHECK( run->isEnabled() );
    recovered.exportBundle(); // no report: refuses before any save dialog
    CHECK( recovered.sessionState().lastExportPath.isEmpty() );
}

TEST_CASE( "R30-T07 G3: matrix shows typed missing metrics and navigable pages",
           "[r30_t07][gui][matrix]" )
{
    ensureApp();
    resetSessionDir( "r30_t07_gui_matrix" );
    qputenv( "SICNU_UI_DEMO_MODE", QByteArrayLiteral( "1" ) );
    ExperimentStudioDock dock;
    auto *steps = dock.findChild<QSpinBox *>();
    REQUIRE( steps );
    steps->setValue( 800 );
    dock.applySyntheticRunMatrix();
    auto *table = dock.findChild<QTableWidget *>( QStringLiteral( "rsStudioMatrixTable" ) );
    REQUIRE( table );
    CHECK( table->rowCount() == 500 ); // bounded first page, R29 contract kept
    CHECK( dock.matrixPage().totalRows == 800 );
    CHECK( dock.matrixPage().pageCount == 2 );
    const auto *status = dock.findChild<QLabel *>( QStringLiteral( "rsStudioMatrixStatus" ) );
    REQUIRE( status );
    CHECK( status->text().contains( QStringLiteral( "preview limited to first 500 of 800 rows" ) ) );

    auto *next = dock.findChild<QPushButton *>( QStringLiteral( "rsStudioMatrixNextPage" ) );
    REQUIRE( next );
    REQUIRE( next->isEnabled() );
    next->click();
    CHECK( dock.matrixPage().page == 1 );
    CHECK( table->rowCount() == 300 );
    CHECK( table->item( 299, 0 )->text() == QStringLiteral( "p0799" ) );
    // R1 P2-4: the "first 500" preview note must NOT appear once the user
    // is actually browsing the second page.
    CHECK_FALSE( status->text().contains( QStringLiteral( "preview limited" ) ) );

    // R1 P1-2: a real cross-page selection must survive a page round trip.
    // Select one point on page 1, then another on page 0; both must remain
    // in the stable (point#rep) memory, not be replaced by the visible page.
    const QString page1Key = table->item( 10, 0 )->text() + QLatin1Char( '#' )
                             + table->item( 10, 1 )->text();
    table->selectRow( 10 );
    auto *prev = dock.findChild<QPushButton *>( QStringLiteral( "rsStudioMatrixPrevPage" ) );
    REQUIRE( prev );
    prev->click();
    CHECK( dock.matrixPage().page == 0 );
    CHECK( dock.matrixPage().selectedKeys.contains( page1Key ) );
    const QString page0Key = table->item( 5, 0 )->text() + QLatin1Char( '#' )
                             + table->item( 5, 1 )->text();
    table->selectRow( 5 );
    // selectedKeys is a rebuild-time snapshot; force one rebuild and then
    // verify BOTH pages' keys survive two further navigations.
    next->click();
    CHECK( dock.matrixPage().page == 1 );
    CHECK( dock.matrixPage().selectedKeys.contains( page1Key ) );
    CHECK( dock.matrixPage().selectedKeys.contains( page0Key ) );
    CHECK( table->selectionModel()->isRowSelected( 10, QModelIndex() ) );
    prev->click();
    CHECK( dock.matrixPage().page == 0 );
    CHECK( dock.matrixPage().selectedKeys.contains( page1Key ) );
    CHECK( dock.matrixPage().selectedKeys.contains( page0Key ) );
    CHECK( table->selectionModel()->isRowSelected( 5, QModelIndex() ) );

    // Change the selected metric to one the demo report never recorded:
    // every cell must be an honest "missing" state, NEVER the other metric's
    // number or a zero.
    QLineEdit *metricEdit = nullptr;
    for ( QLineEdit *edit : dock.findChildren<QLineEdit *>() )
        if ( edit->text() == QStringLiteral( "maskedPercent" ) )
            metricEdit = edit;
    REQUIRE( metricEdit );
    metricEdit->setText( QStringLiteral( "neverRecorded" ) );
    auto *filterBtn = buttonByText( &dock, QStringLiteral( "Filter" ) );
    REQUIRE( filterBtn );
    filterBtn->click(); // real control: resets to page 0 and rebuilds
    const QJsonObject cell = dock.lastMetricCell( 0 );
    REQUIRE_FALSE( cell.isEmpty() );
    CHECK( cell.value( QStringLiteral( "kind" ) ).toString()
           == QStringLiteral( "missing_metric" ) );
    CHECK_FALSE( cell.contains( QStringLiteral( "value" ) ) );
    CHECK( table->item( 0, 4 )->text().contains( QStringLiteral( "not recorded" ) ) );
    CHECK( table->horizontalHeaderItem( 4 )->text().contains(
        QStringLiteral( "neverRecorded" ) ) );
    qunsetenv( "SICNU_UI_DEMO_MODE" );
}

TEST_CASE( "R30-T07 G3/G4: comparison uses REAL label schema evidence and stable pins",
           "[r30_t07][gui][comparison]" )
{
    ensureApp();
    QTemporaryDir dir;
    const QString experimentPath = dir.filePath( "experiment.db" );
    const QString datasetPath = dir.filePath( "dataset.db" );

    // Real committed dataset version carrying a REAL label schema ref.
    sicnu::dataset::DatasetStore datasets;
    REQUIRE( datasets.open( datasetPath ) );
    // LabelSchema::validate() requires a UUID-shaped schema_id; a plain
    // label is refused with dataset.label_schema_invalid.
    const QString schemaId = QStringLiteral( "22222222-3333-4444-5555-666666666666" );
    sicnu::dataset::LabelSchema schema;
    schema.setSchemaId( schemaId );
    schema.setVersion( 1 );
    schema.setName( QStringLiteral( "land cover" ) );
    sicnu::dataset::LabelClass water;
    water.setStableId( QStringLiteral( "11111111-2222-3333-4444-555555555555" ) );
    water.setCode( QStringLiteral( "water" ) );
    schema.classes().append( water );
    REQUIRE( datasets.saveLabelSchema( schema ).isOk() );

    sicnu::dataset::DatasetManifest manifest;
    const auto datasetId = sicnu::dataset::DatasetId::generate();
    manifest.setDatasetId( datasetId.toString() );
    const auto versionId = sicnu::dataset::DatasetVersionId::generate();
    manifest.setVersionId( versionId.toString() );
    manifest.labelSchema().schemaId = schemaId;
    manifest.labelSchema().version = 1;
    REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
    const auto draft = datasets.createDraftVersion( manifest );
    REQUIRE( draft.has_value() );
    REQUIRE( datasets.stageVersion( versionId ).has_value() );
    REQUIRE( datasets.commitVersion( versionId ).has_value() );

    sicnu::experiment::ExperimentStore experiments;
    REQUIRE( experiments.open( experimentPath ) );
    sicnu::experiment::Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-r30" ) );
    exp.setName( QStringLiteral( "schema evidence" ) );
    REQUIRE( experiments.upsertExperiment( exp ) );
    auto addRun = [&]( const QString &runId ) {
        sicnu::experiment::ExperimentRun run;
        run.setRunId( runId );
        run.setExperimentId( QStringLiteral( "exp-r30" ) );
        run.setAlgorithmId( QStringLiteral( "rs:threshold_raster" ) );
        run.setDatasetVersionId( versionId.toString() );
        REQUIRE( experiments.upsertRunWithMetric(
            run, makeMetric( runId, versionId.toString(),
                             QJsonObject{ { QStringLiteral( "accuracy" ), 0.8 } } ) ) );
    };
    addRun( QStringLiteral( "run-r30-a" ) );
    addRun( QStringLiteral( "run-r30-b" ) );

    DatasetExperimentPanel panel;
    REQUIRE( panel.openDatasetStoreAtPath( datasetPath ) );
    REQUIRE( panel.openExperimentStoreAtPath( experimentPath ) );
    auto *table = panel.findChild<QTableWidget *>(
        QStringLiteral( "rsExperimentRunsTable" ) );
    REQUIRE( table );
    REQUIRE( table->rowCount() >= 2 );

    // Pin A on row 0, then move selection to row 1 and pin B — an explicit
    // cross-row pair resolved by RUN ID, never by the first two visible rows.
    auto *pinA = panel.findChild<QPushButton *>( QStringLiteral( "rsExperimentPinA" ) );
    auto *pinB = panel.findChild<QPushButton *>( QStringLiteral( "rsExperimentPinB" ) );
    REQUIRE( pinA ); REQUIRE( pinB );
    table->clearSelection();
    table->selectRow( 0 );
    pinA->click();
    table->clearSelection();
    table->selectRow( 1 );
    pinB->click();
    const auto *pinLabel = panel.findChild<QLabel *>(
        QStringLiteral( "rsExperimentPinLabel" ) );
    REQUIRE( pinLabel );
    CHECK( pinLabel->text().contains( QStringLiteral( "run-r30-a" ) ) );
    CHECK( pinLabel->text().contains( QStringLiteral( "run-r30-b" ) ) );

    auto *compare = buttonByText( &panel, QStringLiteral( "Compare Selected Runs" ) );
    REQUIRE( compare );
    REQUIRE( compare->isEnabled() );
    compare->click();
    QPlainTextEdit *detail = nullptr;
    for ( QPlainTextEdit *edit : panel.findChildren<QPlainTextEdit *>() )
        if ( edit->placeholderText().contains( QStringLiteral( "Run metrics" ) ) )
            detail = edit;
    REQUIRE( detail );
    const QJsonObject report = QJsonDocument::fromJson( detail->toPlainText().toUtf8() ).object();
    INFO( detail->toPlainText().toStdString() );
    CHECK( report.value( QStringLiteral( "schema_evidence" ) )
               .toObject().value( QStringLiteral( "checked" ) ).toBool() );
    CHECK( report.value( QStringLiteral( "comparable_with_schema_evidence" ) ).toBool() );
    CHECK( report.value( QStringLiteral( "schema_evidence" ) )
               .toObject().value( QStringLiteral( "verdict" ) ).toString()
           == QStringLiteral( "compatible" ) );
    CHECK( report.contains( QStringLiteral( "paired_summary" ) ) );
    CHECK( report.contains( QStringLiteral( "protocol_compatibility" ) ) );

    // Clear pins: compare falls back to the two visible selected rows.
    auto *clear = panel.findChild<QPushButton *>(
        QStringLiteral( "rsExperimentClearPins" ) );
    REQUIRE( clear );
    clear->click();
    CHECK( pinLabel->text().contains( QStringLiteral( "(none)" ) ) );
}

TEST_CASE( "R30-T07 G3/G4: missing dataset store means schema NOT checked",
           "[r30_t07][gui][schema_unchecked]" )
{
    ensureApp();
    QTemporaryDir dir;
    const QString experimentPath = dir.filePath( "experiment.db" );
    sicnu::experiment::ExperimentStore experiments;
    REQUIRE( experiments.open( experimentPath ) );
    sicnu::experiment::Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-nods" ) );
    exp.setName( QStringLiteral( "no dataset store" ) );
    REQUIRE( experiments.upsertExperiment( exp ) );
    for ( const QString &id : { QStringLiteral( "run-a" ), QStringLiteral( "run-b" ) } )
    {
        sicnu::experiment::ExperimentRun run;
        run.setRunId( id );
        run.setExperimentId( QStringLiteral( "exp-nods" ) );
        run.setAlgorithmId( QStringLiteral( "rs:threshold_raster" ) );
        run.setDatasetVersionId( QStringLiteral( "dv-1" ) );
        REQUIRE( experiments.upsertRunWithMetric(
            run, makeMetric( id, QStringLiteral( "dv-1" ),
                             QJsonObject{ { QStringLiteral( "accuracy" ), 0.7 } } ) ) );
    }
    DatasetExperimentPanel panel;
    REQUIRE( panel.openExperimentStoreAtPath( experimentPath ) );
    auto *table = panel.findChild<QTableWidget *>(
        QStringLiteral( "rsExperimentRunsTable" ) );
    REQUIRE( table );
    REQUIRE( table->rowCount() == 2 );
    table->setRangeSelected(
        QTableWidgetSelectionRange( 0, 0, 1, table->columnCount() - 1 ), true );
    auto *compare = buttonByText( &panel, QStringLiteral( "Compare Selected Runs" ) );
    REQUIRE( compare );
    REQUIRE( compare->isEnabled() );
    compare->click();
    QPlainTextEdit *detail = nullptr;
    for ( QPlainTextEdit *edit : panel.findChildren<QPlainTextEdit *>() )
        if ( edit->placeholderText().contains( QStringLiteral( "Run metrics" ) ) )
            detail = edit;
    REQUIRE( detail );
    const QJsonObject report = QJsonDocument::fromJson( detail->toPlainText().toUtf8() ).object();
    CHECK_FALSE( report.value( QStringLiteral( "schema_evidence" ) )
                     .toObject().value( QStringLiteral( "checked" ) ).toBool() );
    CHECK( report.value( QStringLiteral( "schema_evidence" ) )
               .toObject().value( QStringLiteral( "verdict" ) ).toString()
           == QStringLiteral( "not_checked_missing_evidence" ) );
    CHECK_FALSE( report.value( QStringLiteral( "comparable_with_schema_evidence" ) ).toBool() );
    // The unverified default must not masquerade as a checked verdict.
    CHECK( report.value( QStringLiteral( "paired_summary" ) )
               .toObject().value( QStringLiteral( "schemas_compatible" ) ).isNull() );
    CHECK( report.value( QStringLiteral( "comparability_note" ) )
               .toString().contains( QStringLiteral( "NOT checked" ) ) );
}
