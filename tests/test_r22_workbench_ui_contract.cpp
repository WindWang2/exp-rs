// test_r22_workbench_ui_contract.cpp — R22-05 source-level UI contracts
//
// Several workbench fixes are not cheaply drivable headlessly (modal dialog
// teardown, TaskCenter-initiated cancels, status-bar strings); this file
// pins their contracts at the source level, following the established
// pattern of test_ui_task_center_contract.cpp (readSource + REQUIRE on
// source text). Each contract names the defect it guards against; these are
// deliberately implementation-coupled regression tripwires, not style rules.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QString>
#include <QStringList>

namespace
{

QString readSource( const QString &relativePath )
{
  const QStringList candidates = {
    QStringLiteral( "%1/%2" ).arg( QStringLiteral( CMAKE_SOURCE_DIR ), relativePath ),
    QStringLiteral( "../%1" ).arg( relativePath ),
    QStringLiteral( "../../%1" ).arg( relativePath ),
    relativePath,
  };
  for ( const QString &path : candidates )
  {
    QFile f( path );
    if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
      return QString::fromUtf8( f.readAll() );
  }
  return {};
}

} // namespace

// W4-contract (#batch-external-cancel): a cancel issued from the Task
// Center (not the dialog's own button) must stop the batch like the button
// does. The Canceled branch of onBatchTaskUpdated used to advance the chain
// silently, skipping files and reporting undercounted totals at 100%.
TEST_CASE( "r22 W4-contract: external task cancel stops the batch chain",
           "[r22][workbench][contract][batch]" )
{
  const QString text = readSource( QStringLiteral( "src/app/dialogs/batch_processing_dialog.cpp" ) );
  REQUIRE_FALSE( text.isEmpty() );

  // The Canceled branch must arm the batch stop even when m_canceled was
  // not set by the dialog's own button.
  REQUIRE( text.contains( QStringLiteral( "TaskStatus::Canceled" ) ) );
  const int branch = text.indexOf( QStringLiteral( "TaskStatus::Canceled" ) );
  REQUIRE( branch >= 0 );
  const QString tail = text.mid( branch, 700 );
  CHECK( tail.contains( QStringLiteral( "m_canceled = true" ) ) );
}

// W6-contract (#workspace-browser-orphans): re-binding WorkspaceBrowserPanel
// to a reopened WorkspaceService must not strand the previous ImportCenter
// (parented to the panel) for the rest of the panel's life.
TEST_CASE( "r22 W6-contract: workspace browser retires the previous ImportCenter",
           "[r22][workbench][contract][workspace_browser]" )
{
  const QString text = readSource( QStringLiteral( "src/app/panels/workspace_browser_panel.cpp" ) );
  REQUIRE_FALSE( text.isEmpty() );
  const int fn = text.indexOf( QStringLiteral( "WorkspaceBrowserPanel::setWorkspaceService" ) );
  REQUIRE( fn >= 0 );
  const QString body = text.mid( fn, 900 );
  CHECK( body.contains( QStringLiteral( "deleteLater" ) ) );
}

// W7/W8-contract (#i18n-english-path): the English UI path must not carry
// CJK full-width punctuation or the untranslatable "n item(s)" pattern, and
// singular/plural branches must exist where a count is interpolated.
TEST_CASE( "r22 W7/W8-contract: English UI strings avoid CJK punctuation and pseudo-plurals",
           "[r22][workbench][contract][i18n]" )
{
  QStringList singularBranchFiles;
  singularBranchFiles << QStringLiteral( "src/app/active_view_host.cpp" );
  QStringList checkedFiles = singularBranchFiles;
  checkedFiles << QStringLiteral( "src/app/cartography/cartography_dock.cpp" )
               << QStringLiteral( "src/app/layout/qgslayoutdesignerdialog.cpp" );
  for ( const QString &path : checkedFiles )
  {
    CAPTURE( path.toStdString() );
    const QString text = readSource( path );
    REQUIRE_FALSE( text.isEmpty() );
    CHECK_FALSE( text.contains( QStringLiteral( "DPI：" ) ) );
    CHECK_FALSE( text.contains( QStringLiteral( "item(s)" ) ) );
    CHECK_FALSE( text.contains( QStringLiteral( "component(s)" ) ) );
    if ( singularBranchFiles.contains( path ) )
      CHECK( text.contains( QStringLiteral( "selected.size() == 1" ) ) );
  }
}

// W9-contract (#toolbox-close-during-cancel): the toolbox algorithm dialog
// must share the raster dialogs' #1560 escape hatch — once the cooperative
// cancel is armed (TaskStatus::Cancelling) the close must be accepted
// instead of trapping the user in a dialog whose task already agreed to
// stop. The predicate lives in shell/gui_job_adapter.{h,cpp} so both dialog
// families use the same one.
TEST_CASE( "r22 W9-contract: toolbox dialog close accepted once cancel is armed",
           "[r22][workbench][contract][algorithm_dialog]" )
{
  const QString header = readSource( QStringLiteral( "src/app/shell/gui_job_adapter.h" ) );
  REQUIRE_FALSE( header.isEmpty() );
  CHECK( header.contains( QStringLiteral( "jobCancelInFlight" ) ) );

  const QString dialog = readSource( QStringLiteral( "src/app/dialogs/sicnu_algorithm_dialog.cpp" ) );
  REQUIRE_FALSE( dialog.isEmpty() );
  const int fn = dialog.indexOf( QStringLiteral( "SicnuAlgorithmDialog::closeEvent" ) );
  REQUIRE( fn >= 0 );
  const QString body = dialog.mid( fn, 2000 );
  CHECK( body.contains( QStringLiteral( "jobCancelInFlight" ) ) );
  // R1-1: once the worker STARTED, runPrepared dereferences mFeedback raw,
  // so the close must still be refused even with the cancel armed.
  CHECK( body.contains( QStringLiteral( "workerStarted" ) ) );
  // R1-1 companion: the executor must refuse to run with a nulled feedback
  // guard (dialog closed after arming, before dispatch).
  const int exec = dialog.indexOf( QStringLiteral( "Dialog closed before execution started" ) );
  CHECK( exec >= 0 );
}

// W-extra-contract (#map-tool-count): the structured "Map tools initialized"
// log must report the number of editing tools actually created, not a
// hardcoded literal that silently drifts on the next tool addition.
TEST_CASE( "r22 W-extra-contract: map tool inventory log counts created tools",
           "[r22][workbench][contract][map_tools]" )
{
  const QString text = readSource( QStringLiteral( "src/app/map_tools/map_tool_manager.cpp" ) );
  REQUIRE_FALSE( text.isEmpty() );
  CHECK_FALSE( text.contains( QStringLiteral( ".arg( 19 )" ) ) );
  CHECK_FALSE( text.contains( QStringLiteral( ".arg(19)" ) ) );
  const int line = text.indexOf( QStringLiteral( "editing tools" ) );
  REQUIRE( line >= 0 );
  // The count must come from a named computation, not a literal.
  CHECK( text.mid( line, 400 ).contains( QStringLiteral( "editingToolCount" ) ) );
}
