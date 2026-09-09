/***************************************************************************
 * help_event_filter.cpp — F1 context resolution implementation
 ***************************************************************************/
#include "app/help/help_event_filter.h"

#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

namespace sicnu::app
{

HelpEventFilter::HelpEventFilter( QObject *parent )
    : QObject( parent )
{
}

void HelpEventFilter::registerObjectName( const QString &objectName, const QString &helpId )
{
    if ( !objectName.isEmpty() && !helpId.isEmpty() )
        m_objectNameMap.insert( objectName, helpId );
}

void HelpEventFilter::setWorkbenchContext( const QString &workbenchHelpId )
{
    m_workbenchHelpId = workbenchHelpId;
}

bool HelpEventFilter::eventFilter( QObject *watched, QEvent *event )
{
    // Only bare F1 claims the help context: Shift+F1 stays reserved for
    // What's This, Ctrl+F1 for application use, and auto-repeat must not
    // spam the Help Center.
    if ( event->type() == QEvent::ShortcutOverride ) {
        // claim bare F1 globally so QAction shortcuts (QKeySequence::HelpContents)
        // do not swallow it before context resolution runs
        if ( auto *keyEvent = static_cast<QKeyEvent *>( event );
             keyEvent->key() == Qt::Key_F1 && keyEvent->modifiers() == Qt::NoModifier
             && !keyEvent->isAutoRepeat() ) {
            keyEvent->accept();
            return true;
        }
        return QObject::eventFilter( watched, event );
    }
    if ( event->type() == QEvent::KeyPress ) {
        if ( auto *keyEvent = static_cast<QKeyEvent *>( event );
             keyEvent->key() == Qt::Key_F1 && keyEvent->modifiers() == Qt::NoModifier
             && !keyEvent->isAutoRepeat() ) {
            const QWidget *widget = qobject_cast<const QWidget *>( watched );
            const QString helpId = resolveHelpId( widget );
            emit helpRequested( helpId );
            return true;
        }
    }
    return QObject::eventFilter( watched, event );
}

QString HelpEventFilter::resolveHelpId( const QWidget *widget ) const
{
    // 1. explicit helpId dynamic property up the parent chain
    for ( const QWidget *w = widget; w; w = w->parentWidget() ) {
        const QVariant property = w->property( "helpId" );
        if ( property.isValid() && !property.toString().isEmpty() )
            return property.toString();
    }
    // 2. objectName map up the parent chain
    for ( const QWidget *w = widget; w; w = w->parentWidget() ) {
        const auto it = m_objectNameMap.constFind( w->objectName() );
        if ( it != m_objectNameMap.constEnd() )
            return it.value();
    }
    // 3. active workbench context
    return m_workbenchHelpId;
}

} // namespace sicnu::app
