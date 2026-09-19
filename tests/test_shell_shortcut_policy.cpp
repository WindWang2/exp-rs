// test_shell_shortcut_policy.cpp — issue #1037 F-1031-P1-letterkey.
//
// The shell re-hosts every shortcut-bearing action on the window so a hidden
// menubar cannot kill the binding (UI review C1). Unmodified letter bindings
// (map.pan = H) must NOT steal typing from a focused editor: the policy claims
// the ShortcutOverride for printable keys while a text-input widget has focus,
// so Qt delivers the key as a normal KeyPress. Modified chords and
// non-editable focus keep the window-hosted action path.
//
// Qt behavior locked here (Qt 6.8): text editors accept ShortcutOverride only
// for the common clipboard/cursor shortcuts, not for plain letters, so without
// the policy a window-hosted `H` fires while the user types "Hello".
#include <catch2/catch_test_macros.hpp>

#include "app/shell/shell_shortcut_policy.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeySequence>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTest>
#include <QTextEdit>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_shell_shortcut_policy";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

/// Window-hosted action bound to `H`, mirroring QgisDesktopWindow::setupMenu
/// (registry action installed with its canonical shortcut) plus
/// forwardActionShortcutsToWindow().
QAction *addHShortcutAction( QMainWindow &window, int *hits )
{
  auto *act = new QAction( QStringLiteral( "Pan" ), &window );
  act->setShortcut( QKeySequence( QStringLiteral( "H" ) ) );
  QObject::connect( act, &QAction::triggered, &window, [hits] { ++*hits; } );
  window.addAction( act );
  return act;
}

} // namespace

TEST_CASE( "Shortcut policy: focused QLineEdit keeps printable letters", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *edit = new QLineEdit( &window );
  window.setCentralWidget( edit );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  edit->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( edit, Qt::Key_H );
  QTest::keyClick( edit, Qt::Key_E );
  QTest::qWait( 1 );

  CHECK( hits == 0 );
  CHECK( edit->text() == QStringLiteral( "he" ) );
}

TEST_CASE( "Shortcut policy: focused QTextEdit (copilot-style) keeps printable letters", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *edit = new QTextEdit( &window );
  window.setCentralWidget( edit );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  edit->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( edit, Qt::Key_H );
  QTest::qWait( 1 );

  CHECK( hits == 0 );
  CHECK( edit->toPlainText() == QStringLiteral( "h" ) );
}

TEST_CASE( "Shortcut policy: map context (non-editable focus) keeps the letter shortcut", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *canvasLike = new QPlainTextEdit( &window );
  canvasLike->setReadOnly( true ); // a non-input focus target
  window.setCentralWidget( canvasLike );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  canvasLike->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( canvasLike, Qt::Key_H );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
}

TEST_CASE( "Shortcut policy: modified chords reach their action even with editor focus", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  auto *act = new QAction( QStringLiteral( "Palette" ), &window );
  act->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+P" ) ) );
  QObject::connect( act, &QAction::triggered, &window, [&hits] { ++hits; } );
  window.addAction( act );

  auto *edit = new QLineEdit( &window );
  window.setCentralWidget( edit );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  edit->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( edit, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
  CHECK( edit->text().isEmpty() );
}

TEST_CASE( "Shortcut policy: read-only editors release the key to the shortcut", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  CHECK( sicnu::app::ShellShortcutPolicy::isTextInputWidget( nullptr ) == false );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *edit = new QLineEdit( &window );
  edit->setReadOnly( true );
  window.setCentralWidget( edit );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  edit->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( edit, Qt::Key_H );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
  CHECK( edit->text().isEmpty() );
}

TEST_CASE( "Shortcut policy: read-only spin box releases the key to the shortcut", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *spin = new QSpinBox( &window );
  spin->setReadOnly( true );
  window.setCentralWidget( spin );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  spin->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( spin, Qt::Key_H );
  QTest::qWait( 1 );

  CHECK( hits == 1 );
}

TEST_CASE( "Shortcut policy: non-editable combos keep keyboard-search letters", "[shortcuts][policy][1037]" )
{
  ensureApp();
  sicnu::app::ShellShortcutPolicy policy;
  qApp->installEventFilter( &policy );

  QMainWindow window;
  int hits = 0;
  addHShortcutAction( window, &hits );

  auto *combo = new QComboBox( &window );
  combo->setEditable( false );
  combo->addItems( { QStringLiteral( "alpha" ), QStringLiteral( "hills" ) } );
  window.setCentralWidget( combo );
  window.show();
  (void) QTest::qWaitForWindowExposed( &window );
  combo->setFocus();
  QTest::qWait( 1 );

  QTest::keyClick( combo, Qt::Key_H );
  QTest::qWait( 1 );

  // A plain combo consumes printable keys for keyboard search; the window
  // shortcut must not steal them.
  CHECK( hits == 0 );
  CHECK( combo->currentText() == QStringLiteral( "hills" ) );
}
