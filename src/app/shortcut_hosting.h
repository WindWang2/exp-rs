// shortcut_hosting.h — which QAction shortcuts may be re-hosted on a window
// (hidden-menubar shell, F-1031-P1-letterkey / F-1031-P2-tests).
#pragma once

#include <QAction>
#include <QKeySequence>
#include <QWidget>

/**
 * True when @a action should be QWidget::addAction()'d onto the main window
 * so a hidden menubar does not swallow its shortcut.
 *
 * Widget-scoped shortcuts stay on their widget. Unmodified letter/number
 * keys (no Ctrl/Alt/Meta — Shift alone does not count) are not window-hosted:
 * they would steal typing in Copilot / the command palette / forms (map.pan
 * is "H"). Ctrl+N / Ctrl+S / Ctrl+Shift+P remain eligible.
 */
inline bool rsShouldWindowHostAction( const QAction *action )
{
  if ( !action || action->shortcuts().isEmpty() )
    return false;
  const auto ctx = action->shortcutContext();
  if ( ctx == Qt::WidgetShortcut || ctx == Qt::WidgetWithChildrenShortcut )
    return false;

  for ( const QKeySequence &seq : action->shortcuts() )
  {
    for ( int i = 0; i < seq.count(); ++i )
    {
      const QKeyCombination comb = seq[i];
      const int key = static_cast<int>( comb.key() );
      const Qt::KeyboardModifiers mods = comb.keyboardModifiers();
      const bool hasNonShiftMod = mods.testFlag( Qt::ControlModifier )
                                  || mods.testFlag( Qt::AltModifier )
                                  || mods.testFlag( Qt::MetaModifier );
      const bool letterOrNumber = ( key >= Qt::Key_A && key <= Qt::Key_Z )
                                  || ( key >= Qt::Key_0 && key <= Qt::Key_9 );
      if ( letterOrNumber && !hasNonShiftMod )
        return false;
    }
  }
  return true;
}

/// Re-host every eligible shortcut-bearing child action onto @a host.
inline void rsForwardActionShortcutsToWidget( QWidget *host )
{
  if ( !host )
    return;
  const QList<QAction *> acts = host->findChildren<QAction *>();
  for ( QAction *action : acts )
  {
    if ( rsShouldWindowHostAction( action ) )
      host->addAction( action );
  }
}
