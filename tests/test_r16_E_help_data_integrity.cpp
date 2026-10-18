/***************************************************************************
 * tests/test_r16_E_help_data_integrity.cpp — R16-E T09 WP1
 *
 * Cross-checks the authored help corpus (data/help/** minus the two files
 * owned by open PR #1517, which stay read-only) against the id universe it
 * is composed into:
 *
 *   (a) descriptor ids are globally unique (no duplicate keys across files);
 *   (b) every cross-reference target exists: related[], diagnostics[],
 *       supersededBy, guidance.actions (shell command ids), guidance.helpTopic
 *       and algorithm.relatedTools (rs:* operator ids). Operator/ command
 *       descriptors are DERIVED at composition time, so the universe here is
 *       data/help ids ∪ glossary term ids ∪ the rs:* operator census scanned
 *       from src/operators (same discipline as test_help_integrity_12's
 *       emittedDiagnosticIds: extract, don't pin);
 *   (c) every reachable entry carries a renderable title+summary (kinds that
 *       derive them from live registries — command.* — are exempt; parameters
 *       get loader defaults);
 *   (d) diagnostics.json's family/code pairs agree with the derived mapping
 *       in DiagnosticCatalog (HelpId::diagnosticId) AND the curated-code
 *       bridge table in ErrorDiagnosticsBridge.
 *
 * RED on master: (1) curated diagnostic descriptors load with an EMPTY
 * summary (DiagnosticCatalog::fallback() fills one; the JSON loader did not),
 * which blanks HelpPresenter::tooltip/statusTip and the agent-facing compact
 * summary for every curated diagnostic page. Fixed in this track by
 * mirroring the fallback default in HelpContentStore::parseEntry.
 * (2) the bridge's code-candidate pattern required 2+ CamelCase humps, so
 * single-word operator codes ("Cancelled", "Unknown") — which
 * RSOperatorError::what() embeds as "Cancelled: message" and job_engine
 * copies verbatim into the task error text scanned by withDiagnosticLink —
 * could never link their curated pages. Fixed in this track by widening the
 * candidate pattern (exact table lookup keeps prose words inert).
 ***************************************************************************/
#include "help/diagnostic_catalog.h"
#include "help/error_diagnostics_bridge.h"
#include "help/help_content_store.h"
#include "help/help_descriptor.h"
#include "help/help_id.h"
#include "help/terminology_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

namespace
{

QString sourceRoot()
{
    return QString::fromUtf8( CMAKE_SOURCE_DIR );
}

QString contentDir()
{
    return sourceRoot() + QStringLiteral( "/data/help" );
}

/// The corpus the audit walks: data/help/**.json minus #1517's read-only pair.
/// (The loader itself reads them too — that is fine, this is a read-only audit
/// and the ownership constraint is about WRITING those files.)
QStringList corpusFiles()
{
    QStringList files;
    QDirIterator it( contentDir(), QStringList{ QStringLiteral( "*.json" ) }, QDir::Files,
                     QDirIterator::Subdirectories );
    while ( it.hasNext() )
        files << it.next();
    files.sort();
    return files;
}

/// rs:* operator names as registered in src/operators — the census the
/// composition-time OperatorHelpProvider derives operator.* descriptors from.
/// Extracted by source scan (REGISTER_RS_OPERATOR macros + registry add(...)
/// calls), mirroring test_help_integrity_12's emittedDiagnosticIds approach:
/// a new operator with no help data fails here without anyone maintaining a
/// hand-copied list.
std::set<QString> rsOperatorCensus()
{
    static const std::regex kMacro( R"RX(REGISTER_RS_OPERATOR\(\s*\w+,\s*"(rs:[a-z0-9_]+)")RX" );
    static const std::regex kAdd( R"RX(\badd\(\s*"(rs:[a-z0-9_]+)")RX" );

    std::set<QString> names;
    const std::filesystem::path root = std::filesystem::path( CMAKE_SOURCE_DIR ) / "src" / "operators";
    std::error_code ec;
    for ( std::filesystem::recursive_directory_iterator it( root, ec ), end; it != end;
          it.increment( ec ) )
    {
        if ( ec )
            break;
        if ( !it->is_regular_file() )
            continue;
        const auto ext = it->path().extension();
        if ( ext != ".cpp" && ext != ".h" )
            continue;
        std::ifstream input( it->path() );
        if ( !input )
            continue;
        const std::string text( ( std::istreambuf_iterator<char>( input ) ),
                                std::istreambuf_iterator<char>() );
        for ( const std::regex *pattern : { &kMacro, &kAdd } )
            for ( std::sregex_iterator m( text.begin(), text.end(), *pattern ), mEnd; m != mEnd; ++m )
                names.insert( QString::fromStdString( ( *m )[1].str() ) );
    }
    return names;
}

const sicnu::help::HelpContentStore::LoadResult &loadedCorpus()
{
    static const sicnu::help::HelpContentStore::LoadResult result =
        sicnu::help::HelpContentStore::loadFromDirectory( contentDir() );
    return result;
}

/// The full resolve universe: authored ids (+ nested parameter ids, registered
/// by the loader) + glossary term ids + derived operator ids from the census.
QSet<QString> resolveUniverse()
{
    QSet<QString> ids;
    for ( const sicnu::help::HelpDescriptor *d : loadedCorpus().registry.all() )
        ids.insert( d->id );
    for ( const auto &alias : loadedCorpus().registry.aliases() )
        ids.insert( alias.first );

    const sicnu::help::TerminologyProvider::LoadResult glossary =
        sicnu::help::TerminologyProvider::loadFromFile(
            sourceRoot() + QStringLiteral( "/data/terms/rs_glossary.json" ) );
    QSet<QString> used;
    for ( const sicnu::help::TermEntry &term : glossary.terms )
    {
        QString id = sicnu::help::termDescriptorId( term.en );
        const QString base = id;
        int n = 2;
        while ( used.contains( id ) )
            id = QStringLiteral( "%1_%2" ).arg( base ).arg( n++ );
        used.insert( id );
        ids.insert( id );
    }

    for ( const QString &name : rsOperatorCensus() )
        ids.insert( QStringLiteral( "operator.%1" )
                        .arg( sicnu::help::HelpId::domainForOperatorId( name ) ) );
    return ids;
}

} // namespace

// (a) — global id uniqueness, asserted on the raw JSON (the tolerant loader
// reports duplicates as errors, but the E-15 lesson says gates re-read the
// data rather than trusting the loader's tolerance).
TEST_CASE( "help corpus ids are globally unique across all files",
           "[r16e][help][wp1][uniqueness]" )
{
    const QStringList files = corpusFiles();
    REQUIRE( files.size() >= 9 );

    QSet<QString> seen;
    QStringList duplicates;
    int entries = 0;
    for ( const QString &path : files )
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        const QString rel = QDir( sourceRoot() ).relativeFilePath( path );

        // lightweight brace-object scan: every top-level object's "id" line
        // (the corpus is one array of flat objects; nested objects belong to
        // parameters/algorithm payloads and never carry "id" at line start)
        const QStringList lines = QString::fromUtf8( f.readAll() ).split( u'\n' );
        for ( const QString &line : lines )
        {
            const QString trimmed = line.trimmed();
            if ( !trimmed.startsWith( QStringLiteral( "\"id\":" ) ) )
                continue;
            const int from = trimmed.indexOf( u'"', 5 ) + 1;
            const int to = trimmed.lastIndexOf( u'"' );
            if ( from <= 0 || to <= from )
                continue;
            const QString id = trimmed.mid( from, to - from );
            ++entries;
            if ( seen.contains( id ) )
                duplicates << QStringLiteral( "%1: %2" ).arg( rel, id );
            seen.insert( id );
        }
    }
    INFO( "entries scanned: " << entries );
    for ( const QString &d : duplicates )
        UNSCOPED_INFO( "duplicate id: " << d.toStdString() );
    CHECK( duplicates.isEmpty() );
    CHECK( entries >= 300 ); // non-vacuous: a silently emptied corpus fails
}

// (b) — no dangling cross-references in the composed universe.
TEST_CASE( "help corpus cross-references resolve in the composed universe",
           "[r16e][help][wp1][references]" )
{
    const QSet<QString> universe = resolveUniverse();
    INFO( "universe size: " << universe.size() );
    REQUIRE( universe.size() >= 600 );

    const std::set<QString> census = rsOperatorCensus();
    REQUIRE( census.size() >= 100 ); // scanner still sees the operator table

    QStringList dangling;
    for ( const sicnu::help::HelpDescriptor *d : loadedCorpus().registry.all() )
    {
        for ( const QString &ref : d->relatedIds )
            if ( !universe.contains( ref ) )
                dangling << QStringLiteral( "%1 -> related %2" ).arg( d->id, ref );
        for ( const QString &ref : d->diagnosticIds )
            if ( !universe.contains( ref ) )
                dangling << QStringLiteral( "%1 -> diagnostics %2" ).arg( d->id, ref );
        if ( d->deprecated && !d->supersededBy.isEmpty() && !universe.contains( d->supersededBy ) )
            dangling << QStringLiteral( "%1 -> supersededBy %2" ).arg( d->id, d->supersededBy );

        if ( d->guidance.has_value() )
        {
            for ( const QString &action : d->guidance->actionCommandIds )
            {
                const QString target = QStringLiteral( "command.%1" ).arg( action );
                if ( !universe.contains( target ) )
                    dangling << QStringLiteral( "%1 -> guidance.action %2" ).arg( d->id, action );
            }
            const QString topic = d->guidance->helpTopicId;
            if ( !topic.isEmpty() && !universe.contains( topic ) )
                dangling << QStringLiteral( "%1 -> guidance.helpTopic %2" ).arg( d->id, topic );
        }
        if ( d->algorithm.has_value() )
        {
            for ( const QString &tool : d->algorithm->relatedTools )
            {
                if ( tool.startsWith( QStringLiteral( "rs:" ) ) )
                {
                    if ( census.count( tool ) == 0 )
                        dangling << QStringLiteral( "%1 -> relatedTools %2 (not registered)" )
                                        .arg( d->id, tool );
                }
                else if ( !universe.contains( tool ) )
                {
                    dangling << QStringLiteral( "%1 -> relatedTools %2" ).arg( d->id, tool );
                }
            }
        }
    }
    std::sort( dangling.begin(), dangling.end() );
    for ( const QString &d : dangling )
        UNSCOPED_INFO( "dangling: " << d.toStdString() );
    CHECK( dangling.isEmpty() );
}

// (c) — renderable title/summary floor. This is the RED case on master:
// curated diagnostics had an empty summary while DiagnosticCatalog::fallback()
// fills one — tooltip/statusTip/compact summaries degraded to blank.
TEST_CASE( "every reachable help entry carries a non-empty title and summary",
           "[r16e][help][wp1][floor]" )
{
    QStringList empty;
    for ( const sicnu::help::HelpDescriptor *d : loadedCorpus().registry.all() )
    {
        // command.* titles/summaries derive from the live CommandRegistry at
        // composition (commands.json only carries additive knowledge).
        if ( d->kind == sicnu::help::HelpKind::Command )
            continue;
        if ( d->title.isEmpty() )
            empty << QStringLiteral( "%1: empty title" ).arg( d->id );
        if ( d->summary.isEmpty() )
            empty << QStringLiteral( "%1: empty summary" ).arg( d->id );
    }
    std::sort( empty.begin(), empty.end() );
    INFO( "offenders: " << empty.size() );
    for ( int i = 0; i < qMin( empty.size(), 15 ); ++i )
        UNSCOPED_INFO( empty.at( i ).toStdString() );
    if ( empty.size() > 15 )
        UNSCOPED_INFO( "... and " << ( empty.size() - 15 ) << " more" );
    CHECK( empty.isEmpty() );
}

// (d) — diagnostics.json code <-> catalog/bridge mapping agreement.
TEST_CASE( "diagnostics json codes agree with the catalog and bridge mapping",
           "[r16e][help][wp1][diagnostics]" )
{
    using namespace sicnu::help;
    const auto &result = loadedCorpus();
    REQUIRE( result.errors.isEmpty() );

    const DiagnosticCatalog catalog( result.registry );
    const ErrorDiagnosticsBridge bridge( result.registry );

    int checked = 0;
    QStringList problems;
    for ( const HelpDescriptor *d : result.registry.byKind( HelpKind::Diagnostic ) )
    {
        if ( !d->diagnostic.has_value() )
            continue;
        const DiagnosticInfo &info = *d->diagnostic;
        const auto family = diagnosticFamilyFromName( info.originFamily );
        if ( !family.has_value() )
        {
            problems << QStringLiteral( "%1: unknown family %2" ).arg( d->id, info.originFamily );
            continue;
        }
        ++checked;

        // derived-id agreement (no hand-crafted exceptions — the loader also
        // enforces this, the bridge depends on it)
        if ( catalog.idFor( *family, info.originCode ) != d->id )
            problems << QStringLiteral( "%1: idFor(%2,%3) disagrees" )
                            .arg( d->id, info.originFamily, info.originCode );

        // bridge: the curated-code table must map (family, code) back to the page
        if ( bridge.helpIdForCode( *family, info.originCode ) != d->id )
            problems << QStringLiteral( "%1: bridge.helpIdForCode disagrees" )
                            .arg( d->id, info.originFamily, info.originCode );

        // bridge message scan: codes whose SHAPE can appear in failure prose
        // (identifier-shaped, starts uppercase: UPPER_SNAKE, CamelCase, or a
        // single capitalized word like "Cancelled"/"Unknown" —
        // RSOperatorError::what() embeds them and job_engine copies what()
        // into the task error text) must be recoverable from that prose.
        // Lowercase/dotted codes (dataset label.*, geospatial snake_case,
        // preflight, dotted rs:*) are STRUCTURED-only by design — verified at
        // their emit sites (dataset findings carry code + human prose
        // separately; GeoError::what() is prose-only; preflight codes drive
        // proposals structurally; the dotted rs codes have no prose emitter),
        // so prose recoverability is not asserted for them.
        const QString message = QStringLiteral( "operator failed: %1 in stage two" ).arg( info.originCode );
        const bool proseShaped = !info.originCode.isEmpty() && info.originCode[0].isUpper()
                                 && QRegularExpression(
                                        QStringLiteral( "^[A-Z][A-Za-z0-9_]+$" ) )
                                        .match( info.originCode )
                                        .hasMatch();
        if ( proseShaped && bridge.helpIdFromMessage( message ).isEmpty() )
            problems << QStringLiteral( "%1: code %2 not recoverable from failure text" )
                            .arg( d->id, info.originCode );
    }
    INFO( "diagnostic pages checked: " << checked );
    REQUIRE( checked >= 100 ); // non-vacuous
    for ( const QString &p : problems )
        UNSCOPED_INFO( "mapping problem: " << p.toStdString() );
    CHECK( problems.isEmpty() );
}

// docs[] refs must exist in the repository (they are clickable in the Help
// Center); a moved/deleted doc page silently breaks the link otherwise.
TEST_CASE( "help corpus doc references point at existing files",
           "[r16e][help][wp1][docs]" )
{
    const QDir root( sourceRoot() );
    QStringList missing;
    int docRefs = 0;
    for ( const sicnu::help::HelpDescriptor *d : loadedCorpus().registry.all() )
    {
        for ( const QString &ref : d->docRefs )
        {
            ++docRefs;
            const QString cleaned = ref.startsWith( u'/' ) ? ref.mid( 1 ) : ref;
            if ( !QFileInfo::exists( root.filePath( cleaned ) ) )
                missing << QStringLiteral( "%1 -> %2" ).arg( d->id, ref );
        }
    }
    INFO( "doc refs checked: " << docRefs );
    for ( const QString &m : missing )
        UNSCOPED_INFO( "missing doc ref: " << m.toStdString() );
    CHECK( missing.isEmpty() );
}
