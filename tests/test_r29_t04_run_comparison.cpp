// test_r29_t04_run_comparison.cpp — D3 (R29-T04).
//
// The dataset/experiment panel's run comparison must consume the experiment
// layer's comparability authority (compareProtocols + pairedRunComparison):
// different protocols are explained, not silently side-by-side; missing
// metrics stay visible one-sided; support is never turned into a zero or a
// winner. Cross-page selection is keyed by run id and re-resolves after a
// rebuild. Real ExperimentStore + real panel, offscreen.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include "app/workbench/dataset_experiment_panel.h"
#include "experiment/evaluation.h"
#include "experiment/experiment_store.h"

using sicnu::app::DatasetExperimentPanel;
using sicnu::experiment::EvaluationProtocol;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRun;
using sicnu::experiment::MetricRecord;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_r29_t04_run_comparison";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

MetricRecord makeMetric( const QString &runId, const QString &datasetVersion,
                         const QString &splitManifest, double iouThreshold,
                         const QJsonObject &metrics )
{
    MetricRecord record;
    record.runId = runId;
    record.protocol.setDatasetVersionId( datasetVersion );
    record.protocol.setSplitManifestId( splitManifest );
    record.protocol.setSubset( QStringLiteral( "test" ) );
    record.protocol.setAggregation( QStringLiteral( "macro" ) );
    record.protocol.setIouThreshold( iouThreshold );
    record.metrics = metrics;
    return record;
}

/// Seeds one experiment with three recorded runs (two comparable — same
/// protocol; one with a DIFFERENT protocol) and returns the store path.
QString seedStore( const QTemporaryDir &dir, QString *experimentIdOut )
{
    const QString path = dir.filePath( "experiment.db" );
    sicnu::experiment::ExperimentStore store;
    QString error;
    REQUIRE( store.open( path, &error ) );

    Experiment experiment;
    const QString experimentId = QStringLiteral( "exp-compare-r29" );
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "comparison probe" ) );
    REQUIRE( store.upsertExperiment( experiment ) );

    int counter = 0;
    auto addRun = [&]( const QString &runId, const QString &datasetVersion,
                       const QString &splitManifest, double iou, const QJsonObject &metrics ) {
        ExperimentRun run;
        run.setRunId( runId );
        run.setExperimentId( experimentId );
        run.setAlgorithmId( QStringLiteral( "rs:threshold_raster" ) );
        run.setDatasetVersionId( datasetVersion );
        run.setCreatedAtUtc( QDateTime::fromString(
          QStringLiteral( "2026-11-08T10:0%1:00Z" ).arg( counter ), Qt::ISODate ) );
        ++counter;
        const auto seeded = store.upsertRunWithMetric(
          run, makeMetric( runId, datasetVersion, splitManifest, iou, metrics ) );
        if ( !seeded )
            FAIL( QStringLiteral( "%1: %2" )
                      .arg( seeded.diagnostics().first().code,
                            seeded.diagnostics().first().message )
                      .toStdString() );
        REQUIRE( seeded );
    };

    const QJsonObject metricsA = QJsonObject{
        { QStringLiteral( "accuracy" ), 0.8 },
        { QStringLiteral( "only_a" ), 1.0 },
    };
    const QJsonObject metricsB = QJsonObject{
        { QStringLiteral( "accuracy" ), 0.9 },
        { QStringLiteral( "only_b" ), 2.0 },
    };
    // A/B share the protocol (same dataset version, split, threshold); C
    // differs on dataset version + split + threshold.
    addRun( QStringLiteral( "run-cmp-a" ), QStringLiteral( "dv-1" ), QStringLiteral( "sm-1" ),
            0.5, metricsA );
    addRun( QStringLiteral( "run-cmp-b" ), QStringLiteral( "dv-1" ), QStringLiteral( "sm-1" ),
            0.5, metricsB );
    addRun( QStringLiteral( "run-cmp-c" ), QStringLiteral( "dv-2" ), QStringLiteral( "sm-2" ),
            0.3, metricsA );

    *experimentIdOut = experimentId;
    return path;
}

QPlainTextEdit *runDetailOf( DatasetExperimentPanel *panel )
{
    const auto edits = panel->findChildren<QPlainTextEdit *>();
    for ( QPlainTextEdit *edit : edits )
        if ( edit->placeholderText().contains( QStringLiteral( "Run metrics" ) ) )
            return edit;
    return nullptr;
}

} // namespace

TEST_CASE( "D3: comparison verdict comes from the protocol authority",
           "[r29_t04][compare][protocol]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    QString experimentId;
    const QString storePath = seedStore( dir, &experimentId );

    DatasetExperimentPanel panel;
    REQUIRE( panel.openExperimentStoreAtPath( storePath ) );
    auto *detail = runDetailOf( &panel );
    REQUIRE( detail != nullptr );

    // Select the two SAME-protocol runs and compare.
    QTableWidget *runsTable = panel.findChild<QTableWidget *>();
    REQUIRE( runsTable != nullptr );
    REQUIRE( runsTable->rowCount() >= 3 );
    int rowA = -1, rowB = -1, rowC = -1;
    for ( int row = 0; row < runsTable->rowCount(); ++row )
    {
        const QString id = runsTable->item( row, 0 )->text();
        if ( id == QStringLiteral( "run-cmp-a" ) )
            rowA = row;
        else if ( id == QStringLiteral( "run-cmp-b" ) )
            rowB = row;
        else if ( id == QStringLiteral( "run-cmp-c" ) )
            rowC = row;
    }
    REQUIRE( ( rowA >= 0 && rowB >= 0 && rowC >= 0 ) );

    runsTable->selectRow( rowA );
    runsTable->selectionModel()->select(
      runsTable->model()->index( rowB, 0 ),
      QItemSelectionModel::Select | QItemSelectionModel::Rows );
    REQUIRE( runsTable->selectionModel()->selectedRows().size() == 2 );
    for ( QPushButton *button : panel.findChildren<QPushButton *>() )
        if ( button->text().contains( QStringLiteral( "Compare" ), Qt::CaseInsensitive ) )
            button->click();

    const QString report = detail->toPlainText();
    INFO( report.toStdString() );
    CHECK( report.contains( QStringLiteral( "protocol_compatibility" ) ) );
    CHECK( report.contains( QStringLiteral( "\"comparable\": true" ) ) );
    // Paired deltas with their denominators, from the authority.
    CHECK( report.contains( QStringLiteral( "paired_summary" ) ) );
    CHECK( report.contains( QStringLiteral( "accuracy" ) ) );
    // One-sided metrics stay visible, never differenced.
    CHECK( report.contains( QStringLiteral( "only_a" ) ) );
    CHECK( report.contains( QStringLiteral( "only_b" ) ) );

    // Different protocol (different dataset version + iou threshold): the
    // authority explains WHY, deltas are not comparable evidence.
    runsTable->clearSelection();
    runsTable->selectRow( rowA );
    runsTable->selectionModel()->select(
      runsTable->model()->index( rowC, 0 ),
      QItemSelectionModel::Select | QItemSelectionModel::Rows );
    for ( QPushButton *button : panel.findChildren<QPushButton *>() )
        if ( button->text().contains( QStringLiteral( "Compare" ), Qt::CaseInsensitive ) )
            button->click();
    const QString cross = detail->toPlainText();
    INFO( cross.toStdString() );
    CHECK( cross.contains( QStringLiteral( "\"comparable\": false" ) ) );
    CHECK( cross.contains( QStringLiteral( "dataset_version" ) ) );
    CHECK( cross.contains( QStringLiteral( "differences" ) ) );
}

TEST_CASE( "D3: run selection survives a table rebuild by run id",
           "[r29_t04][compare][selection]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    QString experimentId;
    const QString storePath = seedStore( dir, &experimentId );

    DatasetExperimentPanel panel;
    REQUIRE( panel.openExperimentStoreAtPath( storePath ) );
    QTableWidget *runsTable = panel.findChild<QTableWidget *>();
    REQUIRE( runsTable != nullptr );
    REQUIRE( runsTable->rowCount() >= 3 );

    int rowB = -1;
    for ( int row = 0; row < runsTable->rowCount(); ++row )
        if ( runsTable->item( row, 0 )->text() == QStringLiteral( "run-cmp-b" ) )
            rowB = row;
    REQUIRE( rowB >= 0 );
    runsTable->selectRow( rowB );

    // A re-open of the same store rebuilds the table (the pager/refresh
    // path): the remembered id must re-select its row.
    REQUIRE( panel.openExperimentStoreAtPath( storePath ) );
    int rowBAfter = -1;
    for ( int row = 0; row < runsTable->rowCount(); ++row )
        if ( runsTable->item( row, 0 )->text() == QStringLiteral( "run-cmp-b" ) )
            rowBAfter = row;
    REQUIRE( rowBAfter >= 0 );
    CHECK( runsTable->selectionModel()->selectedRows().size() == 1 );
    CHECK( runsTable->selectionModel()->isRowSelected( rowBAfter, QModelIndex() ) );

    // A selection whose record vanished resolves to nothing — never a
    // re-aim at a neighbouring row: closing onto a store without that run.
    QTemporaryDir other;
    sicnu::experiment::ExperimentStore empty;
    REQUIRE( empty.open( other.filePath( "empty.db" ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-empty" ) );
    experiment.setName( QStringLiteral( "empty" ) );
    REQUIRE( empty.upsertExperiment( experiment ) );
    REQUIRE( panel.openExperimentStoreAtPath( other.filePath( "empty.db" ) ) );
    CHECK( runsTable->selectionModel()->selectedRows().isEmpty() );
}
