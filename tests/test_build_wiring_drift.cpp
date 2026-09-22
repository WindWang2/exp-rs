// tests/test_build_wiring_drift.cpp
//
// Structure oracle for build/test wiring drift (hardening track
// integration-build-contract-drift). Master accumulated merged modules whose
// CMakeLists.txt was never reached by any add_subdirectory (src/verify,
// src/agent_loop, src/preflight, src/repair_planner, src/study/bridge), one
// merged module without any CMakeLists.txt at all (src/recipes) and merged
// test sources never registered in tests/CMakeLists.txt (test_verifier_schema,
// test_recipe_*, ...). Configure and the default `all` build stayed deceptively
// green through all of it (phantom link targets only break the linker), so
// this gate reads the wiring text mechanically and fails listing offenders:
//
//   1. every src/**/CMakeLists.txt must be reachable from the root
//      CMakeLists.txt through add_subdirectory edges (textual
//      over-approximation: conditionally added directories count as reachable,
//      which is the direction that keeps honest modules green; edges with
//      unresolved variables or absolute paths cannot be checked statically and
//      are skipped);
//   2. every src/**/*.cpp must be named — as a "*.cpp" token — by a build
//      script that either lives in the file's module scope (the nearest
//      ancestor directory carrying a CMakeLists.txt, up to src/) or outside
//      src/ entirely (tests/tools compiling a source directly). A same-named
//      file in an unrelated sibling module cannot mask a delisting (src/lab's
//      session_state.cpp cannot cover src/agent_loop's);
//   3. every tests/test_*.cpp (recursive) must be registered in
//      tests/CMakeLists.txt, i.e. its stem must appear as a whole word.
//
// Known text-level limitations, all fail-open: add_subdirectory operands with
// unresolved variables or absolute paths cannot be checked statically, and
// CMake bracket comments #[[...]] (none exist in this repository) survive the
// line-based comment stripper. Any new module, source file or test that lands
// without wiring turns this test red with the path listed.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string slurp( const fs::path &path )
{
    std::ifstream in( path );
    return { std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
}

// CMake comments are line-based: drop '#'..end-of-line so commented-out
// add_subdirectory calls, source lists and test registrations stop counting
// as wiring (a file listed only inside a comment is not wired). A '#' inside
// a quoted argument is over-stripped in theory; no CMake script in this
// repository quotes '#' in a path.
std::string strip_cmake_comments( const std::string &text )
{
    std::string out;
    out.reserve( text.size() );
    bool in_comment = false;
    for ( const char c : text )
    {
        if ( c == '\n' )
        {
            in_comment = false;
            out.push_back( c );
        }
        else if ( !in_comment )
        {
            if ( c == '#' )
                in_comment = true;
            else
                out.push_back( c );
        }
    }
    return out;
}

bool is_word_char( char c )
{
    return isalnum( static_cast<unsigned char>( c ) ) != 0 || c == '_';
}

bool contains_word( const std::string &haystack, const std::string &word )
{
    if ( word.empty() )
        return true;
    for ( std::size_t pos = haystack.find( word ); pos != std::string::npos;
          pos = haystack.find( word, pos + 1 ) )
    {
        const std::size_t end = pos + word.size();
        const bool left_ok = pos == 0 || !is_word_char( haystack[pos - 1] );
        const bool right_ok = end >= haystack.size() || !is_word_char( haystack[end] );
        if ( left_ok && right_ok )
            return true;
    }
    return false;
}

// True when the canonical directory `child` equals `dir` or lives below it.
bool is_under( const fs::path &child, const fs::path &dir )
{
    auto c = child.begin();
    auto d = dir.begin();
    for ( ; d != dir.end(); ++d, ++c )
        if ( c == child.end() || *c != *d )
            return false;
    return true;
}

// Walk the repository without descending into build outputs / VCS internals,
// so the gate is hermetic (a stale build-dev cannot keep a delisted source
// "listed") and does not slurp megabytes of generated CMake fragments.
bool is_excluded_dir( const std::string &name )
{
    return name == ".git" || name == "CMakeFiles" || name == "Testing" ||
           name == "build" || name.rfind( "build-", 0 ) == 0; // build, build-dev, ...
}

std::vector<fs::path> collect_build_scripts( const fs::path &root )
{
    std::vector<fs::path> scripts;
    for ( auto it = fs::recursive_directory_iterator( root ), end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( it->is_directory() && is_excluded_dir( it->path().filename().string() ) )
        {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path &p = it->path();
        if ( it->is_regular_file() &&
             ( p.filename() == "CMakeLists.txt" || p.extension() == ".cmake" ) )
            scripts.push_back( p );
    }
    std::sort( scripts.begin(), scripts.end() );
    return scripts;
}

// add_subdirectory(x) — target names as written in this repository. Quoted
// forms and ${VAR} segments are tolerated by the match so a future
// variable-composed edge degrades to "skipped", not to a false red.
const std::regex &subdir_regex()
{
    static const std::regex re( R"(add_subdirectory\s*\(\s*\"?([A-Za-z0-9_./$}{-]+))" );
    return re;
}

// Best-effort static resolution of an add_subdirectory operand: literal text
// passes through; anything still containing '$' (unresolved variable) or an
// absolute path cannot be checked and returns nullopt.
std::optional<fs::path> resolve_subdir_operand( const std::string &operand, const fs::path &from_dir )
{
    if ( operand.find( '$' ) != std::string::npos || operand.empty() )
        return std::nullopt;
    fs::path ref = operand;
    if ( ref.is_absolute() )
        return std::nullopt;
    return fs::weakly_canonical( from_dir / ref );
}

} // namespace

TEST_CASE( "build wiring drift gate", "[build_wiring][contract][drift]" )
{
    const fs::path root = fs::weakly_canonical( CMAKE_SOURCE_DIR );
    const fs::path src_root = fs::weakly_canonical( root / "src" );
    REQUIRE( fs::exists( root / "CMakeLists.txt" ) );

    // 1. Reachability of every src/**/CMakeLists.txt from the root file.
    std::set<fs::path> reachable;
    std::vector<fs::path> queue{ root };
    reachable.insert( root );
    while ( !queue.empty() )
    {
        const fs::path dir = queue.back();
        queue.pop_back();
        const fs::path cm = dir / "CMakeLists.txt";
        if ( !fs::exists( cm ) )
            continue;
        const std::string text = strip_cmake_comments( slurp( cm ) );
        for ( std::sregex_iterator it( text.begin(), text.end(), subdir_regex() ), end; it != end;
              ++it )
        {
            const auto resolved = resolve_subdir_operand( ( *it )[1].str(), dir );
            if ( !resolved )
                continue;
            if ( reachable.insert( *resolved ).second )
                queue.push_back( *resolved );
        }
    }

    std::vector<fs::path> unreachable;
    for ( auto it = fs::recursive_directory_iterator( src_root ),
               end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( it->is_regular_file() && it->path().filename() == "CMakeLists.txt" )
        {
            const fs::path dir = fs::weakly_canonical( it->path().parent_path() );
            if ( !reachable.count( dir ) )
                unreachable.push_back( it->path() );
        }
    }

    // 2. Every src/**/*.cpp named by an eligible build script. Bare-basename
    // listings only count inside the file's module scope (or outside src/);
    // path-qualified listings (${CMAKE_SOURCE_DIR}/src/... or relative
    // subpaths) count anywhere, since a path cannot collide by basename.
    const std::regex cpp_token_re( R"([A-Za-z0-9_./-]+\.cpp)" );
    struct ScriptTokens
    {
        fs::path path;
        std::set<std::string> basenames;
        std::vector<std::string> paths; // tokens containing '/'
    };
    std::vector<ScriptTokens> scripts;
    std::vector<std::string> all_path_tokens;
    for ( const fs::path &script : collect_build_scripts( root ) )
    {
        const std::string text = strip_cmake_comments( slurp( script ) );
        ScriptTokens entry;
        entry.path = fs::weakly_canonical( script );
        for ( std::sregex_iterator it( text.begin(), text.end(), cpp_token_re ), end; it != end;
              ++it )
        {
            const std::string tok = ( *it )[0].str();
            entry.basenames.insert( tok.substr( tok.rfind( '/' ) + 1 ) );
            if ( tok.find( '/' ) != std::string::npos )
            {
                entry.paths.push_back( tok );
                all_path_tokens.push_back( tok );
            }
        }
        scripts.push_back( std::move( entry ) );
    }

    std::vector<fs::path> unlisted;
    // Known-unwired sources kept intentionally, each with its reason. Anything
    // not on this list still trips the gate.
    //  - src/stubs/qwt/qwt_plot_renderer.cpp: offline qwt-free stub lane with
    //    no include-path consumer on master (real Qwt comes from
    //    external/qwt-6.3.0 or the system install). The stub header's inline
    //    render() delegates to renderPlot() defined here, so the pair must
    //    stay coherent; wiring or deleting the lane is a product-lane
    //    decision, not wiring cleanup.
    //  - src/app/maptools/{qgsappmaptools,qgsmaptoolsdigitizingtechniquemanager}.cpp:
    //    deliberately commented out of src/app/CMakeLists.txt ("needs deep
    //    QgisApp integration"); no header consumers. Owned by the app-shell
    //    integration effort, not this track.
    static const std::set<fs::path> kExemptUnlisted = {
        src_root / "stubs" / "qwt" / "qwt_plot_renderer.cpp",
        src_root / "app" / "maptools" / "qgsappmaptools.cpp",
        src_root / "app" / "maptools" / "qgsmaptoolsdigitizingtechniquemanager.cpp",
    };
    for ( auto it = fs::recursive_directory_iterator( src_root ),
               end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( !it->is_regular_file() || it->path().extension() != ".cpp" )
            continue;
        const fs::path file = fs::weakly_canonical( it->path() );
        const std::string basename = file.filename().string();
        // The file's module scope: the nearest ancestor directory (walking up
        // to src/) that carries a CMakeLists.txt. A sibling module's script is
        // NOT eligible, so a same-named source listed elsewhere under src/
        // cannot mask this file being delisted from its own module.
        fs::path module_root;
        for ( fs::path anc = file.parent_path();; anc = anc.parent_path() )
        {
            if ( fs::exists( anc / "CMakeLists.txt" ) )
            {
                module_root = anc;
                break;
            }
            if ( anc == src_root )
                break;
        }
        bool covered = false;
        for ( const ScriptTokens &script : scripts )
        {
            // Eligible: script outside src/ (tests/tools compiling a source
            // directly), or under the file's own module scope.
            const bool eligible = !is_under( script.path, src_root ) ||
                                  ( !module_root.empty() && is_under( script.path, module_root ) );
            if ( eligible && script.basenames.count( basename ) )
            {
                covered = true;
                break;
            }
        }
        if ( !covered )
        {
            // Path-qualified listing anywhere in the repository (relative
            // subpath like widgets/x.cpp, or .../src/<module-relative>/x.cpp).
            // Candidate tails are the module-relative path itself and its
            // suffixes that still contain a slash; the bare basename stays
            // scoped to the module so it can never match globally.
            const std::string rel = fs::relative( file, src_root ).generic_string();
            std::vector<std::string> tails{ rel };
            for ( std::size_t slash = rel.find( '/' ); slash != std::string::npos;
                  slash = rel.find( '/', slash + 1 ) )
            {
                const std::string tail = rel.substr( slash + 1 );
                if ( tail.find( '/' ) != std::string::npos )
                    tails.push_back( tail );
            }
            for ( const std::string &tail : tails )
            {
                if ( covered )
                    break;
                for ( const std::string &tok : all_path_tokens )
                {
                    if ( tok == tail || ( tok.size() > tail.size() &&
                                          tok.compare( tok.size() - tail.size() - 1, tail.size() + 1,
                                                       "/" + tail ) == 0 ) )
                    {
                        covered = true;
                        break;
                    }
                }
            }
        }
        if ( !covered && !kExemptUnlisted.count( file ) )
            unlisted.push_back( it->path() );
    }

    // 3. Every tests/test_*.cpp (recursive) registered in tests/CMakeLists.txt.
    const std::string tests_cm =
        strip_cmake_comments( slurp( root / "tests" / "CMakeLists.txt" ) );
    std::vector<std::string> unregistered;
    for ( auto it = fs::recursive_directory_iterator( root / "tests" ),
               end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( it->is_directory() && is_excluded_dir( it->path().filename().string() ) )
        {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path p = it->path();
        if ( !it->is_regular_file() || p.extension() != ".cpp" )
            continue;
        const std::string stem = p.stem().string();
        if ( stem.rfind( "test_", 0 ) != 0 )
            continue;
        if ( !contains_word( tests_cm, stem ) )
            unregistered.push_back( stem );
    }

    std::string violations;
    for ( const fs::path &p : unreachable )
        violations += "unreachable CMakeLists (never add_subdirectory'ed): " + p.string() + "\n";
    for ( const fs::path &p : unlisted )
        violations += "src source file named by no eligible CMake script: " + p.string() + "\n";
    for ( const std::string &s : unregistered )
        violations += "test source not registered in tests/CMakeLists.txt: " + s + "\n";
    INFO( "wiring drift:\n" << violations );
    REQUIRE( violations.empty() );
}
