// test_shortcut_hosting.cpp — behavior-level guardrail for the shortcut
// host migration (UI review C1).
//
// Verified empirically on Qt 6.8: a QAction shortcut only fires while at
// least one of its associated widgets is *visible*. The ribbon shell keeps
// the classic QMenuBar hidden (hide() + setMaximumHeight(0)), so actions
// hosted solely on it never see Shortcut events — Ctrl+Shift+P, Ctrl+N/O/S
// and Undo/Redo were all dead. QgisDesktopWindow::forwardActionShortcutsToWindow()
// re-hosts every shortcut-bearing action on the window itself; this suite
// pins both halves of that contract with real key events (offscreen):
//
//   1. menubar-only hosting on a hidden bar does NOT fire (the bug);
//   2. the same action additionally hosted on the window DOES fire (fix);
//   3. a CommandRegistry action hosted on the window runs its handler;
//   4. the forwarding pass itself re-hosts menubar actions correctly.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/command_registry.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTest>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_shortcut_hosting";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

QMenuBar *hiddenMenuBar( QMainWindow &window )
{
  auto *bar = new QMenuBar( &window );
  bar->setObjectName( QStringLiteral( "rsHiddenMenuBar" ) );
  bar->setNativeMenuBar( false );
  bar->hide();
  bar->setMaximumHeight( 0 );
  return bar;
}

} // namespace

TEST_CASE( "Shortcuts: action hosted only on a hidden menubar never fires",
           "[shortcuts][regression][c1]" )
{
  ensureApp();
  QMainWindow window;
  QMenuBar *bar = hiddenMenuBar( window );

  int hits = 0;
  QAction *act = bar->addMenu( QStringLiteral( "&File" ) )
                     ->addAction( QStringLiteral( "Command Palette" ), &window,
                                  [&hits] { ++hits; } );
  act->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+P" ) ) );

  window.show();
  QTest::qWaitForWindowExposed( &window );
  QTest::keyClick( &window, Qt::Key_P,
                   Qt::ControlModifier | Qt::ShiftModifier );
  QTest::qWait( 1 );

  // The binding exists but every associated widget is invisible → dead.
  // This is the failure mode forwardActionShortcutsToWindow() repairs.
  CHECK( hits == 0 );
}

TEST_CASE( "Shortcuts: window-hosted menubar action fires on real key events",
           "[shortcuts][behavior][c1]" )
{
  ensureApp();
  QMainWindow window;
  QMenuBar *bar = hiddenMenuBar( window );

  int hits = 0;
  QAction *act = bar->addMenu( QStringLiteral( "&File" ) )
                     ->addAction( QStringLiteral( "New Project" ), &window,
                                  [&hits] { ++hits; } );
  act->setShortcut( QKeySequence( QStringLiteral( "Ctrl+N" ) ) );

  // The fix: re-host the action on the window itself; menu membership kept.
  window.addAction( act );

  window.show();
  QTest::qWaitForWindowExposed( &window );
  QTest::keyClick( &window, Qt::Key_N, Qt::ControlModifier );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
}

TEST_CASE( "Shortcuts: registry action hosted on the window runs its handler",
           "[shortcuts][behavior][registry]" )
{
  ensureApp();
  QMainWindow window;
  sicnu::app::CommandRegistry registry( &window );

  int runs = 0;
  sicnu::app::CommandDefinition d;
  d.id = QStringLiteral( "app.commandPalette" );
  d.title = QStringLiteral( "Command Palette..." );
  d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+P" ) );
  d.handler = [&runs] { ++runs; };
  REQUIRE( registry.registerCommand( d ) );

  QAction *act = registry.action( d.id, /*installShortcut=*/true );
  REQUIRE( act );
  window.addAction( act );

  window.show();
  QTest::qWaitForWindowExposed( &window );
  QTest::keyClick( &window, Qt::Key_P,
                   Qt::ControlModifier | Qt::ShiftModifier );
  QTest::qWait( 1 );

  CHECK( runs == 1 );
}

TEST_CASE( "Shortcuts: forwarding pass re-hosts menubar actions on the window",
           "[shortcuts][behavior][c1]" )
{
  ensureApp();
  QMainWindow window;
  QMenuBar *bar = hiddenMenuBar( window );

  int hits = 0;
  QAction *act = bar->addMenu( QStringLiteral( "&Edit" ) )
                     ->addAction( QStringLiteral( "Undo" ), &window,
                                  [&hits] { ++hits; } );
  act->setShortcut( QKeySequence::Undo );

  // Mirror of QgisDesktopWindow::forwardActionShortcutsToWindow().
  const QList<QAction *> acts = window.findChildren<QAction *>();
  for ( QAction *a : acts )
  {
    if ( !a || a->shortcuts().isEmpty() )
      continue;
    if ( a->shortcutContext() == Qt::WidgetShortcut ||
         a->shortcutContext() == Qt::WidgetWithChildrenShortcut )
      continue;
    window.addAction( a );
  }

  window.show();
  QTest::qWaitForWindowExposed( &window );
  QTest::keyClick( &window, Qt::Key_Z, Qt::ControlModifier );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
}

// The wiring half of this contract is now proven by a REAL shell
// construction: the `shell_lifecycle_smoke` CTest target runs the production
// binary with SICNU_SHELL_SELFTEST=1 and asserts the registry exists before
// setupMenu projected its actions (issue #1037 F-1031-P0-registry). The
// former source-text grep (`cpp.contains("forwardActionShortcutsToWindow()")`)
// was deleted: it stayed green when the function was reduced to an empty
// identifier-only body.
