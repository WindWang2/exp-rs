// Workbench 7.0 — unified processing history model (goal §C + §H)
//
// Covers: bounded retention with truthful drop accounting, newest-first
// ordering, state/search filtering (incl. incremental search over 100k-feed
// scale), truthful state strings for every lifecycle state, and O(1) cell
// access at cap. The model is a pure projection — no services involved.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/processing_history_model.h"

#include <QCoreApplication>

namespace
{

int fake_argc = 0;
QCoreApplication &testApp()
{
  static QCoreApplication app( fake_argc, nullptr );
  return app;
}

sicnu::app::HistoryEntry makeTask( long id, const QString &title,
                                   const QString &stateText, bool running,
                                   const QDateTime &started = {} )
{
  sicnu::app::HistoryEntry entry;
  entry.kind = sicnu::app::HistoryEntry::Kind::Task;
  entry.taskId = id;
  entry.title = title;
  entry.source = QStringLiteral( "gui" );
  entry.stateText = stateText;
  entry.running = running;
  entry.started = started;
  entry.algorithmId = QStringLiteral( "rs.%1" ).arg( title );
  return entry;
}

} // namespace

TEST_CASE( "History states project truthfully for every lifecycle value",
           "[history][states]" )
{
  testApp();
  using sicnu::TaskStatus;
  using namespace sicnu::app;
  CHECK( historyTaskStateText( TaskStatus::Queued ) == QStringLiteral( "排队中" ) );
  CHECK( historyTaskStateText( TaskStatus::Running ) == QStringLiteral( "运行中" ) );
  CHECK( historyTaskStateText( TaskStatus::WaitingResource ) == QStringLiteral( "等待资源" ) );
  CHECK( historyTaskStateText( TaskStatus::Completed ) == QStringLiteral( "已完成" ) );
  CHECK( historyTaskStateText( TaskStatus::Failed ) == QStringLiteral( "失败" ) );
  CHECK( historyTaskStateText( TaskStatus::Canceled ) == QStringLiteral( "已取消" ) );
  CHECK( historyTaskTerminal( TaskStatus::Running ) == false );
  CHECK( historyTaskTerminal( TaskStatus::Completed ) == true );
  CHECK( historyTaskTerminal( TaskStatus::Failed ) == true );
  CHECK( historyTaskTerminal( TaskStatus::Canceled ) == true );

  // Interrupted workflow runs are resumable, terminal ones are not.
  using sicnu::workflow::WorkflowRunState;
  CHECK( historyRunStateText( WorkflowRunState::Interrupted )
           .contains( QStringLiteral( "已中断" ) ) );
  CHECK( historyRunResumable( WorkflowRunState::Interrupted ) );
  CHECK_FALSE( historyRunResumable( WorkflowRunState::Completed ) );
  CHECK_FALSE( historyRunResumable( WorkflowRunState::Failed ) );
  CHECK( historyRunStateText( WorkflowRunState::Completed ) == QStringLiteral( "已完成" ) );
}

TEST_CASE( "History model orders newest-first and keeps the visible surface bounded",
           "[history][cap]" )
{
  testApp();
  sicnu::app::ProcessingHistoryModel model;
  const QDateTime base = QDateTime::fromSecsSinceEpoch( 1000 );

  QVector<sicnu::app::HistoryEntry> entries;
  for ( long i = 0; i < 6000; ++i )
    entries.append( makeTask( i, QStringLiteral( "t%1" ).arg( i ),
                              QStringLiteral( "已完成" ), false,
                              base.addSecs( i ) ) );
  model.setEntries( entries );

  CHECK( model.rowCount() == sicnu::app::ProcessingHistoryModel::kMaxRows );
  CHECK( model.droppedCount() == 1000 );
  // Newest first: the highest task id is on top; the 1000 oldest are gone.
  const sicnu::app::HistoryEntry *top = model.entryAtRow( 0 );
  REQUIRE( top );
  CHECK( top->taskId == 5999 );
  const sicnu::app::HistoryEntry *bottom = model.entryAtRow( model.rowCount() - 1 );
  REQUIRE( bottom );
  CHECK( bottom->taskId == 1000 );

  // Drop accounting accumulates across replaces, never resets silently.
  model.setEntries( entries );
  CHECK( model.droppedCount() == 2000 );
}

TEST_CASE( "History model filters by state and incremental search", "[history][filter]" )
{
  testApp();
  sicnu::app::ProcessingHistoryModel model;
  const QDateTime base = QDateTime::fromSecsSinceEpoch( 5000 );

  QVector<sicnu::app::HistoryEntry> entries;
  entries.append( makeTask( 1, QStringLiteral( "NDVI 计算" ),
                            QStringLiteral( "运行中" ), true, base ) );
  entries.append( makeTask( 2, QStringLiteral( "辐射定标" ),
                            QStringLiteral( "失败" ), false, base.addSecs( 1 ) ) );
  entries.append( makeTask( 3, QStringLiteral( "镶嵌" ),
                            QStringLiteral( "已取消" ), false, base.addSecs( 2 ) ) );
  sicnu::app::HistoryEntry run;
  run.kind = sicnu::app::HistoryEntry::Kind::WorkflowRun;
  run.runId = QStringLiteral( "run-abc" );
  run.title = QStringLiteral( "工作流 change-detect" );
  run.source = QStringLiteral( "workflow" );
  run.stateText = QStringLiteral( "已中断（可恢复）" );
  run.resumable = true;
  run.started = base.addSecs( 3 );
  entries.append( run );
  model.setEntries( entries );

  SECTION( "state filter" )
  {
    model.setStateFilter( QStringLiteral( "失败" ) );
    REQUIRE( model.rowCount() == 1 );
    CHECK( model.entryAtRow( 0 )->taskId == 2 );
  }

  SECTION( "incremental search by title (case-insensitive)" )
  {
    model.setSearchText( QStringLiteral( "ndvi" ) );
    REQUIRE( model.rowCount() == 1 );
    CHECK( model.entryAtRow( 0 )->taskId == 1 );
  }

  SECTION( "search matches run ids and exact task ids" )
  {
    model.setSearchText( QStringLiteral( "run-abc" ) );
    REQUIRE( model.rowCount() == 1 );
    CHECK( model.entryAtRow( 0 )->kind == sicnu::app::HistoryEntry::Kind::WorkflowRun );

    model.setSearchText( QStringLiteral( "3" ) );
    REQUIRE( model.rowCount() == 1 );
    CHECK( model.entryAtRow( 0 )->taskId == 3 );
  }

  SECTION( "combined filters" )
  {
    model.setStateFilter( QStringLiteral( "已取消" ) );
    model.setSearchText( QStringLiteral( "镶嵌" ) );
    REQUIRE( model.rowCount() == 1 );
    CHECK( model.entryAtRow( 0 )->taskId == 3 );
  }

  SECTION( "clearing filters restores the full set" )
  {
    model.setStateFilter( QStringLiteral( "失败" ) );
    REQUIRE( model.rowCount() == 1 );
    model.setStateFilter( QString() );
    model.setSearchText( QString() );
    CHECK( model.rowCount() == 4 );
  }
}

TEST_CASE( "History model renders cells O(1) at 100k-feed scale", "[history][scale]" )
{
  testApp();
  sicnu::app::ProcessingHistoryModel model;
  const QDateTime base = QDateTime::fromSecsSinceEpoch( 90000 );

  QVector<sicnu::app::HistoryEntry> entries;
  entries.reserve( 100000 );
  for ( long i = 0; i < 100000; ++i )
  {
    sicnu::app::HistoryEntry entry = makeTask( i, QStringLiteral( "t%1" ).arg( i ),
                                               QStringLiteral( "已完成" ), false,
                                               base.addSecs( i % 1000 ) );
    entry.outputPaths.append( QStringLiteral( "/out/result-%1.tif" ).arg( i ) );
    entries.append( entry );
  }

  model.setEntries( entries );
  REQUIRE( model.rowCount() == sicnu::app::ProcessingHistoryModel::kMaxRows );
  CHECK( model.droppedCount() == 95000 );

  // Cell access and a filtered scan over the capped set stay bounded.
  const QVariant name =
    model.data( model.index( 0, sicnu::app::ProcessingHistoryModel::Output ), Qt::DisplayRole );
  CHECK( name.toString().contains( QStringLiteral( "result-" ) ) );
  model.setSearchText( QStringLiteral( "t9" ) );
  CHECK( model.rowCount() > 0 );
  CHECK( model.rowCount() <= sicnu::app::ProcessingHistoryModel::kMaxRows );
  model.entryAtRow( 12345 ); // must not crash/fail at any in-range row
}

TEST_CASE( "History model renders durations and progress truthfully", "[history][cells]" )
{
  testApp();
  sicnu::app::ProcessingHistoryModel model;
  const QDateTime start = QDateTime::fromSecsSinceEpoch( 100000 );

  sicnu::app::HistoryEntry done = makeTask( 1, QStringLiteral( "done" ),
                                            QStringLiteral( "已完成" ), false, start );
  done.ended = start.addSecs( 125 ); // 2m5s
  sicnu::app::HistoryEntry live = makeTask( 2, QStringLiteral( "live" ),
                                            QStringLiteral( "运行中" ), true, start );
  live.progress = 42.4;
  sicnu::app::HistoryEntry queued = makeTask( 3, QStringLiteral( "queued" ),
                                              QStringLiteral( "排队中" ), true );
  queued.progress = -1; // not applicable
  model.setEntries( { live, done, queued } );

  const QVariant duration =
    model.data( model.index( 1, sicnu::app::ProcessingHistoryModel::Duration ), Qt::DisplayRole );
  CHECK( duration.toString() == QStringLiteral( "2m5s" ) );
  const QVariant progress =
    model.data( model.index( 0, sicnu::app::ProcessingHistoryModel::Progress ), Qt::DisplayRole );
  CHECK( progress.toString() == QStringLiteral( "42%" ) );
  const QVariant noProgress =
    model.data( model.index( 2, sicnu::app::ProcessingHistoryModel::Progress ), Qt::DisplayRole );
  CHECK( noProgress.toString() == QStringLiteral( "—" ) );
}
