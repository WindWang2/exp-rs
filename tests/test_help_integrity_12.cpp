/***************************************************************************
 * test_help_integrity_12.cpp — Platform 12.0 · Oracle O-11
 *
 * The shipped help/diagnostic corpus must load with ZERO errors and ZERO
 * unknown families. Every producer that emits a diagnostic id must have a
 * resolvable page.
 *
 * WHY THIS EXISTS (evidence-anchored, see EVIDENCE.md E-8):
 *
 *   Two independent producer/consumer pairs were never welded, and nothing
 *   asserted the load error list was empty:
 *
 *   E-8a  data/help/diagnostics.json re-declared two ids in a later append
 *         batch. One copy carried "retry": "retryable", which is NOT in the
 *         accepted vocabulary (none|manual|transient|derived) — the loader
 *         drops the entire page on an unknown value. That copy also
 *         contradicted the earlier copy's retry sense (manual vs retryable)
 *         and referenced a help id that exists nowhere.
 *
 *   E-8b  src/geospatial/doctor/env_doctor.h documents that its findings map
 *         to curated prose ids in data/help/diagnostics.json under
 *         **family "env"**, and env_doctor.cpp emits 10 such ids verbatim.
 *         12 env pages ship (the 10 emitted plus ssl_library_missing and
 *         platform_plugin_missing). But DiagnosticFamily (src/help/help_id.h)
 *         has no Env enumerator and diagnosticFamilyFromName() has no "env"
 *         branch — so EVERY env page is silently dropped and every env-doctor
 *         finding resolves to a blank help lookup.
 *
 *         (An earlier draft of this comment said "11 such ids". The executed
 *          gate reports 12 unknown-family errors, one per authored page, which
 *          is the authoritative count. Corrected 2026-09-20.)
 *
 *         CLOSED 2026-09-22 (this track owns src/help/**): DiagnosticFamily
 *         gained the Env enumerator and diagnosticFamilyFromName() the "env"
 *         branch, so the 12 pages register. The pin below moved from
 *         `unknown == {"env"}` to `unknown.empty()`, and the resolution case
 *         ([e8b]) now asserts the user-visible property directly.
 *
 *   The defect class is the same in both cases: the platform faithfully
 *   records the mismatch, and no gate reads the record. This lane reads it.
 *
 *   E-15  data/help/commands.json stopped parsing entirely (a merge re-spliced
 *         an already-present block, leaving fragments with no '{' opener and no
 *         "id" line). The graph assembler reads the corpus directly
 *         (graph_assembly.cpp:151-158) and, on a parse failure, records
 *         `note: unreadable help file` and skips the WHOLE file — so all 59
 *         command_help edges silently lost their target.
 *
 *         This one is the sharpest instance of the class, because the committed
 *         byte-gated snapshot stayed GREEN throughout: a snapshot diffed only
 *         against itself cannot observe that the corpus it describes has
 *         stopped parsing. Hence the gate below re-reads the SOURCE data and
 *         asserts the property the assembler depends on, rather than routing
 *         through the deliberately-tolerant HelpContentStore.
 *
 * DESIGN NOTES
 *  - It asserts on loadFromDirectory (the SOURCE tree), so a bad edit fails
 *    here before it ever reaches the compiled resource bundle.
 *  - It reports each error verbatim before asserting, so a failure names the
 *    file, the entry and the reason instead of a bare boolean.
 *  - It does NOT skip when content is missing: a missing corpus is a broken
 *    install and must be a hard failure, never a green.
 *  - The `env` family assertion is intentionally split out and tagged, so the
 *    out-of-scope ownership of the fix (src/help/**) is visible in the test
 *    name itself.
 ***************************************************************************/
#include "help/help_content_store.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

std::string sourceRoot()
{
    return std::string( CMAKE_SOURCE_DIR );
}

QString contentDir()
{
    return QString::fromStdString( sourceRoot() + "/data/help" );
}

/// Families the consumer is actually able to resolve. This mirrors
/// DiagnosticFamily in src/help/help_id.h. It is duplicated HERE on purpose:
/// the gate must fail when the producer ships a family the consumer does not
/// know, and reading the enum from the same header would hide exactly that.
///
/// E-8b closed 2026-09-22: "env" was authored by 12 pages and emitted by
/// env_doctor/cli_env_doctor while DiagnosticFamily had no Env enumerator,
/// so every env page was rejected at load. Adding "env" here without adding
/// the enumerator makes the gate red again — the mirror only tracks reality.
const std::set<QString> &consumerFamilies()
{
    static const std::set<QString> kFamilies = {
        QStringLiteral( "harness" ),   QStringLiteral( "operator" ),
        QStringLiteral( "geospatial" ), QStringLiteral( "dataset" ),
        QStringLiteral( "preflight" ), QStringLiteral( "rs" ),
        QStringLiteral( "env" ),
    };
    return kFamilies;
}

/// Every `diagnostic.<family>.<code>` string literal in the in-tree sources.
///
/// The producers carry the id verbatim (src/geospatial/doctor/env_doctor.cpp,
/// src/cli/cli_env_doctor.cpp), so the literal set IS the emitted set. It is
/// extracted here rather than pinned: a new producer that emits an id with no
/// shipped page fails this gate without anyone maintaining a list.
///
/// Not routed through sicnu::contracts::text_scan::findMatches on purpose —
/// that helper skips matches inside string literals, which is precisely where
/// these ids live.
std::set<QString> emittedDiagnosticIds()
{
    static const std::regex kPattern( R"RX("diagnostic\.([a-z0-9]+)\.([a-z0-9_]+)")RX" );

    std::set<QString> ids;
    const std::filesystem::path srcRoot = std::filesystem::path( sourceRoot() ) / "src";
    if ( !std::filesystem::is_directory( srcRoot ) )
        return ids;

    // Non-throwing iteration: an unreadable directory must not turn the whole
    // gate into an uncaught std::filesystem_error.
    std::error_code ec;
    for ( std::filesystem::recursive_directory_iterator it( srcRoot, ec ), end; it != end;
          it.increment( ec ) )
    {
        if ( ec )
            break;
        if ( !it->is_regular_file() )
            continue;
        const auto ext = it->path().extension();
        if ( ext != ".cpp" && ext != ".h" && ext != ".hpp" )
            continue;
        std::ifstream input( it->path() );
        if ( !input )
            continue;
        const std::string text( ( std::istreambuf_iterator<char>( input ) ),
                                std::istreambuf_iterator<char>() );
        for ( std::sregex_iterator m( text.begin(), text.end(), kPattern ), mEnd; m != mEnd; ++m )
        {
            const std::string literal = ( *m )[0].str();
            ids.insert( QString::fromStdString( literal.substr( 1, literal.size() - 2 ) ) );
        }
    }
    return ids;
}

} // namespace

TEST_CASE( "the shipped help corpus exists in the source tree",
           "[helpintegrity12]" )
{
    const QString dir = contentDir();
    INFO( "help dir: " << dir.toStdString() );
    REQUIRE( QFileInfo( dir ).isDir() );

    // The corpus the loader walks: top-level JSON + nested operators/.
    const QDir d( dir );
    const auto topLevel =
        d.entryList( { QStringLiteral( "*.json" ) }, QDir::Files );
    INFO( "top-level json files: " << topLevel.size() );
    CHECK( topLevel.size() >= 4 );

    CHECK( QFileInfo( d.filePath( QStringLiteral( "diagnostics.json" ) ) ).exists() );
    CHECK( QFileInfo( d.filePath( QStringLiteral( "commands.json" ) ) ).exists() );
}

TEST_CASE( "the shipped help corpus loads with ZERO errors",
           "[helpintegrity12][o11]" )
{
    // THE GATE for E-8a. Duplicate ids, unknown retry enums, descriptor/id
    // mismatches and dangling family names all surface here as load errors.
    const auto result = sicnu::help::HelpContentStore::loadFromDirectory( contentDir() );

    const QString joined = result.errors.join( QStringLiteral( "\n  - " ) );
    if ( !result.errors.isEmpty() )
        UNSCOPED_INFO( "load errors (" << result.errors.size() << "):\n  - "
                                       << joined.toStdString() );

    INFO( "descriptors loaded: " << result.descriptors );
    INFO( "aliases loaded: " << result.aliases );

    CHECK( result.errors.isEmpty() );

    // Non-vacuous: a corpus that failed to load at all must not pass.
    CHECK( result.descriptors > 100 );
}

TEST_CASE( "every diagnostic family in the shipped data is one the consumer resolves",
           "[helpintegrity12][o11]" )
{
    // THE GATE for E-8b. Reads the authored data directly and compares the
    // families it declares against the families the consumer can resolve.
    // This is what catches a producer shipping a family the enum lacks.
    const auto result = sicnu::help::HelpContentStore::loadFromDirectory( contentDir() );

    // The loader reports "unknown diagnostic family '<x>'" for each rejection;
    // extract the offending names so the failure is actionable.
    std::set<QString> unknown;
    for ( const QString &error : result.errors )
    {
        const QString marker = QStringLiteral( "unknown diagnostic family '" );
        const int at = error.indexOf( marker );
        if ( at < 0 )
            continue;
        const int from = at + marker.size();
        const int to = error.indexOf( QChar( u'\'' ), from );
        if ( to > from )
            unknown.insert( error.mid( from, to - from ) );
    }

    for ( const QString &family : unknown )
        UNSCOPED_INFO( "family declared in data but unresolvable by the consumer: "
                       << family.toStdString() );

    // E-8b (`env`) is CLOSED: DiagnosticFamily gained an Env enumerator, so
    // no family the corpus ships is unresolvable any more. The pin is kept at
    // "no unknown family" — it was only ever relaxed to {"env"} to keep the
    // lane green while the fix lived in another module's ownership.
    CHECK( unknown.empty() );

    // Every unknown family we tolerate must at least be one the consumer
    // vocabulary genuinely lacks — guard against the list masking a typo.
    for ( const QString &family : unknown )
        CHECK( consumerFamilies().count( family ) == 0 );
}

TEST_CASE( "every diagnostic id the sources emit resolves to a shipped page",
           "[helpintegrity12][o11][e8b]" )
{
    // E-8b closure. The defect was not "a page was missing": 12 env pages
    // shipped and 13 call sites emitted their ids verbatim, and the loader
    // still dropped every one of them because DiagnosticFamily had no Env
    // enumerator. A corpus-shape gate (page count, id uniqueness) cannot see
    // that — only a producer→consumer resolution check can. So this case
    // asserts the property the *user* depends on: an id printed by env-doctor
    // resolves to prose in the registry.
    const auto result = sicnu::help::HelpContentStore::loadFromDirectory( contentDir() );

    const std::set<QString> emitted = emittedDiagnosticIds();
    INFO( "diagnostic ids emitted by src/: " << emitted.size() );

    // Non-vacuity: a scanner that silently stopped finding call sites must
    // not turn this into a green no-op (the E-15 lesson).
    REQUIRE( emitted.size() >= 10 );

    std::set<QString> unresolved;
    for ( const QString &id : emitted )
    {
        const sicnu::help::HelpDescriptor *d = result.registry.find( id );
        if ( !d )
        {
            unresolved.insert( id );
            continue;
        }
        // A resolved id must carry its diagnostic payload: an empty shell
        // page renders as a blank lookup, which is the user-visible symptom.
        if ( !d->diagnostic.has_value() )
            unresolved.insert( id );
    }

    for ( const QString &id : unresolved )
        UNSCOPED_INFO( "emitted diagnostic id does not resolve to a page: "
                       << id.toStdString() );

    CHECK( unresolved.empty() );

    // The family the emitters use must be one the consumer resolves — this is
    // the assertion that fails when DiagnosticFamily loses an enumerator.
    for ( const QString &id : emitted )
    {
        const QString family = id.section( u'.', 1, 1 );
        CHECK( consumerFamilies().count( family ) == 1 );
    }
}

TEST_CASE( "no diagnostic id is registered twice in the shipped data",
           "[helpintegrity12][o11]" )
{
    // A targeted regression pin for E-8a: duplicates are rejected by
    // HelpRegistry::registerDescriptor, so the SECOND copy never registers and
    // its (possibly better) prose is lost while its (possibly wrong) enum
    // still emits an error. Assert on the observable symptom.
    const auto result = sicnu::help::HelpContentStore::loadFromDirectory( contentDir() );

    QStringList duplicates;
    for ( const QString &error : result.errors )
        if ( error.contains( QStringLiteral( "duplicate" ) ) )
            duplicates << error;

    for ( const QString &d : duplicates )
        UNSCOPED_INFO( "duplicate: " << d.toStdString() );

    CHECK( duplicates.isEmpty() );
}

TEST_CASE( "every shipped help file parses as an array of uniquely-identified entries",
           "[helpintegrity12][o11][e15]" )
{
    // THE GATE for E-15. This is the case that did NOT exist, and its absence
    // is why a total loss of the command help surface went unnoticed.
    //
    // E-15: data/help/commands.json stopped parsing (a merge re-spliced an
    // already-present block, leaving fragments with no '{' opener and no
    // "id" line). The graph assembler reads this corpus directly
    // (graph_assembly.cpp:151-158) and, on a parse failure, records
    // `note: unreadable help file` and SKIPS THE WHOLE FILE. Every
    // `command_help` edge then has no target: 59 of them silently vanished.
    //
    // The committed byte-gated snapshot could never catch this, because a
    // snapshot diffed only against itself cannot observe that the corpus it
    // describes has stopped parsing. THIS gate re-reads the source data and
    // asserts the property the assembler depends on.
    //
    // It deliberately does NOT go through HelpContentStore: the loader is
    // tolerant by design, and tolerance is exactly what hid the defect.
    const QDir dir( contentDir() );
    REQUIRE( dir.exists() );

    // Every JSON file in the corpus, including nested operators/.
    QStringList files;
    files << dir.entryList( { QStringLiteral( "*.json" ) }, QDir::Files );
    const QDir nested( dir.filePath( QStringLiteral( "operators" ) ) );
    if ( nested.exists() )
        for ( const QString &f :
              nested.entryList( { QStringLiteral( "*.json" ) }, QDir::Files ) )
            files << QStringLiteral( "operators/" ) + f;

    REQUIRE( files.size() >= 4 );

    QStringList problems;
    int totalEntries = 0;
    int totalCommandEntries = 0;
    QSet<QString> seenIds;

    for ( const QString &rel : files )
    {
        const QString path = dir.filePath( rel );
        QFile f( path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        const QByteArray bytes = f.readAll();

        Json::Value doc;
        Json::CharReaderBuilder builder;
        std::string parseError;
        std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        const bool ok = reader->parse( bytes.constData(),
                                       bytes.constData() + bytes.size(), &doc,
                                       &parseError );

        if ( !ok )
        {
            // Naming the file AND the library's own message is the whole
            // point: this is the error the assembler swallowed.
            problems << QStringLiteral( "%1 does not parse: %2" )
                            .arg( rel, QString::fromStdString( parseError ) );
            continue;
        }
        if ( !doc.isArray() )
        {
            problems << QStringLiteral( "%1 is not a JSON array" ).arg( rel );
            continue;
        }

        for ( const Json::Value &e : doc )
        {
            if ( !e.isObject() )
            {
                problems << QStringLiteral( "%1 holds a non-object entry" )
                                .arg( rel );
                continue;
            }
            const QString id =
                QString::fromStdString( e.get( "id", "" ).asString() );
            if ( id.isEmpty() )
            {
                // A missing id is the *signature* of the E-15 splice: the
                // fragment kept the body but lost its "id" line.
                problems << QStringLiteral( "%1 holds an entry with no id" )
                                .arg( rel );
                continue;
            }

            ++totalEntries;
            if ( id.startsWith( QStringLiteral( "command." ) ) )
                ++totalCommandEntries;

            const QString key = rel + QLatin1Char( '\x1f' ) + id;
            if ( seenIds.contains( key ) )
                problems << QStringLiteral( "%1 declares id '%2' twice" )
                                .arg( rel, id );
            seenIds.insert( key );
        }
    }

    for ( const QString &p : problems )
        UNSCOPED_INFO( "help corpus defect: " << p.toStdString() );

    CHECK( problems.isEmpty() );

    // Non-vacuity: a corpus that silently emptied must not pass. The floors
    // are deliberately below the authored counts (335 topics / 76 commands)
    // so that ordinary content edits do not trip them, while a total loss
    // (the E-15 failure mode) does.
    INFO( "total entries: " << totalEntries );
    INFO( "command.* entries: " << totalCommandEntries );
    CHECK( totalEntries >= 300 );
    CHECK( totalCommandEntries >= 70 );
}
