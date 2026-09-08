/***************************************************************************
 * interactive_session.cpp — shared session contract base (moc anchor)
 *
 * The Q_OBJECT base needs a translation unit so AUTOMOC emits
 * moc_interactive_session.cpp into every target that links the adapters.
 ***************************************************************************/
#include "interactive_session.h"

namespace sicnu::app
{

InteractiveSession::InteractiveSession( QObject *parent )
    : QObject( parent )
{
}

} // namespace sicnu::app
