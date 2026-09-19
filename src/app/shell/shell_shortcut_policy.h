/***************************************************************************
 * shell_shortcut_policy.h — window-hosted letter shortcut policy
 *
 * The shell re-hosts every shortcut-bearing action on the window itself so a
 * hidden menubar cannot kill the binding (UI review C1). That includes
 * unmodified letter keys such as `H` (map.pan). Qt only forwards a key to the
 * focused widget instead of the shortcut when the widget accepts a
 * ShortcutOverride event, and the plain text editors (QLineEdit / QTextEdit /
 * QPlainTextEdit / spin boxes) accept ONLY the common clipboard/cursor
 * shortcuts — typing "H" in the Copilot dock would otherwise pan the map
 * (issue #1037 F-1031-P1-letterkey).
 *
 * This policy closes that hole without giving up the map bindings: while a
 * text-input widget has focus, printable keys are claimed for the widget;
 * Ctrl/Alt/Meta chords and every non-editable focus keep the window-hosted
 * action path unchanged.
 ***************************************************************************/
#pragma once

#include <QObject>

class QWidget;

namespace sicnu::app
{

class ShellShortcutPolicy : public QObject
{
  public:
    explicit ShellShortcutPolicy( QObject *parent = nullptr );

    /// True when @p widget currently accepts free-text keyboard input
    /// (read-only editors release the key back to the shortcut host).
    static bool isTextInputWidget( const QWidget *widget );

  protected:
    bool eventFilter( QObject *watched, QEvent *event ) override;
};

} // namespace sicnu::app
