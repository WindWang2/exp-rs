/***************************************************************************
 * test_cli_command_surface.cpp — CLI 3.0 public command-surface integrity
 *
 * `sicnu_geo_rs_cli <command> …` is a public scripting/agent surface, and its
 * command set lives in TWO hand-maintained places inside one file:
 *
 *   - `isCliCommand()`  — `kCommands`, the names main() accepts;
 *   - `dispatchCliCommand()` — an `if (command == "…")` chain that runs them.
 *
 * Nothing ties them together and nothing enumerated either one. A name added
 * to `kCommands` without a dispatch branch produces a command that is
 * *accepted* and then falls through to `ExitCode::InvalidInput` — a silently
 * broken verb that looks exactly like a user typo (exit 6, `ok:false`). This
 * lane makes that class unauthorable:
 *
 *   C-1  every accepted name is dispatched (no ghost commands);
 *   C-2  every dispatched branch is accepted (no dead branches);
 *   C-3  docs/headless/README.md — the discoverability surface for humans and
 *        agents, and the only place the command set is published — lists
 *        exactly the accepted names. Neither a missing row (undocumented
 *        command) nor a stale row (documented command that no longer exists)
 *        is allowed.
 *
 * It reads the SOURCE tree with the same "scan, don't pin" discipline as the
 * contract scanners: the command list is never duplicated here, so adding a
 * verb cannot be forgotten in a third place.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string repoRoot() { return std::string( CMAKE_SOURCE_DIR ); }

std::string readFile( const std::string &path )
{
    std::ifstream input( path );
    if ( !input )
        return {};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

const std::string &cliCommandsSource()
{
    static const std::string text = readFile( repoRoot() + "/src/cli/cli_commands.cpp" );
    return text;
}

/// Sub-string of @p text from @p marker to the first line that is exactly "}".
std::string functionBody( const std::string &text, const std::string &marker )
{
    const std::size_t at = text.find( marker );
    if ( at == std::string::npos )
        return {};
    const std::size_t close = text.find( "\n}", at );
    return text.substr( at, close == std::string::npos ? std::string::npos : close - at );
}

/// The `kCommands` initializer inside isCliCommand(): the ACCEPTED set.
std::set<std::string> acceptedCommands()
{
    const std::string body = functionBody( cliCommandsSource(), "bool isCliCommand(" );
    REQUIRE_FALSE( body.empty() );

    // From "kCommands" to the terminating '}' of the initializer.
    const std::size_t at = body.find( "kCommands" );
    REQUIRE( at != std::string::npos );
    const std::size_t open = body.find( '{', at );
    const std::size_t close = body.find( '}', open );
    REQUIRE( open != std::string::npos );
    REQUIRE( close != std::string::npos );

    static const std::regex kQuoted( R"RX("([a-z][a-z-]*)")RX" );
    std::set<std::string> out;
    const std::string list = body.substr( open + 1, close - open - 1 );
    for ( std::sregex_iterator it( list.begin(), list.end(), kQuoted ), end; it != end; ++it )
        out.insert( ( *it )[1].str() );
    return out;
}

/// The body of dispatchCliCommand() — the DISPATCHED side.
const std::string &dispatchBody()
{
    static const std::string body =
        functionBody( cliCommandsSource(), "int dispatchCliCommand(" );
    return body;
}

/// The `command == "x"` branches of dispatchCliCommand(): the DISPATCHED set.
///
/// Strictly the branch form, not a plain `"x"` substring search: several verbs
/// ("catalog" is the live example) also spell their name inside an envelope or
/// usage literal, so a substring test stays green even after the branch is
/// deleted — the exact ghost this gate exists to catch.
std::set<std::string> dispatchedCommands()
{
    const std::string &body = dispatchBody();
    REQUIRE_FALSE( body.empty() );

    static const std::regex kBranch( R"RX(command\s*==\s*"([a-z][a-z-]*)")RX" );
    std::set<std::string> out;
    for ( std::sregex_iterator it( body.begin(), body.end(), kBranch ), end; it != end; ++it )
        out.insert( ( *it )[1].str() );
    return out;
}

/// Command names documented in docs/headless/README.md's command table.
std::set<std::string> documentedCommands()
{
    const std::string docs = readFile( repoRoot() + "/docs/headless/README.md" );
    REQUIRE_FALSE( docs.empty() );

    // Scope the scan to the `## Commands` section: the flag and exit-code
    // tables below it are also backticked, and a future row like "| `dry-run` |"
    // would otherwise be reported as a stale command.
    const std::size_t at = docs.find( "## Commands" );
    REQUIRE( at != std::string::npos );
    const std::size_t next = docs.find( "\n## ", at + 12 );
    const std::string section =
        docs.substr( at, next == std::string::npos ? std::string::npos : next - at );

    // Table rows start with "| `name` |".
    static const std::regex kRow( R"RX(\|\s*`([a-z][a-z-]*)`\s*\|)RX" );
    std::set<std::string> out;
    for ( std::sregex_iterator it( section.begin(), section.end(), kRow ), end; it != end; ++it )
        out.insert( ( *it )[1].str() );
    return out;
}

} // namespace

TEST_CASE( "every accepted CLI command is dispatched", "[cli][surface][commands]" )
{
    const std::set<std::string> accepted = acceptedCommands();

    // Non-vacuity, and an anchor on the parser rather than only on a count:
    // if `kCommands` stops being found the set collapses and the gates below
    // would otherwise pass on two empty sets.
    INFO( "accepted commands: " << accepted.size() );
    REQUIRE( accepted.size() >= 18 );
    REQUIRE( accepted.count( "algorithms" ) );
    REQUIRE( accepted.count( "passport" ) );

    const std::set<std::string> dispatched = dispatchedCommands();
    REQUIRE( dispatched.size() >= 18 );

    std::set<std::string> ghosts;
    for ( const std::string &name : accepted )
        if ( !dispatched.count( name ) )
            ghosts.insert( name );

    for ( const std::string &name : ghosts )
        UNSCOPED_INFO( "accepted by isCliCommand() but never dispatched: " << name
                       << " — the command exits InvalidInput with no envelope" );

    CHECK( ghosts.empty() );
}

TEST_CASE( "every dispatched CLI command branch is accepted", "[cli][surface][commands]" )
{
    const std::set<std::string> accepted = acceptedCommands();
    const std::set<std::string> dispatched = dispatchedCommands();
    REQUIRE( dispatched.size() >= 18 );

    std::set<std::string> dead;
    for ( const std::string &name : dispatched )
        if ( !accepted.count( name ) )
            dead.insert( name );

    for ( const std::string &name : dead )
        UNSCOPED_INFO( "dispatch branch for a command isCliCommand() rejects: " << name
                       << " — unreachable from main()" );

    CHECK( dead.empty() );

    // The two sides must be the same set, not merely non-empty: a parser that
    // stopped matching would silently shrink both to zero and agree.
    CHECK( accepted == dispatched );
}

TEST_CASE( "the documented CLI command table matches the implemented surface",
           "[cli][surface][commands][docs]" )
{
    const std::set<std::string> accepted = acceptedCommands();
    const std::set<std::string> documented = documentedCommands();

    INFO( "documented rows: " << documented.size() );
    REQUIRE( documented.size() >= 18 );

    std::set<std::string> undocumented;
    std::set<std::string> stale;
    for ( const std::string &name : accepted )
        if ( !documented.count( name ) )
            undocumented.insert( name );
    for ( const std::string &name : documented )
        if ( !accepted.count( name ) )
            stale.insert( name );

    for ( const std::string &name : undocumented )
        UNSCOPED_INFO( "shipped command missing from docs/headless/README.md: " << name );
    for ( const std::string &name : stale )
        UNSCOPED_INFO( "docs/headless/README.md documents a command that is not shipped: "
                       << name );

    CHECK( undocumented.empty() );
    CHECK( stale.empty() );
}
