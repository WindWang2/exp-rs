/***************************************************************************
 * command_registry.cpp — registration, projection actions, refresh
 ***************************************************************************/
#include "command_registry.h"

#include <QAction>
#include <QIcon>
#include <QtDebug>
#include <QTimer>

namespace sicnu::app
{

CommandRegistry::CommandRegistry( QObject *parent )
    : QObject( parent )
{
}

bool CommandRegistry::registerCommand( CommandDefinition definition )
{
    if ( definition.id.isEmpty() || !definition.handler )
    {
        qWarning() << "CommandRegistry: rejected (id/handler required):" << definition.id;
        return false;
    }
    if ( m_commands.contains( definition.id ) )
    {
        qWarning() << "CommandRegistry: duplicate command id rejected:" << definition.id;
        return false;
    }
    if ( !definition.shortcut.isEmpty() )
    {
        const QString key = definition.shortcut.toString( QKeySequence::PortableText );
        if ( m_shortcuts.contains( key ) )
        {
            qWarning() << "CommandRegistry: duplicate shortcut rejected:" << key
                       << "for" << definition.id;
            return false;
        }
        m_shortcuts.insert( key );
    }
    const QString id = definition.id;
    m_commands.insert( id, std::move( definition ) );
    emit commandRegistered( id );
    return true;
}

const CommandDefinition *CommandRegistry::definition( const QString &id ) const
{
    const auto it = m_commands.constFind( id );
    return it != m_commands.constEnd() ? &it.value() : nullptr;
}

QList<const CommandDefinition *> CommandRegistry::definitions() const
{
    QList<const CommandDefinition *> list;
    list.reserve( m_commands.size() );
    for ( auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it )
        list.append( &it.value() );
    return list;
}

QStringList CommandRegistry::commandIds() const
{
    return m_commands.keys();
}

QAction *CommandRegistry::action( const QString &id, bool installShortcut )
{
    if ( !m_commands.contains( id ) )
        return nullptr;
    if ( m_actions.contains( id ) )
        return m_actions[id];

    const CommandDefinition &def = m_commands[id];
    QAction *act = new QAction( def.title, this );
    act->setObjectName( QStringLiteral( "cmd_%1" ).arg( id ) );
    act->setToolTip( def.description );
    act->setStatusTip( def.description );
    act->setWhatsThis( def.description );
    act->setCheckable( def.checkable );
    if ( !def.iconName.isEmpty() )
        act->setIcon( QIcon( QStringLiteral( ":/icons/" ) + def.iconName ) );
    if ( installShortcut )
    {
        Q_ASSERT_X( m_shortcutOwner.isEmpty(), "CommandRegistry",
                    "only one projection may install the canonical shortcut" );
        act->setShortcut( def.shortcut );
        m_shortcutOwner = id;
    }
    connect( act, &QAction::triggered, this, [this, id] {
        const CommandDefinition *d = definition( id );
        if ( !d )
            return;
        const SelectionContextSnapshot snap = currentSnapshot();
        if ( d->availability && !d->availability( snap ) )
            return; // stale click between refreshes — never run unavailable
        d->handler();
    } );
    m_actions.insert( id, act );
    updateAction( id );
    return act;
}

void CommandRegistry::refreshAll()
{
    for ( auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it )
        updateAction( it.key() );
    emit availabilityChanged();
}

QString CommandRegistry::unavailabilityReason( const QString &id ) const
{
    // Computed on demand so palette / tooltips can query reasons for commands
    // that have no materialized projection action yet.
    const CommandDefinition *d = definition( id );
    if ( !d )
        return QString();
    const SelectionContextSnapshot snap = currentSnapshot();
    if ( !d->availability || d->availability( snap ) )
        return QString();
    return d->explain ? d->explain( snap ) : ContextRules::unavailabilityReason( snap, id );
}

bool CommandRegistry::trigger( const QString &id )
{
    const CommandDefinition *d = definition( id );
    if ( !d )
        return false;
    const SelectionContextSnapshot snap = currentSnapshot();
    if ( d->availability && !d->availability( snap ) )
        return false;
    d->handler();
    return true;
}

void CommandRegistry::setSnapshotProvider( std::function<SelectionContextSnapshot()> provider )
{
    m_snapshotProvider = std::move( provider );
    refreshAll();
}

SelectionContextSnapshot CommandRegistry::currentSnapshot() const
{
    static const SelectionContextSnapshot empty;
    return m_snapshotProvider ? m_snapshotProvider() : empty;
}

void CommandRegistry::updateAction( const QString &id )
{
    QAction *act = m_actions.value( id, nullptr );
    const CommandDefinition *def = definition( id );
    if ( !act || !def )
        return;
    const SelectionContextSnapshot snap = currentSnapshot();
    const bool available = !def->availability || def->availability( snap );
    act->setEnabled( available );
    if ( def->checkable && def->checkedState )
        act->setChecked( def->checkedState( snap ) );
}

} // namespace sicnu::app
