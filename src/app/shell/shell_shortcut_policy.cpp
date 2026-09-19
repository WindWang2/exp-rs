/***************************************************************************
 * shell_shortcut_policy.cpp — see header for the contract
 ***************************************************************************/
#include "shell_shortcut_policy.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

namespace sicnu::app
{

ShellShortcutPolicy::ShellShortcutPolicy( QObject *parent )
    : QObject( parent )
{
}

bool ShellShortcutPolicy::isTextInputWidget( const QWidget *widget )
{
    // Walk up the focus chain: a combo-box popup hands focus to its internal
    // QAbstractItemView, whose ancestor IS the QComboBox that owns the
    // keyboard search. The same covers spin-box/editor container children.
    for ( const QWidget *w = widget; w; w = w->parentWidget() )
    {
        if ( const auto *line = qobject_cast<const QLineEdit *>( w ) )
            return !line->isReadOnly();
        if ( const auto *text = qobject_cast<const QTextEdit *>( w ) )
            return !text->isReadOnly();
        if ( const auto *plain = qobject_cast<const QPlainTextEdit *>( w ) )
            return !plain->isReadOnly();
        if ( const auto *spin = qobject_cast<const QAbstractSpinBox *>( w ) )
            return !spin->isReadOnly();
        // Both editable and non-editable combos consume printable keys: the
        // editable one types into its line edit, the plain one does keyboard
        // search (Qt::Key_H jumps to the next "H..." item).
        if ( const auto *combo = qobject_cast<const QComboBox *>( w ) )
            return combo->isEditable() || combo->count() > 0;
    }
    return false;
}

bool ShellShortcutPolicy::eventFilter( QObject *watched, QEvent *event )
{
    if ( event->type() != QEvent::ShortcutOverride )
        return QObject::eventFilter( watched, event );

    auto *keyEvent = static_cast<QKeyEvent *>( event );
    // Modified chords keep their action binding: Ctrl+H is an explicit
    // command, never "text the user tried to type".
    if ( keyEvent->modifiers() & ( Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier ) )
        return QObject::eventFilter( watched, event );
    // Only printable input is contested; Tab/Return/Escape/arrows and other
    // non-printing keys keep their normal shortcut/focus-traversal handlers.
    const QString text = keyEvent->text();
    if ( text.isEmpty() || !text.at( 0 ).isPrint() )
        return QObject::eventFilter( watched, event );
    if ( !isTextInputWidget( qobject_cast<QWidget *>( watched ) ) )
        return QObject::eventFilter( watched, event );

    // Claim the key for the focused editor: Qt then dispatches the key as a
    // regular KeyPress instead of running the window-hosted letter shortcut.
    keyEvent->accept();
    return true;
}

} // namespace sicnu::app
