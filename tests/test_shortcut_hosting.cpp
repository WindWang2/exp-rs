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
//   4. the hosting rule (sicnu::app::actionShortcutIsWindowSafe, #1031) decides
//      what the real forwarding pass may host, and bare typing keys lose.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/command_defs.h"
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

// The mechanism above is only as good as the rule that decides what may be
// window-hosted. QgisDesktopWindow::forwardActionShortcutsToWindow() filters
// through sicnu::app::actionShortcutIsWindowSafe(): a bare unmodified letter
// key must stay off the window, otherwise typing in the Copilot / palette /
// any text field activates a map command (#1031 F-1031-P1-letterkey, map.pan
// was "H"). The rule is an inline helper, so it runs here directly -- no shell
// construction and no source-text grep.
TEST_CASE( "Shortcuts: window-hosting rule rejects bare typing keys",
           "[shortcuts][behavior][letterkey][1031]" )
{
  ensureApp();
  QObject owner; // keeps the probe actions owned

  QAction modified( &owner );
  modified.setShortcut( QKeySequence( QStringLiteral( "Ctrl+N" ) ) );
  CHECK( sicnu::app::actionShortcutIsWindowSafe( &modified ) );

  QAction shiftLetter( &owner );
  shiftLetter.setShortcut( QKeySequence( QStringLiteral( "Shift+H" ) ) );
  CHECK( sicnu::app::actionShortcutIsWindowSafe( &shiftLetter ) );

  QAction functionKey( &owner );
  functionKey.setShortcut( QKeySequence( QStringLiteral( "F5" ) ) );
  CHECK( sicnu::app::actionShortcutIsWindowSafe( &functionKey ) );

  // The reported theft: map.pan was bound to a bare "H".
  QAction bareLetter( &owner );
  bareLetter.setShortcut( QKeySequence( QStringLiteral( "H" ) ) );
  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( &bareLetter ) );

  QAction bareDigit( &owner );
  bareDigit.setShortcut( QKeySequence( QStringLiteral( "5" ) ) );
  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( &bareDigit ) );

  QAction barePunctuation( &owner );
  barePunctuation.setShortcut( QKeySequence( QStringLiteral( "Space" ) ) );
  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( &barePunctuation ) );

  // The rule answers "may this be hosted on the window?" for every action.
  QAction widgetScoped( &owner );
  widgetScoped.setShortcut( QKeySequence( QStringLiteral( "Ctrl+N" ) ) );
  widgetScoped.setShortcutContext( Qt::WidgetShortcut );
  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( &widgetScoped ) );

  QAction noShortcut( &owner );
  noShortcut.setText( QStringLiteral( "no binding" ) );
  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( &noShortcut ) );

  CHECK_FALSE( sicnu::app::actionShortcutIsWindowSafe( nullptr ) );
}

// The forwarding pass itself, decided by the real rule (the shell method
// cannot be constructed in a shell-free test, but its decision is):
// modified keys reach the window host and fire, typing keys do not.
TEST_CASE( "Shortcuts: forwarding hosts Ctrl+Z but never a bare letter key",
           "[shortcuts][behavior][letterkey][1031]" )
{
  ensureApp();
  QMainWindow window;
  QMenuBar *bar = hiddenMenuBar( window );

  int undoHits = 0;
  QAction *undoAction = bar->addMenu( QStringLiteral( "&Edit" ) )
                            ->addAction( QStringLiteral( "Undo" ), &window,
                                         [&undoHits] { ++undoHits; } );
  undoAction->setShortcut( QKeySequence::Undo );

  int panHits = 0;
  QAction *panAction = bar->addMenu( QStringLiteral( "&View" ) )
                           ->addAction( QStringLiteral( "Pan" ), &window,
                                        [&panHits] { ++panHits; } );
  panAction->setShortcut( QKeySequence( QStringLiteral( "H" ) ) );

  const QList<QAction *> acts = window.findChildren<QAction *>();
  for ( QAction *a : acts )
  {
    if ( !sicnu::app::actionShortcutIsWindowSafe( a ) )
      continue;
    if ( a->shortcutContext() == Qt::WidgetShortcut ||
         a->shortcutContext() == Qt::WidgetWithChildrenShortcut )
      continue;
    window.addAction( a );
  }

  REQUIRE( sicnu::app::actionShortcutIsWindowSafe( undoAction ) );
  REQUIRE_FALSE( sicnu::app::actionShortcutIsWindowSafe( panAction ) );
  CHECK( window.actions().contains( undoAction ) );
  CHECK_FALSE( window.actions().contains( panAction ) );

  window.show();
  QTest::qWaitForWindowExposed( &window );
  QTest::keyClick( &window, Qt::Key_Z, Qt::ControlModifier );
  QTest::qWait( 1 );
  CHECK( undoHits == 1 );

  // Typing "H" must not trigger pan from the window host.
  QTest::keyClick( &window, Qt::Key_H );
  QTest::qWait( 1 );
  CHECK( panHits == 0 );
}
