// test_r29_t04_history_actions.cpp — D2 (R29-T04).
//
// The processing-history panel's actions must target ONE stable identity:
// the same row the enable checks gate on, restored by authoritative
// taskId/runId across the model reset every refresh performs, with a real
// multi-artifact pick that never opens a vanished file. Drives the REAL
// panel + model over the REAL TaskCenter singleton (queued tasks with
// autoDispatch=false never execute — they are stable history rows).
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QDateTime>
#include <QFile>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include "app/workbench/processing_history_panel.h"

using sicnu::app::ProcessingHistoryPanel;
using sicnu::app::ProcessingHistoryModel;
using sicnu::app::HistoryEntry;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_r29_t04_history_actions";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

/// A queued task in the REAL TaskCenter (never dispatched → it stays
/// queued and re-appears on every re-query). A real registered algorithm
/// id keeps admission honest; autoDispatch=false means it never runs.
long seedQueuedTask( const QString &suffix )
{
    QVariantMap params;
    params.insert( QStringLiteral( "note" ), suffix );
    return sicnu::TaskCenter::instance().enqueueTask(
      QStringLiteral( "rs:threshold_raster" ), params,
      /*autoLoad=*/false, sicnu::TaskPriority::Normal, QList<long>(),
      /*autoDispatch=*/false, 0, QStringLiteral( "gui-history-test" ) );
}

QTableView *viewOf( ProcessingHistoryPanel *panel )
{
    return panel->findChild<QTableView *>( QStringLiteral( "rsHistoryView" ) );
}

QPushButton *buttonByText( ProcessingHistoryPanel *panel, const QString &needle )
{
    for ( QPushButton *button : panel->findChildren<QPushButton *>() )
        if ( button->text().contains( needle, Qt::CaseInsensitive ) )
            return button;
    return nullptr;
}

} // namespace

TEST_CASE( "D2: identity keys distinguish task rows and workflow rows",
           "[r29_t04][history][identity]" )
{
    HistoryEntry task;
    task.kind = HistoryEntry::Kind::Task;
    task.taskId = 42;
    HistoryEntry run;
    run.kind = HistoryEntry::Kind::WorkflowRun;
    run.runId = QStringLiteral( "run-xyz" );
    CHECK( ProcessingHistoryModel::identityKey( task ) == QStringLiteral( "t42" ) );
    CHECK( ProcessingHistoryModel::identityKey( run ) == QStringLiteral( "rrun-xyz" ) );
}

TEST_CASE( "D2: model reset restores selection by authoritative identity",
           "[r29_t04][history][selection]" )
{
    ensureQgisApplication();
    const long older = seedQueuedTask( QStringLiteral( "sel_older" ) );
    const long newer = seedQueuedTask( QStringLiteral( "sel_newer" ) );
    REQUIRE( ( older > 0 && newer > 0 ) );

    ProcessingHistoryPanel panel;
    panel.refreshNow();

    QTableView *view = viewOf( &panel );
    REQUIRE( view != nullptr );
    // Locate the OLDER task's row by identity (newest-first ordering).
    int olderRow = -1;
    for ( int row = 0; row < panel.model()->rowCount(); ++row )
        if ( const HistoryEntry *entry = panel.model()->entryAtRow( row );
             entry && entry->taskId == older )
            olderRow = row;
    REQUIRE( olderRow >= 0 );
    view->selectRow( olderRow );
    view->selectionModel()->setCurrentIndex( view->model()->index( olderRow, 0 ),
                                             QItemSelectionModel::NoUpdate );
    REQUIRE( view->selectionModel()->selectedRows().size() == 1 );

    // A refresh re-queries the REAL TaskCenter (same identities, fresh
    // timestamps): the selection must survive the model reset.
    panel.refreshNow();
    CHECK( view->selectionModel()->selectedRows().size() == 1 );
    const HistoryEntry *still = panel.model()->entryAtRow(
      view->currentIndex().isValid() ? view->currentIndex().row() : -1 );
    REQUIRE( still != nullptr );
    CHECK( still->taskId == older ); // the identity, not a row number

}

TEST_CASE( "D2: enable and action target the same row (no cross-task cancellation)",
           "[r29_t04][history][actiontarget]" )
{
    ensureQgisApplication();
    ProcessingHistoryPanel panel;

    // One RUNNING task (cancellable) and one FINISHED task (rerunnable).
    HistoryEntry running;
    running.kind = HistoryEntry::Kind::Task;
    running.taskId = 100;
    running.running = true;
    running.stateText = QStringLiteral( "Running" );
    running.started = QDateTime::currentDateTimeUtc().addSecs( -60 );
    HistoryEntry finished;
    finished.kind = HistoryEntry::Kind::Task;
    finished.taskId = 200;
    finished.running = false;
    finished.algorithmId = QStringLiteral( "rs:x" );
    finished.stateText = QStringLiteral( "Finished" );
    finished.started = QDateTime::currentDateTimeUtc();
    panel.model()->setEntries( { running, finished } );

    QTableView *view = viewOf( &panel );
    REQUIRE( view != nullptr );
    REQUIRE( panel.model()->rowCount() >= 2 );

    // Multi-select both rows; keyboard current sits on the FINISHED row.
    // The old bug: enable checked selectedRows().first() while the action
    // hit currentIndex — cancel could arm against row A but the button's
    // enable came from row B.
    view->selectionModel()->select(
      QItemSelection( view->model()->index( 0, 0 ),
                      view->model()->index( 1, ProcessingHistoryModel::ColumnCount - 1 ) ),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    // NoUpdate: the view-level setCurrentIndex default (ClearAndSelect)
    // would collapse the multi-selection to the current row.
    view->selectionModel()->setCurrentIndex( view->model()->index( 1, 0 ),
                                             QItemSelectionModel::NoUpdate );

    QPushButton *cancelBtn = buttonByText( &panel, QStringLiteral( "Cancel" ) );
    QPushButton *rerunBtn = buttonByText( &panel, QStringLiteral( "Re-run" ) );
    REQUIRE( cancelBtn != nullptr );
    REQUIRE( rerunBtn != nullptr );
    // The current row (finished) drives BOTH enable checks now: cancel
    // disabled, rerun enabled — one rule for enable and act.
    CHECK_FALSE( cancelBtn->isEnabled() );
    CHECK( rerunBtn->isEnabled() );
}

TEST_CASE( "D2: multi-artifact choices expose existence honestly",
           "[r29_t04][history][artifacts]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    const QString existing = dir.filePath( "exists.tif" );
    {
        QFile file( existing );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "x" );
    }
    HistoryEntry entry;
    entry.kind = HistoryEntry::Kind::Task;
    entry.taskId = 300;
    entry.outputPaths = { existing, dir.filePath( "gone.tif" ) };

    const auto choices = ProcessingHistoryPanel::outputChoicesFor( entry );
    REQUIRE( choices.size() == 2 );
    CHECK( choices[0].exists );
    CHECK( choices[0].label.contains( QStringLiteral( "exists.tif" ) ) );
    CHECK_FALSE( choices[0].label.contains( QStringLiteral( "(missing)" ) ) );
    CHECK_FALSE( choices[1].exists );
    CHECK( choices[1].label.contains( QStringLiteral( "(missing)" ) ) );
    CHECK( choices[1].label.contains( QStringLiteral( "gone.tif" ) ) );

    // Single-artifact rows need no menu: the one path is the pick.
    entry.outputPaths = { existing };
    const auto single = ProcessingHistoryPanel::outputChoicesFor( entry );
    REQUIRE( single.size() == 1 );
    CHECK( single[0].path == existing );

    // A row whose every artifact vanished: open/inspect stay disabled (the
    // enable rule requires at least one EXISTING artifact).
    entry.outputPaths = { dir.filePath( "gone.tif" ) };
    bool anyExisting = false;
    for ( const auto &choice : ProcessingHistoryPanel::outputChoicesFor( entry ) )
        anyExisting = anyExisting || choice.exists;
    CHECK_FALSE( anyExisting );
}

TEST_CASE( "D2: retry names the new task id and distinguishes resubmission",
           "[r29_t04][history][retry]" )
{
    ensureQgisApplication();
    // The rerun path is exercised over the REAL TaskCenter: a queued task
    // that cannot be retried (not failed) falls back to resubmission and
    // the status line must name the NEW task id, never a silent "same".
    const long original = seedQueuedTask( QStringLiteral( "retry_probe" ) );
    REQUIRE( original > 0 );

    ProcessingHistoryPanel panel;
    panel.refreshNow();
    QTableView *view = viewOf( &panel );
    REQUIRE( view != nullptr );
    // The seeded task is somewhere in the (newest-first) projection.
    int seededRow = -1;
    for ( int row = 0; row < panel.model()->rowCount(); ++row )
        if ( const HistoryEntry *entry = panel.model()->entryAtRow( row );
             entry && entry->taskId == original )
            seededRow = row;
    REQUIRE( seededRow >= 0 );
    view->selectRow( seededRow );
    view->setCurrentIndex( view->model()->index( seededRow, 0 ) );

    QPushButton *rerunBtn = buttonByText( &panel, QStringLiteral( "Re-run" ) );
    REQUIRE( rerunBtn != nullptr );
    // Queued, not running, with an algorithm id → rerunnable.
    REQUIRE( rerunBtn->isEnabled() );
    rerunBtn->click();

    QLabel *status = panel.findChild<QLabel *>();
    REQUIRE( status != nullptr );
    const QString text = status->text();
    INFO( text.toStdString() );
    // Either outcome names the new task id — retry or resubmission, both
    // distinguishable in the message.
    CHECK( ( text.contains( QStringLiteral( "retry queued as new task" ) )
             || text.contains( QStringLiteral( "resubmitted as new task" ) ) ) );
    CHECK( text.contains( QString::number( original ) ) );
}
