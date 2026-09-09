/***************************************************************************
 * help_registry.cpp — registry implementation
 ***************************************************************************/
#include "help/help_registry.h"

#include <algorithm>

namespace sicnu::help
{
namespace
{

/// Sanity binding between declared kind and id-implied kind.
bool kindMatchesId( const HelpDescriptor &d )
{
    const std::optional<HelpKind> implied = HelpId::kindOf( d.id );
    return implied.has_value() && *implied == d.kind;
}

} // namespace

bool HelpRegistry::registerDescriptor( HelpDescriptor descriptor, QString *error )
{
    auto fail = [error]( const QString &reason ) {
        if ( error )
            *error = reason;
        return false;
    };

    if ( !HelpId::isValid( descriptor.id ) )
        return fail( QStringLiteral( "invalid help id: %1" ).arg( descriptor.id ) );
    if ( !kindMatchesId( descriptor ) )
        return fail( QStringLiteral( "kind mismatch for id: %1" ).arg( descriptor.id ) );
    if ( m_descriptors.contains( descriptor.id ) )
        return fail( QStringLiteral( "duplicate help id: %1" ).arg( descriptor.id ) );
    if ( descriptor.deprecated && descriptor.supersededBy.isEmpty() )
        return fail( QStringLiteral( "deprecated descriptor without supersededBy: %1" ).arg( descriptor.id ) );

    if ( descriptor.id.startsWith( QLatin1String( "diagnostic." ) ) && !descriptor.diagnostic.has_value() )
        return fail( QStringLiteral( "diagnostic descriptor without DiagnosticInfo: %1" ).arg( descriptor.id ) );

    m_descriptors.insert( descriptor.id, std::move( descriptor ) );
    if ( error )
        error->clear();
    return true;
}

bool HelpRegistry::upsertDescriptor( HelpDescriptor descriptor, QString *error )
{
    // Same validation as registerDescriptor, minus the duplicate rejection
    // (providers upsert derived descriptors over embedded knowledge).
    auto fail = [error]( const QString &reason ) {
        if ( error )
            *error = reason;
        return false;
    };
    if ( !HelpId::isValid( descriptor.id ) )
        return fail( QStringLiteral( "invalid help id: %1" ).arg( descriptor.id ) );
    if ( !kindMatchesId( descriptor ) )
        return fail( QStringLiteral( "kind mismatch for id: %1" ).arg( descriptor.id ) );
    if ( descriptor.deprecated && descriptor.supersededBy.isEmpty() )
        return fail( QStringLiteral( "deprecated descriptor without supersededBy: %1" ).arg( descriptor.id ) );
    if ( descriptor.id.startsWith( QLatin1String( "diagnostic." ) ) && !descriptor.diagnostic.has_value() )
        return fail( QStringLiteral( "diagnostic descriptor without DiagnosticInfo: %1" ).arg( descriptor.id ) );
    m_descriptors.insert( descriptor.id, std::move( descriptor ) );
    if ( error )
        error->clear();
    return true;
}

bool HelpRegistry::registerAlias( const QString &aliasId, const QString &targetId, QString *error )
{
    auto fail = [error]( const QString &reason ) {
        if ( error )
            *error = reason;
        return false;
    };

    if ( !HelpId::isValid( aliasId ) )
        return fail( QStringLiteral( "invalid alias id: %1" ).arg( aliasId ) );
    if ( !HelpId::isValid( targetId ) )
        return fail( QStringLiteral( "invalid alias target id: %1" ).arg( targetId ) );
    if ( m_descriptors.contains( aliasId ) )
        return fail( QStringLiteral( "alias collides with descriptor: %1" ).arg( aliasId ) );
    if ( m_aliases.contains( aliasId ) )
        return fail( QStringLiteral( "duplicate alias: %1" ).arg( aliasId ) );

    m_aliases.insert( aliasId, targetId );
    if ( error )
        error->clear();
    return true;
}

const HelpDescriptor *HelpRegistry::find( const QString &id ) const
{
    if ( const auto it = m_descriptors.constFind( id ); it != m_descriptors.constEnd() )
        return &it.value();
    // one alias hop only — chains are rejected by validateReferences()
    if ( const auto alias = m_aliases.constFind( id ); alias != m_aliases.constEnd() ) {
        const auto target = m_descriptors.constFind( alias.value() );
        if ( target != m_descriptors.constEnd() )
            return &target.value();
    }
    return nullptr;
}

QVector<const HelpDescriptor *> HelpRegistry::all() const
{
    QVector<const HelpDescriptor *> out;
    out.reserve( m_descriptors.size() );
    for ( const HelpDescriptor &d : m_descriptors )
        out.push_back( &d );
    std::sort( out.begin(), out.end(), []( const HelpDescriptor *a, const HelpDescriptor *b ) {
        return a->id < b->id;
    } );
    return out;
}

QVector<const HelpDescriptor *> HelpRegistry::byKind( HelpKind kind ) const
{
    QVector<const HelpDescriptor *> out;
    for ( const HelpDescriptor &d : m_descriptors ) {
        if ( d.kind == kind )
            out.push_back( &d );
    }
    std::sort( out.begin(), out.end(), []( const HelpDescriptor *a, const HelpDescriptor *b ) {
        return a->id < b->id;
    } );
    return out;
}

QList<QPair<QString, QString>> HelpRegistry::aliases() const
{
    QList<QPair<QString, QString>> out;
    out.reserve( m_aliases.size() );
    for ( auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it )
        out.append( { it.key(), it.value() } );
    std::sort( out.begin(), out.end(), []( const auto &a, const auto &b ) { return a.first < b.first; } );
    return out;
}

QStringList HelpRegistry::validateReferences() const
{
    QStringList problems;
    const auto checkTarget = [&]( const QString &from, const QString &ref ) {
        if ( m_descriptors.contains( ref ) )
            return;
        if ( m_aliases.contains( ref ) )
            return;
        problems << QStringLiteral( "%1: reference '%2' not registered" ).arg( from, ref );
    };

    for ( const HelpDescriptor &d : m_descriptors ) {
        for ( const QString &related : d.relatedIds )
            checkTarget( d.id, related );
        for ( const QString &diag : d.diagnosticIds )
            checkTarget( d.id, diag );
        if ( !d.supersededBy.isEmpty() && d.deprecated )
            checkTarget( d.id, d.supersededBy );
    }
    for ( auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it )
        checkTarget( it.key(), it.value() );
    problems.sort();
    return problems;
}

void HelpRegistry::mergeFrom( const HelpRegistry &other, QStringList *errors )
{
    for ( const auto &entry : other.m_descriptors ) {
        QString error;
        if ( !registerDescriptor( entry, &error ) && errors )
            errors->append( error );
    }
    for ( auto it = other.m_aliases.constBegin(); it != other.m_aliases.constEnd(); ++it ) {
        QString error;
        if ( !registerAlias( it.key(), it.value(), &error ) && errors )
            errors->append( error );
    }
}

HelpRegistry &globalHelpRegistry()
{
    static HelpRegistry registry;
    return registry;
}

} // namespace sicnu::help
