/***************************************************************************
 * shell_selftest.cpp — see header
 ***************************************************************************/
#include "shell_selftest.h"

#include "main_window.h"
#include "workbench/command_registry.h"

#include "georeferencer/qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QSettings>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <QWindow>

#include <qgsmapcanvas.h>
#include <qgsmaptool.h>

namespace sicnu::app
{
namespace
{

void setFailure( QString *failure, const QString &message )
{
  if ( failure && failure->isEmpty() )
    *failure = message;
}

/// Waits for the offscreen window to be exposed — QAction shortcut delivery
/// requires an exposed, active window (same precondition as QTest's
/// qWaitForWindowExposed used by the shortcut-hosting suite).
void waitForExposure( QWidget *widget )
{
  if ( !widget )
    return;
  widget->raise();
  widget->activateWindow();
  for ( int i = 0; i < 200; ++i )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    if ( widget->windowHandle() && widget->windowHandle()->isExposed() )
      return;
    QThread::msleep( 5 );
  }
}

/// Sends a synthetic key press to @p receiver and processes events. Synthetic
/// events go through QApplicationPrivate::notify → qt_sendShortcutOverrideEvent,
/// the exact path QTest::keyClick drives, without linking Qt Test into the
/// production binary.
void sendKey( QWidget *receiver, int key, const QString &text )
{
  QWidget *focus = receiver;
  if ( focus )
    focus->setFocus();
  QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier, text );
  QCoreApplication::sendEvent( receiver, &press );
  QCoreApplication::processEvents();
}

} // namespace

bool runShellSelfTest( QgisDesktopWindow *window, QString *failure )
{
  if ( failure )
    failure->clear();
  if ( !window )
  {
    setFailure( failure, QStringLiteral( "window is null" ) );
    return false;
  }

  // Hermetic settings: the georeferencer's close path persists geometry and a
  // workflow snapshot; never touch the developer's real QSettings.
  QSettings::setDefaultFormat( QSettings::IniFormat );
  QSettings::setPath( QSettings::IniFormat, QSettings::UserScope,
                      QDir::temp().filePath( QStringLiteral( "sicnu-shell-selftest" ) ) );

  waitForExposure( window );

  // 1. Registry liveness + ordering: setupMenu() runs before the workbench
  //    wiring in the constructor and projects registry-backed entries. A live,
  //    populated registry and its materialized menu actions prove the
  //    ordering held (#1037 F-1031-P0-registry).
  CommandRegistry *registry = window->commandRegistry();
  if ( !registry )
  {
    setFailure( failure, QStringLiteral( "commandRegistry() is null after construction" ) );
    return false;
  }
  if ( registry->count() == 0 || !registry->definition( QStringLiteral( "project.new" ) ) )
  {
    setFailure( failure, QStringLiteral( "shell commands were not registered before setupMenu()" ) );
    return false;
  }

  QAction *newProjectAction = window->findChild<QAction *>( QStringLiteral( "cmd_project.new" ) );
  if ( !newProjectAction )
  {
    setFailure( failure, QStringLiteral( "setupMenu did not project cmd_project.new" ) );
    return false;
  }
  if ( newProjectAction->shortcut() != QKeySequence( QKeySequence::New ) )
  {
    setFailure( failure, QStringLiteral( "cmd_project.new lost its canonical Ctrl+N binding" ) );
    return false;
  }

  QAction *panAction = window->findChild<QAction *>( QStringLiteral( "cmd_map.pan" ) );
  if ( !panAction )
  {
    setFailure( failure, QStringLiteral( "setupMenu did not project cmd_map.pan" ) );
    return false;
  }
  if ( panAction->shortcut() != QKeySequence( QStringLiteral( "H" ) ) )
  {
    setFailure( failure, QStringLiteral( "cmd_map.pan lost its canonical H binding" ) );
    return false;
  }
  if ( !window->actions().contains( panAction ) )
  {
    // forwardActionShortcutsToWindow() must re-host the action on the window;
    // a hidden-menubar-only host makes every menu shortcut dead.
    setFailure( failure, QStringLiteral( "cmd_map.pan is not hosted on the window" ) );
    return false;
  }

  // 2. Letter-shortcut policy: the real shell must not steal printable input
  //    from a focused editor (#1037 F-1031-P1-letterkey). The probe is a
  //    QTextEdit/QPlainTextEdit: Qt's QLineEdit already claims unmodified
  //    printable ShortcutOverride itself, so only the multi-line editors
  //    (Copilot dock, notes) actually exercise the policy.
  int panHits = 0;
  QMetaObject::Connection panConn = QObject::connect(
    panAction, &QAction::triggered, window, [&panHits] { ++panHits; } );

  auto *probeEdit = new QTextEdit( window );
  probeEdit->setObjectName( QStringLiteral( "rsShellSelfTestEditor" ) );
  probeEdit->show();
  sendKey( probeEdit, Qt::Key_H, QStringLiteral( "h" ) );
  const QString typed = probeEdit->toPlainText();

  QObject::disconnect( panConn );
  if ( panHits != 0 )
  {
    setFailure( failure, QStringLiteral( "typing 'h' in an editor triggered map.pan" ) );
    return false;
  }
  if ( !typed.contains( QLatin1Char( 'h' ) ) )
  {
    setFailure( failure, QStringLiteral( "the editor did not receive the typed character" ) );
    return false;
  }

  // 3. Map context: the same binding must still fire when no editor has focus.
  panHits = 0;
  panConn = QObject::connect( panAction, &QAction::triggered, window, [&panHits] { ++panHits; } );
  sendKey( window, Qt::Key_H, QStringLiteral( "h" ) );
  QObject::disconnect( panConn );
  if ( panHits != 1 )
  {
    setFailure( failure, QStringLiteral( "the map.pan binding did not fire outside an editor" ) );
    return false;
  }

  // 4. Registry map-tool commands: switching tools must stay crash-free.
  registry->trigger( QStringLiteral( "map.pan" ) );
  QCoreApplication::processEvents();
  registry->trigger( QStringLiteral( "map.identify" ) );
  QCoreApplication::processEvents();

  // 5. Fresh georeferencer window: born clean (#1052), closes without the
  //    "unsaved control points" prompt, and its canvases destroy cleanly
  //    (the #1048 early-destruction path).
  window->openGeorefImageToImage();
  QCoreApplication::processEvents();
  auto *georefWindow = window->findChild<QgsGeoreferencerMainWindow *>();
  if ( !georefWindow )
  {
    setFailure( failure, QStringLiteral( "openGeorefImageToImage created no window" ) );
    return false;
  }
  if ( georefWindow->isDirtyForTest() )
  {
    setFailure( failure, QStringLiteral( "a fresh georeferencer window is dirty from birth" ) );
    return false;
  }
  georefWindow->close();
  QCoreApplication::processEvents();

  delete probeEdit;
  return true;
}

} // namespace sicnu::app
