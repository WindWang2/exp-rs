// test_workbench_full_shell_lifecycle.cpp — the full-shell/offscreen fixture
//
// The #1284 track delivered four component-level fixtures (session boundary,
// secondary view session, designer lifecycle, spatial tool registration) and
// recorded the full shell as future work: combined
// project+docks+editing+mission+lab+store state was never exercised by a
// test, and the window's own lifecycle entry points were unreachable because
// they sat behind QFileDialog. The shell now exposes dialog-free entry
// points (openProjectFrom / saveProjectAsTo) and read-only projections of
// the state whose binding IS the lifecycle contract (projectContext,
// secondaryMapSession, watchedMissionSidecars, lab recording getters).
//
// This suite drives the REAL QgisDesktopWindow offscreen through whole
// lifecycle stories:
//
//   1. open good A → probe-refused open of B → the session is still A's and
//      a save still targets A (the failed target can never be adopted, and
//      never gets overwritten);
//   2. A → new project → B: lab recording context, mission context and the
//      active map tool are reset at the story boundary, then re-bound to B;
//   3. Save As rebinds identity + governance store + mission sidecar
//      watcher; a failed Save As rolls identity/store/watcher back;
//   4. secondary view close→reopen keeps exactly one live engine view and a
//      fresh pixel-sync controller;
//   5. a layout designer left open retires when its layout dies at the
//      project clear;
//   6. close honours the shutdown policy: a refusing dirty bench aborts the
//      close; the unsaved-project prompt can abort or discard it.
//
// Modal QMessageBox calls inside these paths (open failure warning, unsaved
// prompt) are answered by a scheduled dismisser — dialog plumbing, not race
// timing: the nested exec() loop of the box runs the queued callbacks.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "main_window.h"
#include "project_context.h"
#include "shell/secondary_map_view_session.h"
#include "workbench/workbench_host.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <layout/qgslayoutdesignerdialog.h>
#include <qgsmapcanvas.h>
#include <qgsmaptool.h>
#include <qgsmaptoolpan.h>
#include <qgsproject.h>
#include <qgsrectangle.h>

// QgsProject + canvas keep thread-local QgsProjContext state that crashes
// during glibc atexit cleanup after a Catch2 run; bypass it with std::_Exit
// once Catch has reported the final result (suite precedent:
// test_project_session_boundary, test_secondary_map_view_session).
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
        std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                      ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                      static_cast<unsigned>( stats.totals.assertions.passed ),
                      static_cast<unsigned>( stats.totals.assertions.passed
                                             + stats.totals.assertions.failed ),
                      static_cast<unsigned>( stats.totals.testCases.passed ),
                      static_cast<unsigned>( stats.totals.testCases.passed
                                             + stats.totals.testCases.failed ) );
        std::fflush( stderr );
        std::_Exit( ok ? 0 : 1 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_workbench_full_shell_lifecycle";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    // The full shell needs widget platform support; CI hosts have none.
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    // Hermetic QSettings: the window saves/restores dock state and reads
    // preferences through the default QSettings constructor. A per-process
    // namespace keeps runs independent and never touches user config.
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "full-shell-lifecycle" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QApplication *app = new QApplication( fake_argc, fake_argv );
    QSettings().clear(); // drop any state a previous run of this suite saved
    return app;
  }
  return static_cast<QApplication *>( QCoreApplication::instance() );
}

/// Dismiss up to @p budget modal QMessageBoxes as they appear, clicking
/// @p button when non-null (fallback: close). Each dismissal schedules the
/// next probe, so a chain covers a story that pops several dialogs.
void armModalAnswer( QMessageBox::StandardButton button, int budget = 6 )
{
  if ( budget <= 0 )
    return;
  QTimer::singleShot( 0, ensureApp(), [button, budget] {
    if ( QWidget *modal = QApplication::activeModalWidget() )
    {
      if ( QMessageBox *box = qobject_cast<QMessageBox *>( modal ) )
      {
        if ( button != QMessageBox::NoButton )
        {
          if ( QAbstractButton *target = box->button( button ) )
          {
            target->click();
          }
          else
          {
            box->close();
          }
        }
        else
        {
          box->close();
        }
      }
      else
      {
        modal->close();
      }
    }
    armModalAnswer( button, budget - 1 );
  } );
}

/// A well-formed project file the real parser accepts.
bool writeValidProject( const QString &path )
{
  QgsProject probe;
  return probe.write( path );
}

QByteArray readBytes( const QString &path )
{
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return QByteArray();
  return f.readAll();
}

/// A dirty bench that refuses close — the shutdown policy's first stage.
class RefusingBench : public QObject, public sicnu::app::IWorkbench
{
    Q_OBJECT
  public:
    RefusingBench() { setObjectName( QStringLiteral( "testRefusingBench" ) ); }
    QString id() const override { return QStringLiteral( "test-refusing" ); }
    QString title() const override { return QStringLiteral( "测试工作台" ); }
    QIcon icon() const override { return QIcon(); }
    QWidget *primaryWidget() override { return nullptr; }
    void activate() override {}
    bool isActive() const override { return false; }
    bool isDirty() const override { return m_dirty; }
    bool requestClose() override { return !m_refuse; }
    sicnu::app::WorkbenchFeatures features() const override
    {
      using F = sicnu::app::WorkbenchFeature;
      return F::ExternalWindow;
    }
    void setRefusing( bool refuse )
    {
      m_refuse = refuse;
      m_dirty = refuse;
    }

  private:
    bool m_refuse = true;
    bool m_dirty = true;
};


struct ShellFixture
{
  QgisDesktopWindow window;

  ShellFixture()
  {
    window.show();
    QTest::qWaitForWindowExposed( &window );
  }
};

/// Two sibling projects in Unicode directories — the combined lifecycle
/// must hold on non-ASCII paths (Save As targets, sidecars, stores).
struct ProjectPair
{
  QTemporaryDir tmp;
  QString dirA;
  QString dirB;
  QString pathA;
  QString pathB;

  ProjectPair()
  {
    REQUIRE( tmp.isValid() );
    dirA = tmp.filePath( QStringLiteral( "项目甲" ) );
    dirB = tmp.filePath( QStringLiteral( "项目乙" ) );
    REQUIRE( QDir().mkpath( dirA ) );
    REQUIRE( QDir().mkpath( dirB ) );
    pathA = QDir( dirA ).filePath( QStringLiteral( "工程A.qgs" ) );
    pathB = QDir( dirB ).filePath( QStringLiteral( "工程B.qgs" ) );
    REQUIRE( writeValidProject( pathA ) );
    REQUIRE( writeValidProject( pathB ) );
  }

  QString sidecarA() const { return QDir( dirA ).filePath( QStringLiteral( "工程A.mission.json" ) ); }
  QString sidecarB() const { return QDir( dirB ).filePath( QStringLiteral( "工程B.mission.json" ) ); }
  QString storeA() const { return QDir( dirA ).filePath( QStringLiteral( "工程A.governance.db" ) ); }
  QString storeB() const { return QDir( dirB ).filePath( QStringLiteral( "工程B.governance.db" ) ); }
};

} // namespace

TEST_CASE( "Full shell: probe-refused open leaves the live session and its save target untouched",
           "[app][full_shell][open_fail]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;

  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  CHECK( fx.window.windowTitle().contains( QStringLiteral( "工程A" ) ) );
  CHECK( QgsProject::instance()->fileName() == projects.pathA );
  REQUIRE( fx.window.projectContext() );
  CHECK( fx.window.projectContext()->workspaceService().isStoreOpen() );
  CHECK( QFile::exists( projects.storeA() ) );

  // A corrupt target next door: the probe refuses it, the live session
  // (identity, store, title) must not budge.
  const QString corrupt = projects.tmp.filePath( QStringLiteral( "损坏.qgs" ) );
  {
    QFile f( corrupt );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    f.write( "this is not a project document\n" );
  }
  const QByteArray corruptBefore = readBytes( corrupt );

  armModalAnswer( QMessageBox::Ok );
  CHECK( !fx.window.openProjectFrom( corrupt ) );

  CHECK( fx.window.windowTitle().contains( QStringLiteral( "工程A" ) ) );
  CHECK( QgsProject::instance()->fileName() == projects.pathA );
  CHECK( fx.window.projectContext()->workspaceService().isStoreOpen() );
  CHECK( readBytes( corrupt ) == corruptBefore );

  // The point of the old-or-new contract: a save after the failed open
  // writes THE OPEN PROJECT, never the refused target.
  armModalAnswer( QMessageBox::Ok );
  fx.window.saveProject();
  CHECK( QgsProject::instance()->fileName() == projects.pathA );
  CHECK( QFile::exists( projects.pathA ) );
  CHECK( readBytes( corrupt ) == corruptBefore );
}

TEST_CASE( "Full shell: new project resets story state, open re-binds it",
           "[app][full_shell][story_boundary]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;

  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  // Auto lab recording (opt-out default) binds A's experiment db.
  CHECK( fx.window.labExperimentDbPath()
         == QDir( projects.dirA ).filePath( QStringLiteral( ".sicnu/lab/experiments.db" ) ) );
  CHECK( fx.window.labExperimentId() == QStringLiteral( "lab-工程A" ) );
  // Simulate the live mission a studio/agent session left behind.
  fx.window.missionContext().missionId = QStringLiteral( "mission-from-A" );
  // And a non-pan active tool: the session owns an interaction context.
  fx.window.selectFeatures();
  QgsMapTool *active = fx.window.mapCanvas()->mapTool();
  REQUIRE( active );
  CHECK( QLatin1String( active->metaObject()->className() )
         != QLatin1String( "QgsMapToolPan" ) );

  // A fresh open can already carry the modified marker (CRS/serializer
  // restoration re-stamps state during the read), so New Project legitimately
  // runs the unsaved-changes prompt: answer with Discard — the interactive
  // equivalent of dropping A's story.
  armModalAnswer( QMessageBox::Discard );
  fx.window.newProject();

  // The empty session owns no story and no tool context.
  CHECK( fx.window.labExperimentDbPath().isEmpty() );
  CHECK( fx.window.labExperimentId().isEmpty() );
  CHECK( fx.window.labWorkspaceRoot().isEmpty() );
  CHECK( fx.window.missionContext().missionId.isEmpty() );
  CHECK( qobject_cast<QgsMapToolPan *>( fx.window.mapCanvas()->mapTool() ) );
  CHECK( QgsProject::instance()->fileName().isEmpty() );
  CHECK( fx.window.windowTitle().contains( QStringLiteral( "Untitled Project" ) ) );

  // Opening B re-binds the story to B — nothing from A leaks in.
  REQUIRE( fx.window.openProjectFrom( projects.pathB ) );
  CHECK( QgsProject::instance()->fileName() == projects.pathB );
  CHECK( fx.window.labExperimentDbPath()
         == QDir( projects.dirB ).filePath( QStringLiteral( ".sicnu/lab/experiments.db" ) ) );
  CHECK( fx.window.labExperimentId() == QStringLiteral( "lab-工程B" ) );
  CHECK( fx.window.missionContext().missionId != QStringLiteral( "mission-from-A" ) );
}

TEST_CASE( "Full shell: Save As rebinds store and mission watcher, failed Save As rolls identity back",
           "[app][full_shell][save_as]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;

  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  // First save publishes the mission sidecar next to A (first publication).
  armModalAnswer( QMessageBox::Ok );
  fx.window.saveProject();
  CHECK( QFile::exists( projects.sidecarA() ) );

  // Save As to the Unicode sibling: identity, governance store, mission
  // authority ref and the out-of-process watcher must all move to the new
  // file. A stale projectRef is not cosmetic — mission reconciliation
  // refuses to run while it differs from the open file.
  REQUIRE( fx.window.saveProjectAsTo( projects.pathB ) );
  CHECK( QgsProject::instance()->fileName() == projects.pathB );
  CHECK( fx.window.windowTitle().contains( QStringLiteral( "工程B" ) ) );
  CHECK( fx.window.projectContext()->workspaceService().isStoreOpen() );
  CHECK( fx.window.missionContext().projectRef == projects.pathB );
  CHECK( QFile::exists( projects.storeB() ) );
  CHECK( QFile::exists( projects.sidecarB() ) );
  CHECK( fx.window.watchedMissionSidecars()
         == QStringList{ projects.sidecarB() } );

  // Failed Save As: the target is an existing DIRECTORY, so the write must
  // refuse. Identity, store binding, mission ref and watcher stay on B —
  // and B's bytes are exactly what they were.
  const QString dirTarget = projects.tmp.filePath( QStringLiteral( "目标目录" ) );
  REQUIRE( QDir().mkpath( dirTarget ) );
  const QByteArray projectBBefore = readBytes( projects.pathB );
  armModalAnswer( QMessageBox::Ok );
  CHECK( !fx.window.saveProjectAsTo( dirTarget ) );
  CHECK( QgsProject::instance()->fileName() == projects.pathB );
  CHECK( fx.window.windowTitle().contains( QStringLiteral( "工程B" ) ) );
  CHECK( fx.window.projectContext()->workspaceService().isStoreOpen() );
  CHECK( fx.window.missionContext().projectRef == projects.pathB );
  CHECK( fx.window.watchedMissionSidecars()
         == QStringList{ projects.sidecarB() } );
  CHECK( readBytes( projects.pathB ) == projectBBefore );
}

TEST_CASE( "Full shell: secondary view reopen keeps one engine view and a live sync",
           "[app][full_shell][secondary_view]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;
  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  REQUIRE( fx.window.projectContext() );

  for ( int cycle = 1; cycle <= 3; ++cycle )
  {
    INFO( "reopen cycle " << cycle );
    fx.window.openSecondaryMapView();
    SecondaryMapSession *session = fx.window.secondaryMapSession();
    REQUIRE( session );
    REQUIRE( session->isOpen() );
    // The pixel-sync controller must exist on EVERY open (the B2 contract).
    CHECK( session->syncController() );
    // Main + exactly one live secondary engine view — never an accumulation.
    CHECK( fx.window.projectContext()->views().size() == 2 );

    // The secondary follows the primary's extent center.
    fx.window.mapCanvas()->setExtent(
        QgsRectangle( 100, 100, 200, 200 ), false );
    fx.window.mapCanvas()->refresh();
    QTest::qWait( 60 );

    fx.window.closeSecondaryMapView();
    CHECK( !session->isOpen() );
    CHECK( fx.window.projectContext()->views().size() == 1 );
  }

  // After the last close the session must be fully re-openable.
  fx.window.openSecondaryMapView();
  CHECK( fx.window.secondaryMapSession()->syncController() );
  fx.window.closeSecondaryMapView();
}

TEST_CASE( "Full shell: designer left open retires when the project clears its layout",
           "[app][full_shell][designer]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;
  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );

  fx.window.newLayout();
  QPointer<QgsLayoutDesignerDialog> designer =
      fx.window.findChild<QgsLayoutDesignerDialog *>();
  REQUIRE( !designer.isNull() );

  // The clear removes the layout through the manager; the designer's
  // QObject-destroyed defense must retire it instead of dangling. The
  // unsaved-changes prompt may legitimately appear for the fresh open —
  // discard A's story.
  armModalAnswer( QMessageBox::Discard );
  fx.window.newProject();
  QTest::qWait( 60 );
  CHECK( designer.isNull() );
}

TEST_CASE( "Full shell: close honours the shutdown policy and the unsaved prompt",
           "[app][full_shell][shutdown]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;
  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );

  RefusingBench bench;
  REQUIRE( fx.window.workbenchHost()->registerWorkbench( &bench ) );

  // Stage 1 of the shutdown policy: a dirty bench that refuses to close
  // aborts the quit before any project-level prompt.
  bench.setRefusing( true );
  fx.window.close();
  CHECK( fx.window.isVisible() );

  bench.setRefusing( false );
  // Project is clean now → close proceeds without a prompt.
  fx.window.close();
  CHECK( !fx.window.isVisible() );

  // Reopen the shell story: an unsaved project close must ask, and Cancel
  // must keep the window up.
  QgisDesktopWindow window2;
  window2.show();
  QTest::qWaitForWindowExposed( &window2 );
  REQUIRE( window2.openProjectFrom( projects.pathA ) );
  QgsProject::instance()->setDirty( true );

  // The armed answer fires inside the prompt's own nested exec loop.
  armModalAnswer( QMessageBox::Cancel, 1 );
  window2.close();
  QTest::qWait( 60 );
  CHECK( window2.isVisible() );

  // Second close: discard the changes → the window really goes away.
  armModalAnswer( QMessageBox::Discard, 1 );
  window2.close();
  QTest::qWait( 60 );
  CHECK( !window2.isVisible() );
}

TEST_CASE( "Full shell: saved layout state that cannot be restored is dropped, not retried forever",
           "[app][full_shell][restore_state][b12]" )
{
  ensureApp();
  {
    QSettings settings;
    settings.clear();
    // A same-version but corrupt state blob: restoreState() refuses it.
    // Pre-B12 the return value was ignored and the blob came back on every
    // launch; the contract now is drop-once so the next savePanelState
    // rewrites real state.
    settings.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), 11 );
    settings.setValue( QStringLiteral( "mainwindow/state" ),
                       QByteArray( "not-a-valid-qmainwindow-state-blob" ) );
    // Geometry: garbage bytes must be dropped too. (No leading NUL: a
    // QByteArray literal truncates there and the poison would become an
    // empty — skipped — entry.)
    settings.setValue( QStringLiteral( "mainwindow/geometry" ),
                       QByteArray( "junk-that-is-not-window-geometry" ) );
  }

  ShellFixture fx;

  QSettings settings;
  CHECK( !settings.contains( QStringLiteral( "mainwindow/state" ) ) );
  CHECK( !settings.contains( QStringLiteral( "mainwindow/geometry" ) ) );
  // The constructor's restore path re-stamps the contract version it
  // accepts (a ShellFixture ctor ran with the corrupt entries present, so
  // the version key itself is untouched state — still 11).
  CHECK( settings.value( QStringLiteral( "mainwindow/shellLayoutVersion" ) )
             .toInt() == 11 );

  // A state saved by a NEWER shell must not be restored into this older
  // binary either: the exact-version gate drops it to the baseline.
  {
    QSettings newer;
    newer.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), 99 );
    newer.setValue( QStringLiteral( "mainwindow/state" ),
                    QByteArray( "future-layout-state" ) );
  }
  {
    ShellFixture fx2;
    QSettings after;
    CHECK( !after.contains( QStringLiteral( "mainwindow/state" ) ) );
    CHECK( after.value( QStringLiteral( "mainwindow/shellLayoutVersion" ) )
               .toInt() == 11 );
  }
}

#include "test_workbench_full_shell_lifecycle.moc"
