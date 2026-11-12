/***************************************************************************
 * error_diagnostics_bridge.cpp — curated-code detection in failure text
 ***************************************************************************/
#include "help/error_diagnostics_bridge.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace sicnu::help
{
namespace
{

/// family name → { originCode → helpId } over curated diagnostic pages only.
QHash<QString, QHash<QString, QString>> buildCuratedCodes( const HelpRegistry &registry )
{
    QHash<QString, QHash<QString, QString>> built;
    // byKind(Diagnostic) rather than all(): the table covers curated
    // diagnostic pages only, so the descriptor pointer vector the registry
    // allocates and sorts carries just that slice (163 of the corpus) instead
    // of every command/operator/concept page.
    for ( const HelpDescriptor *d : registry.byKind( HelpKind::Diagnostic ) ) {
        if ( !d->diagnostic.has_value() )
            continue;
        built[d->diagnostic->originFamily][d->diagnostic->originCode] = d->id;
    }
    return built;
}

/// The failure path constructs a bridge per lookup, so reuse the derived
/// table until a successful registry mutation changes its revision. Unlike
/// descriptor counts, the revision also detects upserts and a new registry
/// constructed at the same address with the same number of descriptors.
struct CuratedCodeTable
{
    const HelpRegistry *registry = nullptr;
    quint64 revision = 0;
    int descriptors = -1;
    int aliases = -1;
    QHash<QString, QHash<QString, QString>> byFamily;
    /// byFamily keys, sorted: QHash iteration order is unspecified, so a code
    /// string that ever appears in two families would resolve differently
    /// across processes. Sorted keys make the first hit stable; today's
    /// curated codes are globally unique, this guards the future.
    QStringList families;
};

const CuratedCodeTable &curatedCodeTable( const HelpRegistry &registry )
{
    // thread_local: the lookups stay lock-free, and each thread that asks
    // pays the build at most once per registry.
    static thread_local CuratedCodeTable memo;
    if ( memo.registry != &registry || memo.revision != registry.revision()
         || memo.descriptors != registry.count() || memo.aliases != registry.aliasCount() ) {
        memo.registry = &registry;
        memo.revision = registry.revision();
        // A default move transfers the maps but leaves scalar revisions in
        // the source. Counts also invalidate lookups against that source.
        memo.descriptors = registry.count();
        memo.aliases = registry.aliasCount();
        memo.byFamily = buildCuratedCodes( registry );
        memo.families = memo.byFamily.keys();
        std::sort( memo.families.begin(), memo.families.end() );
    }
    return memo;
}

/// Token-shaped code candidates: UPPER_SNAKE (harness/preflight codes),
/// CamelCase words (operator codes like FileNotFound) and single capitalized
/// words (operator codes like "Cancelled"/"Unknown" — RSOperatorError::what()
/// is "CodeName: message", and job_engine copies what() verbatim into the
/// task error text this bridge scans). Longest first so a specific long code
/// wins over a shorter one embedded in it. Prose safety: a candidate only
/// resolves when it EXACTLY equals a curated code, so ordinary capitalized
/// words that match no code stay inert ("no guessing" per F20).
QStringList codeCandidates( const QString &message )
{
    static const QRegularExpression re(
        QStringLiteral( "\\b[A-Z][A-Z0-9_]{2,}\\b|\\b[A-Z][a-z]+(?:[A-Z][a-z]+){1,}\\b"
                        "|\\b[A-Z][a-z]{2,}\\b" ) );
    // QSet dedup: the single-capitalized-word branch makes candidate counts
    // O(distinct capitalized words) and a QStringList contains() scan is
    // quadratic on hostile prose (review R1 P2).
    QSet<QString> seen;
    QStringList tokens;
    auto it = re.globalMatch( message );
    while ( it.hasNext() ) {
        const QString token = it.next().captured( 0 );
        if ( !seen.contains( token ) ) {
            seen.insert( token );
            tokens << token;
        }
    }
    std::stable_sort( tokens.begin(), tokens.end(),
                      []( const QString &a, const QString &b ) { return a.size() > b.size(); } );
    return tokens;
}

} // namespace

QString ErrorDiagnosticsBridge::helpIdForCode( DiagnosticFamily family, const QString &code ) const
{
    const CuratedCodeTable &table = curatedCodeTable( m_registry );
    const auto familyIt = table.byFamily.constFind( diagnosticFamilyName( family ) );
    if ( familyIt == table.byFamily.constEnd() )
        return QString();
    const auto codeIt = familyIt->constFind( code );
    return codeIt == familyIt->constEnd() ? QString() : codeIt.value();
}

QString ErrorDiagnosticsBridge::helpIdFromMessage( const QString &message ) const
{
    const CuratedCodeTable &table = curatedCodeTable( m_registry );
    for ( const QString &token : codeCandidates( message ) ) {
        // table.families is name-sorted so the family order never depends on
        // QHash iteration order (see CuratedCodeTable).
        for ( const QString &family : table.families ) {
            const auto familyIt = table.byFamily.constFind( family );
            const auto codeIt = familyIt->constFind( token );
            if ( codeIt != familyIt->constEnd() )
                return codeIt.value();
        }
    }
    return QString();
}

} // namespace sicnu::help
