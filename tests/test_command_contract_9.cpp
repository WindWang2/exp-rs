/***************************************************************************
 * test_command_contract_9.cpp
 *
 * Contract Platform 9.0 (M3) — command / help / action reference graph.
 * Permanent guards for the #869 / #881 / #882 drift classes and the #871
 * HelpContentStore regression:
 *
 *   1. the registered command vocabulary is non-empty and idiom-stable;
 *   2. every surface lookup (ribbon/menu/palette/window ->action("…"))
 *      resolves to a registered command (no dangling references);
 *   3. every empty-state/context CTA commandId resolves;
 *   4. every preflight suggested repair action resolves;
 *   5. help ↔ registry bidirectional coverage: no phantom "command.*" help
 *      topics, every registered command has help knowledge (allow-listed);
 *   6. HelpContentStore loads each id exactly once and rejects duplicates
 *      (#871: the double-load regression).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "contracts/command_ref_scanner.h"

#include <help/help_content_store.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using sicnu::contracts::CommandRefScanner;
using sicnu::contracts::CommandRefReport;

namespace {

const char *kSourceDir = CMAKE_SOURCE_DIR;

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( std::istreambuf_iterator<char>( in ),
                        std::istreambuf_iterator<char>() );
}

/// Explicit, reasoned allow-lists. Empty by default — every entry must name
/// the consumer that legitimately references a non-statically-registered id
/// (e.g. plugin-contributed commands registered at runtime).
struct AllowEntry
{
    const char *id;
    const char *reason;
};

const std::vector<AllowEntry> kAllowedDanglingLookups = {
    // { "id", "reason" }
};

const std::vector<AllowEntry> kAllowedDanglingCtas = {
    // { "id", "reason" }
};

/// Live findings of the reference graph (recorded in REVIEW_LOG.md): the
/// preflight emits agent-tool suggestions with dot separators
/// (spatial.understand — the real tool id is spatial:understand) plus a set
/// of action ids that exist in no vocabulary. scientific_preflight.cpp is
/// modified by the open spatial-scientist-harness-9 PR (#885), so the fix
/// belongs there; the entries below are named "OWNED-BY-#885" so the
/// pending drift is impossible to miss in review.
const std::vector<AllowEntry> kAllowedDanglingPreflightActions = {
    { "spatial.understand", "OWNED-BY-#885: tool id separator drift (spatial:understand)" },
    { "temporal.preflight_collection", "OWNED-BY-#885: tool id separator drift (temporal:preflight_collection)" },
    { "harness.plan", "OWNED-BY-#885: tool id separator drift (harness:plan)" },
    { "harness.preflight", "OWNED-BY-#885: no tool/command with this id" },
    { "align_to_reference", "OWNED-BY-#885: no tool/command with this id" },
    { "calibrate_consistently", "OWNED-BY-#885: no tool/command with this id" },
    { "check_dataset", "OWNED-BY-#885: no tool/command with this id" },
    { "check_training", "OWNED-BY-#885: no tool/command with this id" },
    { "inspect_bands", "OWNED-BY-#885: no tool/command with this id" },
    { "normalize_radiometry", "OWNED-BY-#885: no tool/command with this id" },
    { "reproject_to_reference", "OWNED-BY-#885: no tool/command with this id" },
    { "select_matching_polarization", "OWNED-BY-#885: no tool/command with this id" },
};

const std::vector<AllowEntry> kAllowedCommandsWithoutHelp = {
    // { "id", "reason: internal/diagnostic command with no user surface" }
};

const std::vector<AllowEntry> kAllowedPhantomHelpCommands = {
    // { "id", "reason: help documents a command registered dynamically" }
};

bool allowed( const std::vector<AllowEntry> &list, const std::string &id )
{
    return std::any_of( list.begin(), list.end(),
                        [&]( const AllowEntry &e ) { return id == e.id; } );
}

/// All .cpp files under a directory (relative to the source root), sorted.
std::vector<std::string> cppFilesUnder( const std::string &rel )
{
    const std::string root = std::string( kSourceDir ) + "/" + rel;
    std::vector<std::string> out;
    std::error_code ec;
    for ( auto it = std::filesystem::recursive_directory_iterator(
              root, std::filesystem::directory_options::skip_permission_denied,
              ec );
          it != std::filesystem::recursive_directory_iterator();
          it.increment( ec ) )
    {
        if ( ec )
            break;
        if ( it->is_regular_file( ec ) && it->path().extension() == ".cpp" )
            out.push_back( it->path().string() );
    }
    std::sort( out.begin(), out.end() );
    return out;
}

CommandRefReport scanAllRefs()
{
    CommandRefReport report;
    CommandRefScanner scanner;

    // Registration sites (idiom-pinned files).
    scanner.scanRegistered(
        readFile( std::string( kSourceDir ) +
                  "/src/app/workbench/command_defs.cpp" ),
        "src/app/workbench/command_defs.cpp", report );
    scanner.scanRegistered(
        readFile( std::string( kSourceDir ) +
                  "/src/app/main_window_workbench.cpp" ),
        "src/app/main_window_workbench.cpp", report );

    // Consumer surfaces across the app tree.
    for ( const auto &f : cppFilesUnder( "src/app" ) )
    {
        const std::string src = readFile( f );
        scanner.scanLookups( src, f, report );
        scanner.scanCtas( src, f, report );
    }

    // Preflight suggested actions.
    scanner.scanPreflightActions(
        readFile( std::string( kSourceDir ) +
                  "/src/agent/harness/scientific_preflight.cpp" ),
        "src/agent/harness/scientific_preflight.cpp", report );
    return report;
}

/// data/help/commands.json entries: "command.<id>" → <id>.
std::set<std::string> helpCommandIds( QStringList *errors )
{
    std::set<std::string> ids;
    QFile f( QStringLiteral( "%1/data/help/commands.json" )
                 .arg( QStringLiteral( CMAKE_SOURCE_DIR ) ) );
    if ( !f.open( QIODevice::ReadOnly ) )
    {
        if ( errors )
            *errors << QStringLiteral( "cannot open data/help/commands.json" );
        return ids;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( f.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isArray() )
    {
        if ( errors )
            *errors << QStringLiteral( "commands.json: %1" )
                           .arg( parseError.errorString() );
        return ids;
    }
    for ( const auto &e : doc.array() )
    {
        const QString id = e.toObject()[ QStringLiteral( "id" ) ].toString();
        if ( id.startsWith( QStringLiteral( "command." ) ) )
            ids.insert( id.mid( 8 ).toStdString() );
    }
    return ids;
}

} // namespace

TEST_CASE( "Command vocabulary: registration idioms stay recognizable",
           "[contracts9][command]" )
{
    const auto report = scanAllRefs();
    REQUIRE( report.registeredIds.size() >= 50 );
    // Idiom drift sentinels: if these disappear the extraction idiom changed
    // and the guard must be updated consciously, not silently.
    CHECK( report.registeredIds.count( "project.open" ) == 1 );
    CHECK( report.registeredIds.count( "workbench.temporal" ) == 1 );
    CHECK( report.registeredIds.count( "workbench.datasetExperiment" ) == 1 );
    CHECK( report.registeredIds.count( "workbench.model" ) == 1 );
    CHECK( report.registeredIds.count( "workbench.processingHistory" ) == 1 );
}

TEST_CASE( "Every surface lookup resolves to a registered command (#882 class)",
           "[contracts9][command]" )
{
    const auto report = scanAllRefs();
    std::set<std::string> dangling;
    for ( const auto &id : report.lookupIds )
        if ( !report.registeredIds.count( id ) )
            dangling.insert( id );
    for ( const auto &id : dangling )
    {
        INFO( "dangling lookup: " << id << " at "
                                  << report.evidence.at( id ) );
        CHECK( allowed( kAllowedDanglingLookups, id ) );
    }
}

TEST_CASE( "Every empty-state/context CTA resolves to a registered command "
           "(#882 class)",
           "[contracts9][command]" )
{
    const auto report = scanAllRefs();
    REQUIRE( report.ctaCommandIds.size() >= 3 );
    for ( const auto &id : report.ctaCommandIds )
    {
        INFO( "cta: " << id << " at " << report.evidence.at( id ) );
        CHECK( ( report.registeredIds.count( id ) == 1 ||
                 allowed( kAllowedDanglingCtas, id ) ) );
    }
}

TEST_CASE( "Every preflight suggested action resolves (#881 class)",
           "[contracts9][command]" )
{
    const auto report = scanAllRefs();
    REQUIRE( report.preflightActionIds.size() >= 3 );
    std::set<std::string> dangling;
    for ( const auto &id : report.preflightActionIds )
    {
        INFO( "preflight action: " << id << " at " << report.evidence.at( id ) );
        if ( report.registeredIds.count( id ) != 1 )
        {
            dangling.insert( id );
            CHECK( allowed( kAllowedDanglingPreflightActions, id ) );
        }
    }
    // Rot guard: once #885 fixes an action id it resolves, and its
    // allow-list entry must be removed (the entry no longer matches).
    for ( const auto &e : kAllowedDanglingPreflightActions )
    {
        INFO( "stale allow-list entry: " << e.id );
        CHECK( dangling.count( e.id ) == 1 );
    }
}

TEST_CASE( "Help ↔ registry: no phantom and no uncovered commands (#869 class)",
           "[contracts9][command][help]" )
{
    const auto report = scanAllRefs();
    QStringList errors;
    const auto helpIds = helpCommandIds( &errors );
    REQUIRE( errors.isEmpty() );
    REQUIRE( helpIds.size() >= 20 );

    for ( const auto &id : helpIds )
    {
        INFO( "phantom help command: " << id );
        CHECK( ( report.registeredIds.count( id ) == 1 ||
                 allowed( kAllowedPhantomHelpCommands, id ) ) );
    }
    for ( const auto &id : report.registeredIds )
    {
        INFO( "command without help: " << id );
        CHECK( ( helpIds.count( id ) == 1 ||
                 allowed( kAllowedCommandsWithoutHelp, id ) ) );
    }
}

TEST_CASE( "HelpContentStore loads each id exactly once and rejects "
           "duplicates (#871 regression)",
           "[contracts9][command][helpstore]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString base = dir.path();

    // A single file with a single id must load exactly once — the #871 bug
    // double-loaded top-level files and collided with itself.
    REQUIRE( QDir( base ).mkpath( QStringLiteral( "nested/deep" ) ) );
    {
        QFile f( base + QStringLiteral( "/a.json" ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( R"json([{"id":"concept.single_load"}])json" );
    }
    {
        const auto result = sicnu::help::HelpContentStore::loadFromDirectory( base );
        INFO( "errors: "
              << result.errors.join( QStringLiteral( " | " ) ).toStdString() );
        CHECK( result.errors.isEmpty() );
        CHECK( result.descriptors == 1 );
    }

    // Two files claiming the same id: exactly one survivor, one error.
    {
        QFile f( base + QStringLiteral( "/nested/deep/b.json" ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( R"json([{"id":"concept.single_load"}])json" );
    }
    {
        const auto result = sicnu::help::HelpContentStore::loadFromDirectory( base );
        CHECK( result.descriptors == 1 );
        CHECK_FALSE( result.errors.isEmpty() ); // duplicate must be reported
    }
}
