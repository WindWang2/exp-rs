/***************************************************************************
 * error_diagnostics_bridge.cpp — curated-code detection in failure text
 ***************************************************************************/
#include "help/error_diagnostics_bridge.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace sicnu::help
{
namespace
{

/// family name → { originCode → helpId } over curated diagnostic pages only.
QHash<QString, QHash<QString, QString>> curatedCodes( const HelpRegistry &registry )
{
    QHash<QString, QHash<QString, QString>> built;
    for ( const HelpDescriptor *d : registry.all() ) {
        if ( d->kind != HelpKind::Diagnostic || !d->diagnostic.has_value() )
            continue;
        built[d->diagnostic->originFamily][d->diagnostic->originCode] = d->id;
    }
    return built;
}

/// Token-shaped code candidates: UPPER_SNAKE (harness/preflight codes) and
/// CamelCase words (operator codes like FileNotFound). Longest first so a
/// specific long code wins over a shorter one embedded in it.
QStringList codeCandidates( const QString &message )
{
    static const QRegularExpression re(
        QStringLiteral( "\\b[A-Z][A-Z0-9_]{2,}\\b|\\b[A-Z][a-z]+(?:[A-Z][a-z]+){1,}\\b" ) );
    QStringList tokens;
    auto it = re.globalMatch( message );
    while ( it.hasNext() ) {
        const QString token = it.next().captured( 0 );
        if ( !tokens.contains( token ) )
            tokens << token;
    }
    std::stable_sort( tokens.begin(), tokens.end(),
                      []( const QString &a, const QString &b ) { return a.size() > b.size(); } );
    return tokens;
}

} // namespace

ErrorDiagnosticsBridge::ErrorDiagnosticsBridge( const HelpRegistry &registry )
    : m_registry( registry )
    , m_catalog( registry )
{
}

QString ErrorDiagnosticsBridge::helpIdForCode( DiagnosticFamily family, const QString &code ) const
{
    const QHash<QString, QHash<QString, QString>> table = curatedCodes( m_registry );
    const auto familyIt = table.constFind( diagnosticFamilyName( family ) );
    if ( familyIt == table.constEnd() )
        return QString();
    const auto codeIt = familyIt->constFind( code );
    return codeIt == familyIt->constEnd() ? QString() : codeIt.value();
}

QString ErrorDiagnosticsBridge::helpIdFromMessage( const QString &message ) const
{
    const QHash<QString, QHash<QString, QString>> table = curatedCodes( m_registry );
    for ( const QString &token : codeCandidates( message ) ) {
        for ( auto familyIt = table.constBegin(); familyIt != table.constEnd(); ++familyIt ) {
            const auto codeIt = familyIt->constFind( token );
            if ( codeIt != familyIt->constEnd() )
                return codeIt.value();
        }
    }
    return QString();
}

} // namespace sicnu::help
