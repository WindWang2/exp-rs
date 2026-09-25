// tests/test_mission_timeline_surface.cpp — Mission Runtime 13.0 lifecycle
// gate for the desktop surface: the mission task selection the panel pushes
// into SelectionContext must mirror the authority, never the last click.
//
// Two failure shapes drove this gate (both fail-open before the panel
// re-derived its selection on model changes):
//
//   1. Project boundary: resetMissionSessionState() resets the panel through
//      setTimeline(empty). A model reset clears the table selection WITHOUT a
//      QItemSelectionModel::selectionChanged, so the push never happened and
//      project A's selected task id/status survived into project B's session
//      — mission.task.retry availability then derived from a dead mission.
//   2. Same-project drift: the agent tools mutate the authority out of
//      process; the sidecar watcher refresh applies the events through
//      applyEvents (row dataChanged, again no selectionChanged). The snapshot
//      kept the stale status (e.g. "failed" while the task is running).
//
// The harness mirrors the shell wiring exactly: panel taskSelected ->
// SelectionContext::notifyMissionTaskSelection. Headless (QT_QPA_PLATFORM is
// set by the test runner; widgets need a QApplication instance).
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_timeline_panel.h"
#include "app/workbench/selection_context.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QTableView>

using namespace sicnu::app;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_mission_timeline_surface";
char *appArgv[] = { appArgv0, nullptr };

QApplication &ensureApp()
{
    if ( !QApplication::instance() )
        new QApplication( appArgc(), appArgv );
    return *qobject_cast<QApplication *>( QApplication::instance() );
}

MissionTask makeTask( const QString &id, MissionStage stage )
{
    MissionTask task;
    task.id = id;
    task.stage = stage;
    task.title = QStringLiteral( "task " ) + id;
    task.capabilityId = QStringLiteral( "rs:spectral_index" );
    return task;
}

QTableView *tableView( MissionTimelinePanel &panel )
{
    return panel.findChild<QTableView *>();
}

void selectRow( QTableView *view, int row )
{
    view->selectionModel()->select(
        view->model()->index( row, 0 ),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
}

/// Two-task mission: t1 pending with a bound run (so it may go Running), t2
/// plain pending.
MissionTimeline makeTimeline()
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "m-surface" ) );
    timeline.setProjectRef( QStringLiteral( "/tmp/surface.qgz" ) );

    MissionTask t1 = makeTask( QStringLiteral( "t1" ), MissionStage::Analyze );
    t1.run.kind = QStringLiteral( "task_center" );
    t1.run.id = QStringLiteral( "tc-1" );
    REQUIRE( timeline.addTask( t1 ).applied );
    REQUIRE( timeline.addTask( makeTask( QStringLiteral( "t2" ), MissionStage::Import ) ).applied );
    return timeline;
}

} // namespace

TEST_CASE( "pushed selection clears when the panel reloads a mission",
           "[mission][surface][lifecycle]" )
{
    ensureApp();
    MissionTimelinePanel panel;
    SelectionContext context;
    QObject::connect( &panel, &MissionTimelinePanel::taskSelected, &context,
                      &SelectionContext::notifyMissionTaskSelection );

    panel.setTimeline( makeTimeline() );
    QTableView *view = tableView( panel );
    REQUIRE( view != nullptr );
    selectRow( view, 0 );

    SelectionContextSnapshot selected = context.snapshot();
    REQUIRE( selected.hasMissionTaskSelection );
    REQUIRE( selected.selectedMissionTaskId == QStringLiteral( "t1" ) );

    // The project boundary (New/Open Project reset) reloads an empty
    // timeline through this exact call. The pushed selection must follow —
    // project B's commands must not derive availability from project A's
    // task.
    panel.setTimeline( MissionTimeline{} );

    const SelectionContextSnapshot afterReset = context.snapshot();
    REQUIRE_FALSE( afterReset.hasMissionTaskSelection );
    REQUIRE( afterReset.selectedMissionTaskId.isEmpty() );
    REQUIRE_FALSE( ContextRules::missionTaskSelected( afterReset ) );
    REQUIRE_FALSE( ContextRules::missionTaskRetryable( afterReset ) );
}

TEST_CASE( "pushed selection status follows an incremental authority update",
           "[mission][surface][lifecycle]" )
{
    ensureApp();
    MissionTimeline timeline = makeTimeline();

    MissionTimelinePanel panel;
    SelectionContext context;
    QObject::connect( &panel, &MissionTimelinePanel::taskSelected, &context,
                      &SelectionContext::notifyMissionTaskSelection );
    panel.setTimeline( timeline );

    QTableView *view = tableView( panel );
    REQUIRE( view != nullptr );
    selectRow( view, 0 );
    REQUIRE( context.snapshot().selectedMissionTaskStatus == MissionTaskStatus::Pending );

    // The agent surface advanced the task out of process; the GUI refreshes
    // through applyEvents (the incremental path, no reset).
    const quint64 cursor = timeline.lastEventSeq();
    REQUIRE( timeline.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                                  QStringLiteral( "2026-09-25T00:00:00Z" ) ).applied );
    panel.applyEvents( timeline, cursor );

    const SelectionContextSnapshot afterUpdate = context.snapshot();
    REQUIRE( afterUpdate.hasMissionTaskSelection );
    REQUIRE( afterUpdate.selectedMissionTaskId == QStringLiteral( "t1" ) );
    REQUIRE( afterUpdate.selectedMissionTaskStatus == MissionTaskStatus::Running );

    // Availability rules must project the CURRENT status, not the click-time
    // one: a running task is neither retryable nor resumable.
    REQUIRE_FALSE( ContextRules::missionTaskRetryable( afterUpdate ) );
    REQUIRE_FALSE( ContextRules::missionTaskResumable( afterUpdate ) );
}

TEST_CASE( "selection stays bound to the task id across appended rows",
           "[mission][surface][selection]" )
{
    ensureApp();
    MissionTimeline timeline = makeTimeline();

    MissionTimelinePanel panel;
    SelectionContext context;
    QObject::connect( &panel, &MissionTimelinePanel::taskSelected, &context,
                      &SelectionContext::notifyMissionTaskSelection );
    panel.setTimeline( timeline );

    QTableView *view = tableView( panel );
    REQUIRE( view != nullptr );
    selectRow( view, 1 ); // t2
    REQUIRE( context.snapshot().selectedMissionTaskId == QStringLiteral( "t2" ) );

    // A task lands under the selection (append): rows never move, so the
    // selection still addresses t2 — and the push reports t2, not row 1 of
    // some re-indexed table.
    MissionTask t3 = makeTask( QStringLiteral( "t3" ), MissionStage::Verify );
    const quint64 cursor = timeline.lastEventSeq();
    REQUIRE( timeline.addTask( t3 ).applied );
    panel.applyEvents( timeline, cursor );

    const SelectionContextSnapshot afterAppend = context.snapshot();
    REQUIRE( afterAppend.hasMissionTaskSelection );
    REQUIRE( afterAppend.selectedMissionTaskId == QStringLiteral( "t2" ) );
    REQUIRE( panel.model()->rowOfTask( QStringLiteral( "t2" ) ) == 1 );
    REQUIRE( panel.model()->rowOfTask( QStringLiteral( "t3" ) ) == 2 );
}
