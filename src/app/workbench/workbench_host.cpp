/***************************************************************************
 * workbench_host.cpp — registry + switcher implementation
 ***************************************************************************/
#include "workbench_host.h"

#include "selection_context.h"

namespace sicnu::app
{

WorkbenchHost::WorkbenchHost( QObject *parent )
    : QObject( parent )
{
}

bool WorkbenchHost::registerWorkbench( IWorkbench *bench )
{
    if ( !bench || bench->id().isEmpty() )
        return false;
    for ( IWorkbench *existing : m_benches )
    {
        if ( existing->id() == bench->id() )
            return false; // duplicate id — contract test guards
    }
    m_benches.append( bench );
    emit workbenchRegistered( bench->id() );
    return true;
}

IWorkbench *WorkbenchHost::workbench( const QString &id ) const
{
    for ( IWorkbench *bench : m_benches )
    {
        if ( bench->id() == id )
            return bench;
    }
    return nullptr;
}

IWorkbench *WorkbenchHost::activeWorkbench() const
{
    return workbench( m_activeId );
}

QString WorkbenchHost::activeWorkbenchId() const
{
    return m_activeId;
}

QStringList WorkbenchHost::workbenchIds() const
{
    QStringList ids;
    ids.reserve( m_benches.size() );
    for ( IWorkbench *bench : m_benches )
        ids.append( bench->id() );
    return ids;
}

bool WorkbenchHost::activate( const QString &id )
{
    if ( id == m_activeId )
        return true;
    IWorkbench *next = workbench( id );
    if ( !next )
        return false;

    const QString previousId = m_activeId;
    if ( IWorkbench *current = activeWorkbench() )
        current->deactivate();

    m_activeId = id;
    next->activate();
    emit activeWorkbenchChanged( id, previousId );
    return true;
}

} // namespace sicnu::app
