/***************************************************************************
 * adapters.cpp — workbench adapter implementations
 ***************************************************************************/
#include "adapters.h"

#include <QWidget>

namespace sicnu::app
{

MapWorkbench::MapWorkbench( QWidget *canvasStack, QObject *parent )
    : QObject( parent )
    , m_canvasStack( canvasStack )
{
}

void MapWorkbench::activate()
{
    m_active = true;
    if ( m_canvasStack )
        m_canvasStack->raise();
}

ExternalWindowWorkbench::ExternalWindowWorkbench( const QString &id, const QString &title,
                                                  const QString &iconAlias, Opener opener,
                                                  QObject *parent )
    : QObject( parent )
    , m_id( id )
    , m_title( title )
    , m_iconAlias( iconAlias )
    , m_opener( std::move( opener ) )
{
}

void ExternalWindowWorkbench::activate()
{
    m_active = true;
    if ( m_opener )
        m_opener();
}

bool ExternalWindowWorkbench::requestClose()
{
    if ( m_closeFn )
        return m_closeFn();
    if ( QWidget *window = ( m_windowGetter ? m_windowGetter() : nullptr ) )
        window->close();
    m_active = false;
    return true;
}

} // namespace sicnu::app
