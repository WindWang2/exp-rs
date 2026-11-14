// test_r30_t06_history_action_parity.cpp — R30-T06 / F2 (WP04, WP05)
//
// The processing-history panel must present ONE honest action-availability
// truth. Baseline defects (all on dcda90bf, verified by reading the real
// source, not by issue titles):
//   * outputChoicesFor() only checks path existence; `supported` stays at its
//     default true, so vanished artifacts are never marked unsupported and no
//     type/source information is shown.
//   * the Compare button is enabled for ANY two selected rows — even when
//     neither row has a real artifact, or both hold non-raster products that
//     openComparisonForPaths() (main_window_workbench.cpp:142) cannot load.
//   * the context menu never reuses updateActionStates(), so keyboard/menu
//     entries can fire actions the buttons refuse.
//   * a failed re-submission is still reported as a queued new task.
//
// RED stage: assertions use ONLY the baseline public API, so they compile and
// fail cleanly on the baseline. Behaviour assertions needing the new
// test-injection seam are added after the production fix.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QAction>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>

#include "app/workbench/processing_history_panel.h"

using sicnu::app::HistoryEntry;
using sicnu::app::ProcessingHistoryModel;
using sicnu::app::ProcessingHistoryPanel;

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
  static int argc = 1;
  static char applicationName[] = "test_r30_t06_history_action_parity";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QString writeArtifact( QTemporaryDir &dir, const QString &name )
{
  const QString path = dir.filePath( name );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( "x" ) == 1 );
  file.close();
  return path;
}

HistoryEntry finishedTask( long id )
{
  HistoryEntry entry;
  entry.kind = HistoryEntry::Kind::Task;
  entry.taskId = id;
  entry.algorithmId = QStringLiteral( "rs:x" );
  entry.stateText = QStringLiteral( "Finished" );
  entry.started = QDateTime::fromSecsSinceEpoch( 1700000000 );
  return entry;
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

void selectBothRows( QTableView *view, ProcessingHistoryModel *model )
{
  REQUIRE( model->rowCount() >= 2 );
  view->selectionModel()->select(
    QItemSelection( model->index( 0, 0 ),
                    model->index( 1, ProcessingHistoryModel::ColumnCount - 1 ) ),
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
}

} // namespace

TEST_CASE( "R30-T06 F2: artifact choices expose existence, type and support honestly",
           "[r30_t06][history][f2][artifacts]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeArtifact( dir, QStringLiteral( "a.tif" ) );
  const QString missing = dir.filePath( QStringLiteral( "gone.tif" ) );

  HistoryEntry entry = finishedTask( 1 );
  entry.outputPaths = { raster, missing };
  const auto choices = ProcessingHistoryPanel::outputChoicesFor( entry );
  REQUIRE( choices.size() == 2 );

  CHECK( choices[0].exists );
  CHECK( choices[0].supported );
  CHECK( choices[0].label.contains( QStringLiteral( "a.tif" ) ) );
  CHECK_FALSE( choices[0].label.contains( QStringLiteral( "(missing)" ) ) );
  // Baseline: `supported` is never computed, so a vanished artifact is
  // indistinguishable from an openable one.
  CHECK_FALSE( choices[1].exists );
  CHECK_FALSE( choices[1].supported );
  CHECK( choices[1].label.contains( QStringLiteral( "(missing)" ) ) );
  // The pick menu must tell the user WHAT the artifact is, not just its name.
  CHECK( choices[0].label.contains( QStringLiteral( "raster" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "R30-T06 F2: compare is disabled and explained when a side has no real artifact",
           "[r30_t06][history][f2][compare]" )
{
  ensureQgisApplication();
  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { finishedTask( 1 ), finishedTask( 2 ) } );

  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );

  QPushButton *compare = buttonByText( &panel, QStringLiteral( "Comparison" ) );
  REQUIRE( compare != nullptr );
  // Baseline: rows.size()==2 is the ONLY gate, so two empty rows are
  // "comparable" and no reason is ever shown.
  CHECK_FALSE( compare->isEnabled() );
  CHECK_FALSE( compare->toolTip().isEmpty() );
}

TEST_CASE( "R30-T06 F2: non-raster / cross-type pairs are refused for comparison",
           "[r30_t06][history][f2][compare]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  HistoryEntry left = finishedTask( 3 );
  left.outputPaths = { writeArtifact( dir, QStringLiteral( "left.json" ) ) };
  HistoryEntry right = finishedTask( 4 );
  right.outputPaths = { writeArtifact( dir, QStringLiteral( "right.json" ) ) };

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { left, right } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );

  QPushButton *compare = buttonByText( &panel, QStringLiteral( "Comparison" ) );
  REQUIRE( compare != nullptr );
  CHECK_FALSE( compare->isEnabled() );
  CHECK_FALSE( compare->toolTip().isEmpty() );
}

TEST_CASE( "R30-T06 F2: two real raster rows remain comparable (no over-blocking)",
           "[r30_t06][history][f2][compare]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  HistoryEntry left = finishedTask( 5 );
  left.outputPaths = { writeArtifact( dir, QStringLiteral( "l1.tif" ) ),
                       writeArtifact( dir, QStringLiteral( "l2.tif" ) ) };
  HistoryEntry right = finishedTask( 6 );
  right.outputPaths = { writeArtifact( dir, QStringLiteral( "r1.tif" ) ),
                        writeArtifact( dir, QStringLiteral( "r2.tif" ) ) };

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { left, right } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );

  QPushButton *compare = buttonByText( &panel, QStringLiteral( "Comparison" ) );
  REQUIRE( compare != nullptr );
  CHECK( compare->isEnabled() );
}

TEST_CASE( "R30-T06 F2: R29 identity gating is preserved (no regression)",
           "[r30_t06][history][f2][identity]" )
{
  ensureQgisApplication();
  HistoryEntry running = finishedTask( 10 );
  running.running = true;
  running.stateText = QStringLiteral( "Running" );
  HistoryEntry finished = finishedTask( 20 );
  // Keep the current target distinct from the first selected (newest) row.
  running.started = finished.started.addSecs( 60 );

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { running, finished } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );
  const int finishedRow = panel.model()->rowForIdentity( ProcessingHistoryModel::identityKey( finished ) );
  REQUIRE( finishedRow >= 0 );
  view->selectionModel()->setCurrentIndex( panel.model()->index( finishedRow, 0 ),
                                           QItemSelectionModel::NoUpdate );
  REQUIRE( view->selectionModel()->selectedRows().size() == 2 );
  REQUIRE( view->currentIndex().row() != view->selectionModel()->selectedRows().first().row() );

  QPushButton *cancel = buttonByText( &panel, QStringLiteral( "Cancel" ) );
  QPushButton *rerun = buttonByText( &panel, QStringLiteral( "Re-run" ) );
  REQUIRE( cancel != nullptr );
  REQUIRE( rerun != nullptr );
  CHECK_FALSE( cancel->isEnabled() );
  CHECK( rerun->isEnabled() );
}

TEST_CASE( "R30-T06 F2: context menu and buttons share ONE action plan with reasons",
           "[r30_t06][history][f2][parity]" )
{
  ensureQgisApplication();
  HistoryEntry running = finishedTask( 30 );
  running.running = true;
  running.stateText = QStringLiteral( "Running" );
  HistoryEntry finished = finishedTask( 40 );
  running.started = finished.started.addSecs( 60 );

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { running, finished } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );
  const int finishedRow = panel.model()->rowForIdentity( ProcessingHistoryModel::identityKey( finished ) );
  REQUIRE( finishedRow >= 0 );
  view->selectionModel()->setCurrentIndex( panel.model()->index( finishedRow, 0 ),
                                           QItemSelectionModel::NoUpdate );
  REQUIRE( view->selectionModel()->selectedRows().size() == 2 );
  REQUIRE( view->currentIndex().row() != view->selectionModel()->selectedRows().first().row() );

  const ProcessingHistoryPanel::ActionPlan plan = panel.actionPlan();
  QMenu menu;
  panel.buildContextMenuActions( &menu );
  const auto menuAction = [&menu]( const QString &needle ) -> QAction * {
    for ( QAction *action : menu.actions() )
      if ( !action->isSeparator() && action->text() == needle )
        return action;
    return nullptr;
  };
  QAction *menuCancel = menuAction( QStringLiteral( "Cancel" ) );
  QAction *menuRerun = menuAction( QStringLiteral( "Re-run" ) );
  REQUIRE( menuCancel != nullptr );
  REQUIRE( menuRerun != nullptr );
  QPushButton *btnCancel = buttonByText( &panel, QStringLiteral( "Cancel" ) );
  QPushButton *btnRerun = buttonByText( &panel, QStringLiteral( "Re-run" ) );
  REQUIRE( btnCancel != nullptr );
  REQUIRE( btnRerun != nullptr );

  CHECK( menuCancel->isEnabled() == btnCancel->isEnabled() );
  CHECK( menuRerun->isEnabled() == btnRerun->isEnabled() );
  CHECK( menuCancel->isEnabled() == plan.cancel );
  CHECK( menuRerun->isEnabled() == plan.rerun );
  CHECK_FALSE( menuCancel->isEnabled() ); // terminal row: no cancellation
  CHECK( menuRerun->isEnabled() );
  CHECK_FALSE( menuCancel->toolTip().isEmpty() ); // rejection reason is visible
  CHECK( plan.cancelReason.contains( QStringLiteral( "terminal" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "R30-T06 F2: comparison carries the explicitly chosen NON-FIRST artifact of BOTH rows",
           "[r30_t06][history][f2][compare][behaviour]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  HistoryEntry left = finishedTask( 50 );
  left.outputPaths = { writeArtifact( dir, QStringLiteral( "l0.tif" ) ),
                       writeArtifact( dir, QStringLiteral( "l1.tif" ) ) };
  HistoryEntry right = finishedTask( 60 );
  right.outputPaths = { writeArtifact( dir, QStringLiteral( "r0.tif" ) ),
                        writeArtifact( dir, QStringLiteral( "r1.tif" ) ) };
  // The model sorts newest first; give the signal's left/right order an
  // explicit fixture contract instead of relying on insertion or wall time.
  left.started = right.started.addSecs( 60 );

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { left, right } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );
  const QModelIndexList selected = view->selectionModel()->selectedRows();
  REQUIRE( selected.size() == 2 );
  REQUIRE( panel.model()->rowForIdentity( ProcessingHistoryModel::identityKey( left ) ) == selected.first().row() );
  REQUIRE( panel.model()->rowForIdentity( ProcessingHistoryModel::identityKey( right ) ) == selected.last().row() );
  QPushButton *compare = buttonByText( &panel, QStringLiteral( "Comparison" ) );
  REQUIRE( compare != nullptr );
  REQUIRE( compare->isEnabled() );

  int picks = 0;
  panel.setArtifactPickFn(
    [&picks]( const QVector<ProcessingHistoryPanel::OutputArtifactChoice> &choices ) {
      ++picks;
      return choices.size() > 1 ? choices.at( 1 ).path : QString();
    } );
  QSignalSpy spy( &panel, &ProcessingHistoryPanel::compareRequested );
  REQUIRE( spy.isValid() );
  compare->click();

  REQUIRE( spy.count() == 1 );
  const QList<QVariant> args = spy.takeFirst();
  // Baseline always emitted outputPaths.first() of each row: both assertions fail.
  CHECK( args.at( 0 ).toString() == left.outputPaths.at( 1 ) );
  CHECK( args.at( 1 ).toString() == right.outputPaths.at( 1 ) );
  CHECK( picks == 2 ); // one explicit pick per side, no forked picker
}

TEST_CASE( "R30-T06 F2: comparison only pairs real raster products, never a vanished one",
           "[r30_t06][history][f2][compare][behaviour]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  HistoryEntry left = finishedTask( 70 );
  left.outputPaths = { writeArtifact( dir, QStringLiteral( "l0.tif" ) ),
                       dir.filePath( QStringLiteral( "vanished.tif" ) ) };
  HistoryEntry right = finishedTask( 80 );
  right.outputPaths = { writeArtifact( dir, QStringLiteral( "r0.tif" ) ),
                        writeArtifact( dir, QStringLiteral( "r1.tif" ) ) };

  ProcessingHistoryPanel panel;
  panel.model()->setEntries( { left, right } );
  QTableView *view = viewOf( &panel );
  REQUIRE( view != nullptr );
  selectBothRows( view, panel.model() );
  QPushButton *compare = buttonByText( &panel, QStringLiteral( "Comparison" ) );
  REQUIRE( compare != nullptr );
  REQUIRE( compare->isEnabled() );

  // Deliberately ask for the LAST candidate on each side. The picker must
  // never even offer the vanished artifact (it is filtered before picking).
  QVector<QString> offered;
  panel.setArtifactPickFn(
    [&offered]( const QVector<ProcessingHistoryPanel::OutputArtifactChoice> &choices ) {
      for ( const auto &choice : choices )
        offered.append( choice.path );
      return choices.isEmpty() ? QString() : choices.last().path;
    } );
  QSignalSpy spy( &panel, &ProcessingHistoryPanel::compareRequested );
  REQUIRE( spy.isValid() );
  compare->click();

  REQUIRE( spy.count() == 1 );
  const QList<QVariant> args = spy.takeFirst();
  const QString pathA = args.at( 0 ).toString();
  const QString pathB = args.at( 1 ).toString();
  CHECK_FALSE( offered.contains( dir.filePath( QStringLiteral( "vanished.tif" ) ) ) );
  CHECK( QFileInfo::exists( pathA ) );
  CHECK( QFileInfo::exists( pathB ) );
  CHECK_FALSE( pathA.contains( QStringLiteral( "vanished" ) ) );
  CHECK_FALSE( pathB.contains( QStringLiteral( "vanished" ) ) );
  CHECK( ProcessingHistoryPanel::artifactKind( pathA ) == ProcessingHistoryPanel::ArtifactKind::Raster );
  CHECK( ProcessingHistoryPanel::artifactKind( pathB ) == ProcessingHistoryPanel::ArtifactKind::Raster );
}
