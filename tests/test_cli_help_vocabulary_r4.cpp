// tests/test_cli_help_vocabulary_r4.cpp — Track 14 WP-C: the published
// command vocabulary (docs/headless/README.md) and the implemented CLI
// surface must agree in both directions, at the subcommand level.
//
// Oracle: docs/headless/README.md "## Commands" table. Its second column is
// prose whose backticked spans document subcommands; the FIRST word of every
// backticked span is a documented subcommand (or a flag/argument template for
// flag-driven commands — those are out of scope here and classified N/A
// below, with the reason recorded in the test).
//
// Direction 1 (docs -> CLI): every documented subcommand must be part of the
// CLI's rejection message when the verb is unknown — i.e. the CLI itself can
// enumerate what it accepts (usage line or error_details.expected).
// Direction 2 (CLI -> docs): where the CLI carries a structured vocabulary
// (error_details.expected, the dataset/experiment/reproduce family), the set
// must be EXACTLY the documented set — no undocumented verb, no stale doc.
//
// Source-scanned, not pinned: this test reads the README table at test time
// and spawns the real binary for the CLI side (SICNU_TEST_CLI), so either
// side drifting turns the suite red.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "support/offline_probe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef SICNU_TEST_CLI
#error "SICNU_TEST_CLI must point at sicnu_geo_rs_cli"
#endif

namespace {

std::string readFile( const std::string &path )
{
    std::ifstream input( path );
    if ( !input )
        return {};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

struct RunResult
{
    int exitCode = -1;
    std::string output;
};

RunResult runCli( const std::string &args )
{
    FILE *pipe = ::popen( ( std::string( SICNU_TEST_CLI ) + " " + args + " 2>&1" ).c_str(),
                          "r" );
    REQUIRE( pipe != nullptr );
    char buffer[4096];
    size_t read = 0;
    RunResult result;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), pipe ) ) > 0 )
        result.output.append( buffer, read );
    const int status = ::pclose( pipe );
    result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
    return result;
}

/// command -> documented second-column text of the README command table.
std::map<std::string, std::string> documentedRows()
{
    const std::string docs = readFile( CMAKE_SOURCE_DIR "/docs/headless/README.md" );
    REQUIRE_FALSE( docs.empty() );
    const std::size_t at = docs.find( "## Commands" );
    REQUIRE( at != std::string::npos );
    const std::size_t next = docs.find( "\n## ", at + 12 );
    const std::string section =
        docs.substr( at, next == std::string::npos ? std::string::npos : next - at );

    static const std::regex kRow( R"RX(^\|\s*`([a-z][a-z-]*)`\s*\|\s*(.+?)\|\s*$)RX" );
    std::map<std::string, std::string> rows;
    std::istringstream stream( section );
    std::string line;
    while ( std::getline( stream, line ) )
    {
        std::smatch m;
        if ( std::regex_match( line, m, kRow ) )
            rows[m[1].str()] = m[2].str();
    }
    REQUIRE( rows.size() >= 19 ); // the full surface, incl. session
    return rows;
}

/// First word of every backticked span, keeping only lowercase verb-shaped
/// tokens (drops `<placeholders>`, `--flags`, `[optionals]`).
std::set<std::string> documentedSubcommands( const std::string &cell )
{
    static const std::regex kSpan( R"RX(`([^`]+)`)RX" );
    std::set<std::string> out;
    for ( std::sregex_iterator it( cell.begin(), cell.end(), kSpan ), end; it != end; ++it )
    {
        const std::string span = ( *it )[1].str();
        if ( span.empty() )
            continue;
        if ( span[0] == '-' || span[0] == '<' || span[0] == '[' )
            continue; // flag-driven command or argument template
        static const std::regex kHead( "^([a-z][a-z-]*)" );
        std::smatch m;
        if ( std::regex_search( span, m, kHead ) )
            out.insert( m[1].str() );
    }
    return out;
}

/// The CLI's own statement of its vocabulary: run the given argument string
/// (a certainly-bogus verb, or a bare command whose usage line enumerates the
/// vocabulary) and capture the rejection (usage line and/or
/// error_details.expected in the JSON envelope).
std::string cliVocabularyText( const std::string &args )
{
    const auto result = runCli( args + " --json" );
    REQUIRE( result.exitCode != 0 );
    REQUIRE( result.exitCode != -1 );
    return result.output;
}

Json::Value parseJson( const std::string &text )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( text, root, false );
    return root;
}

std::set<std::string> splitVocabulary( const std::string &expected )
{
    std::set<std::string> out;
    std::string current;
    for ( const char c : expected )
    {
        if ( c == '|' )
        {
            if ( !current.empty() )
                out.insert( current );
            current.clear();
        }
        else
        {
            current.push_back( c );
        }
    }
    if ( !current.empty() )
        out.insert( current );
    return out;
}

} // namespace

SICNU_OFFLINE_GUARD()

TEST_CASE( "the README documents every CLI command", "[cli][vocabulary][r4]" )
{
    const auto rows = documentedRows();
    // The 19 accepted commands, duplicated deliberately: this list is the
    // per-command N/A/coverage ledger for this suite. test_cli_command_surface
    // enforces the command-set equality itself; here every one of the 19 must
    // have a documented row with a non-empty cell.
    for ( const std::string &command :
          { "algorithms", "run", "pipeline", "workflow", "plugin", "models", "project",
            "data", "data-providers", "dataset", "experiment", "reproduce", "lab",
            "env-doctor", "tools", "batch", "session", "passport", "catalog" } )
    {
        INFO( "command: " << command );
        REQUIRE( rows.count( command ) == 1 );
        REQUIRE_FALSE( rows.at( command ).empty() );
    }
}

TEST_CASE( "documented subcommands are enumerated by the CLI itself",
           "[cli][vocabulary][r4]" )
{
    // Direction 1 for every command with subcommand-shaped documentation.
    // Commands whose second column documents FLAGS/arguments instead of
    // subcommands (run, lab, passport), commands without subcommands
    // (data-providers, env-doctor), and the legacy positional-URL grammar
    // (data) are classified here with the reason — nothing is skipped
    // silently.
    struct Probe
    {
        std::string command;
        std::string args; // full argument string probed for the vocabulary
    };
    const std::vector<Probe> probes = {
        { "algorithms", "algorithms __no_such_sub_r4__" },
        { "pipeline", "pipeline __no_such_sub_r4__" },
        { "workflow", "workflow __no_such_sub_r4__" },
        { "plugin", "plugin __no_such_sub_r4__" },
        { "models", "models __no_such_sub_r4__" },
        { "project", "project __no_such_sub_r4__ /nonexistent/sicnu-r4/probe.qgz" },
        { "dataset", "dataset __no_such_sub_r4__" },
        { "experiment", "experiment __no_such_sub_r4__" },
        { "reproduce", "reproduce __no_such_sub_r4__" },
        { "tools", "tools __no_such_sub_r4__" },
        { "batch", "batch __no_such_sub_r4__" },
        { "session", "session __no_such_sub_r4__" },
        { "catalog", "catalog __no_such_sub_r4__" },
        // Positional-URL legacy grammar: a bogus verb would be taken as a
        // URL, so the vocabulary comes from the bare-command usage line.
        { "data", "data" },
        // NOT probed (flag-driven, no subcommand vocabulary): lab, passport,
        // env-doctor, data-providers, run — classified in
        // EXIT_CODE_CONTRACT.md rows 2/9/13/14/16.
    };

    for ( const Probe &probe : probes )
    {
        SECTION( probe.command )
        {
            const auto rows = documentedRows();
            REQUIRE( rows.count( probe.command ) == 1 );
            const std::set<std::string> documented =
                documentedSubcommands( rows.at( probe.command ) );
            if ( documented.empty() )
            {
                WARN( probe.command
                      << ": no subcommand-shaped documentation (flag-driven command)" );
                return;
            }
            const std::string vocabulary = cliVocabularyText( probe.args );
            for ( const std::string &sub : documented )
            {
                INFO( probe.command << " subcommand <" << sub << "> must be enumerated by the CLI:\n"
                                    << vocabulary );
                REQUIRE( vocabulary.find( sub ) != std::string::npos );
            }
        }
    }
}

TEST_CASE( "structured vocabularies match the documentation exactly",
           "[cli][vocabulary][r4]" )
{
    // Direction 2 for the dataset family: error_details.expected is a
    // machine-readable set; it must equal the documented set exactly.
    const std::vector<std::pair<std::string, std::string>> structured = {
        { "dataset", "dataset __no_such_sub_r4__" },
        { "experiment", "experiment __no_such_sub_r4__" },
        { "reproduce", "reproduce __no_such_sub_r4__" },
    };
    for ( const auto &entry : structured )
    {
        SECTION( entry.first )
        {
            const auto result = runCli( entry.second );
            REQUIRE( result.exitCode == 6 );
            const Json::Value envelope = parseJson( result.output );
            REQUIRE( envelope.get( "ok", true ).asBool() == false );
            const Json::Value details = envelope["error_details"];
            REQUIRE( details.isObject() );
            REQUIRE( details.isMember( "expected" ) );
            const std::set<std::string> cliVocabulary =
                splitVocabulary( details["expected"].asString() );
            REQUIRE_FALSE( cliVocabulary.empty() );

            const auto rows = documentedRows();
            REQUIRE( rows.count( entry.first ) == 1 );
            const std::set<std::string> documented =
                documentedSubcommands( rows.at( entry.first ) );

            std::set<std::string> undocumented;
            for ( const std::string &sub : cliVocabulary )
                if ( !documented.count( sub ) )
                    undocumented.insert( sub );
            std::set<std::string> stale;
            for ( const std::string &sub : documented )
                if ( !cliVocabulary.count( sub ) )
                    stale.insert( sub );
            for ( const std::string &sub : undocumented )
                UNSCOPED_INFO( "CLI accepts '" << sub << "' but docs/headless/README.md does not document it" );
            for ( const std::string &sub : stale )
                UNSCOPED_INFO( "docs/headless/README.md documents '" << sub << "' but the CLI does not accept it" );
            REQUIRE( undocumented.empty() );
            REQUIRE( stale.empty() );
        }
    }
}

TEST_CASE( "the session usage line enumerates every accepted action",
           "[cli][vocabulary][r4][session]" )
{
    // session's vocabulary lives in its usage line (run|resume|reconcile|
    // status|timeline|export|pause|cancel|clear-pause|clear-cancel|
    // approve-repair|actions). Every backticked documented action must be
    // accepted-or-mentioned: probing a bogus action must echo the full list.
    const auto rows = documentedRows();
    REQUIRE( rows.count( "session" ) == 1 );
    const std::set<std::string> documented = documentedSubcommands( rows.at( "session" ) );
    REQUIRE_FALSE( documented.empty() );

    const std::string vocabulary = cliVocabularyText( "session __no_such_sub_r4__" );
    for ( const std::string &action : documented )
    {
        INFO( "session action <" << action << "> must appear in the rejection usage line" );
        REQUIRE( vocabulary.find( action ) != std::string::npos );
    }
}
