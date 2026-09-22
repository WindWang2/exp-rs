/***************************************************************************
 * test_sdk_public_surface.cpp — ExpRS public SDK install-surface integrity
 *
 * The installed SDK (component `sdk`, produced by src/sdk/CMakeLists.txt) is
 * a *package*, not a directory listing: a third party runs
 * `find_package(ExpRS)` and includes <exprs/...>. Two properties make that
 * package usable, and nothing asserted either of them:
 *
 *   S-1  CLOSURE. The installed header set must be closed under
 *        `#include "exprs/..."` — otherwise a consumer that includes one
 *        shipped header gets a fatal "exprs/plugin_host_runtime.h: No such
 *        file or directory" from a header the package itself handed them.
 *        Found state (2026-09-22): exprs/plugin_registry.h is installed and
 *        includes exprs/plugin_host_runtime.h, which was NOT installed —
 *        the whole public registry surface was unusable out of tree.
 *
 *   S-2  VERSION PARITY. The plugin API version is declared in four places
 *        that must agree (exprs/version.h, the CMake package version, the
 *        Python SDK, docs/sdk/README.md). A bump that misses one produces a
 *        package that negotiates a plausible but wrong contract: the loader
 *        accepts plugins whose MINOR <= the host's, so a stale authority
 *        silently changes who may load.
 *
 * Both gates read the SOURCE tree (never a build artifact), so they fail at
 * the moment the drift is authored rather than after an install.
 *
 * Deliberately NOT asserted: which headers *should* be public. That is a
 * design decision owned by src/sdk/CMakeLists.txt; this lane only proves the
 * published set is self-consistent, closed and version-coherent.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

/// The `EXPRS_SDK_INCLUDE_HEADERS` list in src/sdk/CMakeLists.txt, as bare
/// file names ("plugin_registry.h"). Parsed, not pinned: the list is the
/// authority being checked.
std::set<std::string> installedHeaders()
{
    const std::string cmake = readFile( repoRoot() + "/src/sdk/CMakeLists.txt" );
    // The list is the first `set(EXPRS_SDK_INCLUDE_HEADERS ...)` block.
    // Locate the `set( EXPRS_SDK_INCLUDE_HEADERS ... )` command itself: the
    // name sits AFTER the '(', so searching forward for '(' from the name
    // lands in whatever comes next (a comment, the operator-contract set) and
    // yields an empty block — a gate that cannot fail. Anchor on `set(`, then
    // bracket-count to the matching ')' so parenthesised comments inside the
    // list cannot truncate it either.
    static const std::regex kSet( R"RX(set\s*\(\s*EXPRS_SDK_INCLUDE_HEADERS\b)RX" );
    std::smatch found;
    REQUIRE( std::regex_search( cmake, found, kSet ) );
    const std::size_t open = cmake.find( '(', found.position( 0 ) );
    REQUIRE( open != std::string::npos );

    std::size_t depth = 0;
    std::size_t close = std::string::npos;
    for ( std::size_t i = open; i < cmake.size(); ++i )
    {
        if ( cmake[i] == '(' )
            ++depth;
        else if ( cmake[i] == ')' && --depth == 0 )
        {
            close = i;
            break;
        }
    }
    REQUIRE( close != std::string::npos );

    static const std::regex kEntry( R"RX(exprs/([A-Za-z0-9_]+\.h))RX" );
    std::set<std::string> out;
    const std::string block = cmake.substr( open + 1, close - open - 1 );
    for ( std::sregex_iterator it( block.begin(), block.end(), kEntry ), end; it != end; ++it )
        out.insert( ( *it )[1].str() );

    // Non-vacuity for the parser itself: version.h is the first entry of the
    // list, so an empty parse cannot masquerade as a pass.
    REQUIRE( out.count( "version.h" ) );
    return out;
}

/// `#include "exprs/xxx.h"` targets of one header.
std::set<std::string> localIncludesOf( const std::string &headerName )
{
    const std::string text = readFile( repoRoot() + "/src/sdk/exprs/" + headerName );
    static const std::regex kInclude( R"RX(#include\s+"exprs/([A-Za-z0-9_]+\.h)")RX" );
    std::set<std::string> out;
    for ( std::sregex_iterator it( text.begin(), text.end(), kInclude ), end; it != end; ++it )
        out.insert( ( *it )[1].str() );
    return out;
}

/// Transitive closure of the installed set under `#include "exprs/..."`.
std::set<std::string> includeClosure( const std::set<std::string> &roots )
{
    std::set<std::string> seen;
    std::vector<std::string> stack( roots.begin(), roots.end() );
    while ( !stack.empty() )
    {
        const std::string name = stack.back();
        stack.pop_back();
        if ( !seen.insert( name ).second )
            continue;
        for ( const std::string &dep : localIncludesOf( name ) )
            if ( !seen.count( dep ) )
                stack.push_back( dep );
    }
    return seen;
}

/// Headers intentionally NOT installed: host-side/internal, unreachable from
/// every installed header. Pinned so a new exprs/*.h must be classified —
/// shipped or declared internal — instead of silently landing in neither.
const std::set<std::string> &declaredInternalHeaders()
{
    static const std::set<std::string> kInternal = {
        "msvc_posix_shim.h",  // MSVC/POSIX portability shim, no public types
        "plugin_index.h",     // host-side local/offline plugin index
        "plugin_ui_schema.h", // host-side projection of declarative plugin UI
    };
    return kInternal;
}

} // namespace

TEST_CASE( "the installed SDK header set is closed under #include",
           "[sdk][public-surface][install]" )
{
    const std::set<std::string> installed = installedHeaders();
    REQUIRE( installed.size() >= 20 ); // non-vacuity: the list parsed

    // Every shipped header must exist on disk — a rename leaves a stale entry
    // that installs nothing.
    for ( const std::string &name : installed )
    {
        INFO( "installed header: " << name );
        REQUIRE( std::filesystem::exists( repoRoot() + "/src/sdk/exprs/" + name ) );
    }

    const std::set<std::string> closure = includeClosure( installed );

    std::set<std::string> missing;
    for ( const std::string &name : closure )
        if ( !installed.count( name ) )
            missing.insert( name );

    for ( const std::string &name : missing )
        UNSCOPED_INFO( "header reachable from an installed header but not installed: exprs/"
                       << name << " — a consumer including the shipped header fails to compile" );

    CHECK( missing.empty() );

    // Non-vacuity: a closure that collapsed to a couple of entries means the
    // include parser stopped working (the E-15 lesson — a gate that cannot
    // fail is worse than no gate).
    CHECK( closure.size() >= 20 );
}

TEST_CASE( "every exprs header is either installed or declared host-internal",
           "[sdk][public-surface][install]" )
{
    const std::set<std::string> installed = installedHeaders();

    std::set<std::string> unclassified;
    for ( const auto &entry :
          std::filesystem::directory_iterator( repoRoot() + "/src/sdk/exprs" ) )
    {
        if ( !entry.is_regular_file() || entry.path().extension() != ".h" )
            continue;
        const std::string name = entry.path().filename().string();
        if ( installed.count( name ) || declaredInternalHeaders().count( name ) )
            continue;
        unclassified.insert( name );
    }

    for ( const std::string &name : unclassified )
        UNSCOPED_INFO( "exprs/" << name
                                << " is neither installed nor listed as host-internal" );

    CHECK( unclassified.empty() );

    // An internal header must stay internal: if an installed header ever
    // reaches one, the closure gate above already fails, so here we only
    // guard the pin itself against bit-rot.
    CHECK_FALSE( declaredInternalHeaders().empty() );
    for ( const std::string &name : declaredInternalHeaders() )
        REQUIRE( std::filesystem::exists( repoRoot() + "/src/sdk/exprs/" + name ) );
}

TEST_CASE( "the plugin API version is declared identically everywhere",
           "[sdk][public-surface][version]" )
{
    // Four authorities. A MINOR bump that misses one leaves the package
    // negotiating a contract the loader no longer means.
    const std::string versionHeader = readFile( repoRoot() + "/src/sdk/exprs/version.h" );
    const std::string sdkCMake = readFile( repoRoot() + "/src/sdk/CMakeLists.txt" );
    const std::string pythonInit = readFile( repoRoot() + "/src/python/sdk/exprs/__init__.py" );
    const std::string sdkDocs = readFile( repoRoot() + "/docs/sdk/README.md" );
    REQUIRE_FALSE( versionHeader.empty() );
    REQUIRE_FALSE( sdkCMake.empty() );
    REQUIRE_FALSE( pythonInit.empty() );
    REQUIRE_FALSE( sdkDocs.empty() );

    auto firstMatch = []( const std::string &text, const std::string &pattern ) {
        std::regex re( pattern );
        std::smatch m;
        return std::regex_search( text, m, re ) ? m[1].str() : std::string{};
    };

    // "MAJOR.MINOR" plugin API version.
    const std::string fromHeaderApi = [&] {
        const std::string major =
            firstMatch( versionHeader, R"RX(EXP_RS_PLUGIN_API_VERSION_MAJOR\s+(\d+))RX" );
        const std::string minor =
            firstMatch( versionHeader, R"RX(EXP_RS_PLUGIN_API_VERSION_MINOR\s+(\d+))RX" );
        REQUIRE_FALSE( major.empty() );
        REQUIRE_FALSE( minor.empty() );
        return major + "." + minor;
    }();
    const std::string fromCMakeApi =
        firstMatch( sdkCMake, R"RX(EXP_RS_PLUGIN_API_VERSION\s+"([0-9]+\.[0-9]+)")RX" );
    const std::string fromPythonApi =
        firstMatch( pythonInit, R"RX(PLUGIN_API_VERSION\s*=\s*"([0-9]+\.[0-9]+)")RX" );
    const std::string fromDocsApi =
        firstMatch( sdkDocs, R"RX(find_package\(ExpRS\s+([0-9]+\.[0-9]+))RX" );

    REQUIRE_FALSE( fromCMakeApi.empty() );
    REQUIRE_FALSE( fromPythonApi.empty() );
    REQUIRE_FALSE( fromDocsApi.empty() );

    CHECK( fromCMakeApi == fromHeaderApi );
    CHECK( fromPythonApi == fromHeaderApi );
    CHECK( fromDocsApi == fromHeaderApi );

    // Semver of the SDK package itself.
    const std::string fromHeaderSemver =
        firstMatch( versionHeader, R"RX(EXP_RS_SDK_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)")RX" );
    const std::string fromCMakeSemver =
        firstMatch( sdkCMake, R"RX(EXP_RS_PACKAGE_VERSION\s+([0-9]+\.[0-9]+\.[0-9]+))RX" );
    const std::string fromPythonSemver =
        firstMatch( pythonInit, R"RX(__version__\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)")RX" );
    REQUIRE_FALSE( fromHeaderSemver.empty() );
    REQUIRE_FALSE( fromCMakeSemver.empty() );
    REQUIRE_FALSE( fromPythonSemver.empty() );

    CHECK( fromCMakeSemver == fromHeaderSemver );
    CHECK( fromPythonSemver == fromHeaderSemver );

    // The exported CMake variable is what a plugin manifest is generated
    // from, so it must be the same string the C++ header compiles to.
    const std::string exportedApi =
        firstMatch( sdkCMake, R"RX(set\(\s*EXP_RS_PLUGIN_API_VERSION\s+"([^"]+)"\s*\))RX" );
    CHECK( exportedApi == fromHeaderApi );
}
