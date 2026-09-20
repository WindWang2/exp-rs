/***************************************************************************
 * command_defs.h — shell command definitions (Workbench 5.0, Milestone C)
 *
 * Wires the existing QgisDesktopWindow slots into CommandDefinition entries:
 * one handler, one availability contract and one canonical shortcut per
 * capability. Surfaces (ribbon / hidden menu host / context menus / palette)
 * project these through CommandRegistry::action().
 *
 * Batch 1 covers the primary capability set; follow-up milestones migrate
 * the remaining dialogs and digitizing tools and make the menu host consume
 * these definitions directly.
 ***************************************************************************/
#pragma once

#include <QtGlobal>

#include <QAction>
#include <QKeySequence>

class QgisDesktopWindow;

namespace sicnu::app
{
class CommandRegistry;
}

namespace sicnu::app
{

/**
 * Window-hosting rule for QgisDesktopWindow::forwardActionShortcutsToWindow()
 * (issue #1031, F-1031-P1-letterkey).
 *
 * The forwarding pass re-hosts menubar actions on the window so their
 * shortcuts fire while the menubar itself stays hidden. A bare unmodified
 * letter / digit / punctuation key must NOT be window-hosted: Qt delivers
 * those keys to the focused widget and never to the window as a ShortcutOverride,
 * so a window-hosted "H" (map.pan) fires while the user is typing in the
 * Copilot, the command palette or any text field. Modified keys (Ctrl/Alt/
 * Shift+key), function keys and hardware keys stay safe.
 *
 * Inline so the rule can be exercised by tests that do not link the whole
 * shell (tests/test_shortcut_hosting.cpp).
 */
inline bool actionShortcutIsWindowSafe( const QAction *action )
{
    if ( !action || action->shortcuts().isEmpty() )
        return false;
    // A widget-scoped action is not the window's to host: its context belongs
    // to the widget that owns it, and window-hosting would widen it to global.
    if ( action->shortcutContext() == Qt::WidgetShortcut
         || action->shortcutContext() == Qt::WidgetWithChildrenShortcut )
        return false;
    for ( const QKeySequence &seq : action->shortcuts() )
    {
        if ( seq.isEmpty() || seq.count() == 0 )
            continue;
        const int combined = seq[0].toCombined();
        if ( combined == 0 || combined == Qt::Key_unknown )
            continue;
        const int key = combined & ~static_cast<int>( Qt::KeyboardModifierMask );
        const Qt::KeyboardModifiers mods = static_cast<Qt::KeyboardModifiers>(
            combined & static_cast<int>( Qt::KeyboardModifierMask ) );
        if ( mods == Qt::NoModifier && key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde )
            return false;
    }
    return true;
}

} // namespace sicnu::app

/// Registers the shell's built-in commands on @a registry. @a window is the
/// handler target (raw pointer, must outlive the registry).
void registerShellCommands( sicnu::app::CommandRegistry *registry, QgisDesktopWindow *window );
