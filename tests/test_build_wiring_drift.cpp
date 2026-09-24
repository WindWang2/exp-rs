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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <utility>
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

// ─────────────────────────────────────────────────────────────────────────────
// Target-graph rules (post-merge build health, #1274/#1276 incident classes).
//
// Rules 4–8 below cover the fault classes the gate above cannot see. Each was
// a real merge incident on this repository between #1274 and #1276:
//
//   4. duplicate target definition (CMP0002 — #1276: test_sci_inspector was
//      registered twice and master configure broke). Two definitions collide
//      only when they can both execute in one configure: same project tree,
//      not separated by an if/elseif/else branch boundary, and neither site
//      wrapped in a `if(NOT TARGET <name>)` guard.
//   5. a helper-registered test whose implicit `${NAME}.cpp` does not exist
//      (CMake breaks at generate, after configure printed nothing).
//   6. a source file listed in a target's source list that does not exist on
//      disk (generate-time "Cannot find source file").
//   7. a bare link name in target_link_libraries that is no in-tree target
//      (silently becomes `-lname` and breaks at link time; the #1274
//      missing-jsoncpp incident is this class).
//   8. a bare ws2_32/wsock32 link outside a WIN32/MSVC guard (breaks the
//      POSIX platforms; the guarded form is the repository norm).
//
// Scope: the main build tree only — scripts reachable from the root
// CMakeLists.txt plus the find modules and helpers it loads. Directories with
// their own project() that no add_subdirectory reaches (mission-runtime-gate,
// cmake/LibSVM, examples/plugins/*, tests/fixtures/*, external standalones)
// are separate build trees that configure their own closure and are exempt.
//
// Known text-level limitations, all failing in the direction that keeps
// honest wiring green except where noted: generator expressions, ${VAR}
// sources and multi-line if( conditions cannot be checked statically and are
// skipped; bracket comments #[[...]] (none exist in this repository) would
// make their interior lines count as live wiring, i.e. a false positive, not
// a miss; an else() branch's effective condition is the *negation* of its
// chain, so it is treated as statically unknown — never dead (rule 7 checks
// it) and never platform-guarded (rule 8 flags it).
//
// Every rule is also exercised against synthetic fixture trees below, where
// each fault class is injected and must turn exactly that rule red — a gate
// that cannot fail proves nothing.
// ─────────────────────────────────────────────────────────────────────────────
namespace wiring
{

struct DefSite
{
    std::size_t script = 0;
    std::size_t line = 0;
};

// One if/elseif/else branch level enclosing a line. `chain_line` identifies
// the if/elseif/else chain (the line of its opening if), `selector` the
// branch within that chain ("if:<cond>", "elseif:<cond>", "else") — two lines
// can never both execute when some depth has equal chain_line but a
// different selector, which is CMake's own branch exclusivity.
struct Frame
{
    std::size_t chain_line = 0;
    std::string selector;
    std::string cond;
};

struct Script
{
    fs::path path;
    std::string text; // comment-stripped
    std::vector<std::string> lines;
    bool main_tree = false;
    bool find_module = false;
    std::vector<std::vector<Frame>> frames; // per line, outermost first
};

struct Helper
{
    std::string param;
    std::string template_dir; // "" = bare ${NAME}.cpp next to the script
    bool has_template = false;
};

struct RepoModel
{
    fs::path root;
    std::vector<Script> scripts;
    std::map<fs::path, std::size_t> index;
    std::set<std::string> targets;     // main-tree literal + helper-created
    std::set<std::string> aliases;
    std::set<std::string> assigned_vars; // set()/option() first args, all scripts
    std::set<std::string> generated;     // generated-output basenames, main tree
    std::map<std::string, Helper> helpers;
    std::vector<DefSite> target_sites( const std::string &name ) const
    {
        auto it = target_site_map.find( name );
        return it == target_site_map.end() ? std::vector<DefSite>{} : it->second;
    }
    std::map<std::string, std::vector<DefSite>> target_site_map;
};

namespace
{

struct Cmd
{
    std::string name;
    std::string args;
    std::size_t line = 0;   // 1-based
    std::size_t offset = 0; // byte offset of the command name in the text
};

std::vector<Cmd> scan_commands( const std::string &text )
{
    std::vector<Cmd> out;
    std::size_t i = 0;
    while ( i < text.size() )
    {
        if ( !is_word_char( text[i] ) || isdigit( static_cast<unsigned char>( text[i] ) ) )
        {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while ( i < text.size() && is_word_char( text[i] ) )
            ++i;
        const std::string name = text.substr( start, i - start );
        std::size_t j = i;
        while ( j < text.size() && isspace( static_cast<unsigned char>( text[j] ) ) )
            ++j;
        if ( j >= text.size() || text[j] != '(' )
            continue;
        std::size_t depth = 1;
        std::size_t k = j + 1;
        while ( k < text.size() && depth )
        {
            if ( text[k] == '(' )
                ++depth;
            else if ( text[k] == ')' )
                --depth;
            ++k;
        }
        Cmd cmd;
        cmd.name = name;
        cmd.args = text.substr( j + 1, k - j - 2 );
        cmd.line = 1 + static_cast<std::size_t>( std::count( text.begin(), text.begin() + start, '\n' ) );
        cmd.offset = start;
        out.push_back( std::move( cmd ) );
        i = k;
    }
    return out;
}

std::vector<std::string> split_args( const std::string &args )
{
    std::vector<std::string> out;
    std::string cur;
    for ( const char c : args )
    {
        if ( isspace( static_cast<unsigned char>( c ) ) )
        {
            if ( !cur.empty() )
                out.push_back( std::exchange( cur, {} ) );
        }
        else
            cur.push_back( c );
    }
    if ( !cur.empty() )
        out.push_back( cur );
    return out;
}

bool valid_target_name( const std::string &s )
{
    if ( s.empty() || !is_word_char( s[0] ) )
        return false;
    for ( const char c : s )
        if ( !is_word_char( c ) && c != '.' && c != ':' && c != '-' )
            return false;
    return true;
}

bool is_source_token( const std::string &s )
{
    static const char *exts[] = { ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".ui", ".qrc", ".mm", ".m", ".rc" };
    for ( const char *e : exts )
        if ( s.size() > strlen( e ) && s.compare( s.size() - strlen( e ), strlen( e ), e ) == 0 )
            return true;
    return false;
}

// Frames for every line: walk if/elseif/else/endif (case-insensitive, alone on
// their line, single condition — the repository style) and record the branch
// chain state after each opening line.
std::vector<std::vector<Frame>> branch_frames( const std::vector<std::string> &lines )
{
    std::vector<std::vector<Frame>> per_line( lines.size() );
    std::vector<Frame> stack;
    static const std::regex re( R"(^\s*(if|elseif|else|endif)\s*\((.*)\)\s*$)" );
    for ( std::size_t li = 0; li < lines.size(); ++li )
    {
        std::smatch m;
        if ( std::regex_match( lines[li], m, re ) )
        {
            const std::string kw = m[1].str();
            const std::string cond = m[2].str();
            if ( kw == "if" )
                stack.push_back( { li, "if:" + cond, cond } );
            else if ( kw == "elseif" && !stack.empty() )
            {
                stack.back().selector = "elseif:" + cond;
                stack.back().cond = cond;
            }
            else if ( kw == "else" && !stack.empty() )
            {
                // an else branch runs under the *negation* of the chain's
                // conditions, which we do not reconstruct: clear the
                // inherited condition so consumers treat the branch as
                // statically unknown (never dead, never guarded)
                stack.back().selector = "else";
                stack.back().cond.clear();
            }
            else if ( kw == "endif" && !stack.empty() )
                stack.pop_back();
        }
        per_line[li] = stack;
    }
    return per_line;
}

// Frames enclosing a 1-based command line (empty when out of range).
const std::vector<Frame> &frames_at( const Script &s, std::size_t one_based_line )
{
    static const std::vector<Frame> empty;
    if ( one_based_line == 0 || one_based_line > s.frames.size() )
        return empty;
    return s.frames[one_based_line - 1];
}

// True when some enclosing condition guards the target name against a second
// definition, i.e. `if(NOT TARGET foo)`. A *positive* `if(TARGET foo)` is the
// opposite of a def guard: its branch executes precisely when foo already
// exists, so a definition inside it is a guaranteed collision.
bool target_guarded( const Script &s, std::size_t line, const std::string &name )
{
    for ( const Frame &f : frames_at( s, line ) )
    {
        const std::string &cond = f.cond;
        for ( std::size_t pos = cond.find( "TARGET" ); pos != std::string::npos;
              pos = cond.find( "TARGET", pos + 1 ) )
        {
            std::size_t p = pos + 6;
            while ( p < cond.size() && isspace( static_cast<unsigned char>( cond[p] ) ) )
                ++p;
            std::string word;
            while ( p < cond.size() && ( is_word_char( cond[p] ) || cond[p] == ':' || cond[p] == '.' || cond[p] == '-' ) )
                word.push_back( cond[p++] );
            if ( word == name )
            {
                // require the `NOT` keyword immediately before TARGET
                std::size_t q = pos;
                while ( q > 0 && isspace( static_cast<unsigned char>( cond[q - 1] ) ) )
                    --q;
                const std::size_t tok_end = q;
                while ( q > 0 && is_word_char( cond[q - 1] ) )
                    --q;
                if ( cond.compare( q, tok_end - q, "NOT" ) == 0 )
                    return true;
            }
        }
    }
    return false;
}

// True when the line sits inside a branch whose whole condition is one bare
// identifier that CMake never assigns in this tree and that is not a platform
// built-in: the branch is statically dead today. If the variable ever gets an
// option()/set(), the branch becomes live and the links inside are checked.
bool in_dead_branch( const Script &s, std::size_t line, const std::set<std::string> &assigned )
{
    static const std::set<std::string> builtin = {
        "WIN32", "UNIX", "APPLE", "MSVC", "CYGWIN", "MSYS", "ANDROID", "IOS",
    };
    static const std::regex bare( R"(^\s*[A-Za-z_][A-Za-z0-9_]*\s*$)" );
    for ( const Frame &f : frames_at( s, line ) )
    {
        if ( !std::regex_match( f.cond, bare ) )
            continue;
        std::string id = f.cond;
        id.erase( remove_if( id.begin(), id.end(), []( char c ) { return isspace( static_cast<unsigned char>( c ) ); } ), id.end() );
        if ( !builtin.count( id ) && !assigned.count( id ) )
            return true;
    }
    return false;
}

bool win32_guarded( const Script &s, std::size_t line )
{
    for ( const Frame &f : frames_at( s, line ) )
        if ( f.cond.find( "WIN32" ) != std::string::npos || f.cond.find( "MSVC" ) != std::string::npos )
            return true;
    return false;
}

// Two sites in one script can never both execute when a branch chain they
// share opened in different selectors.
bool branch_separated( const Script &s, std::size_t la, std::size_t lb )
{
    const std::vector<Frame> &fa = frames_at( s, la );
    const std::vector<Frame> &fb = frames_at( s, lb );
    const std::size_t n = std::min( fa.size(), fb.size() );
    for ( std::size_t d = 0; d < n; ++d )
        if ( fa[d].chain_line == fb[d].chain_line && fa[d].selector != fb[d].selector )
            return true;
    return false;
}

std::string after_root_marker( std::string w, const fs::path &root, const fs::path &script_dir, bool rooted_ok )
{
    // Resolve the root/current-dir markers produced by var-normalization;
    // returns "" when the word still references an unresolvable variable.
    const std::string r = root.string();
    const std::string c = script_dir.string();
    std::string out;
    for ( std::size_t i = 0; i < w.size(); )
    {
        if ( w[i] == '\x01' && i + 1 < w.size() )
        {
            if ( w[i + 1] == 'R' )
            {
                if ( !rooted_ok )
                    return "";
                out += r;
            }
            else if ( w[i + 1] == 'W' )
                out += c;
            else
                return "";
            i += 2;
        }
        else
            out.push_back( w[i++] );
    }
    return out;
}

} // namespace

RepoModel build_model( const fs::path &root )
{
    RepoModel model;
    model.root = fs::weakly_canonical( root );

    // Reachability from the root script (same textual over-approximation as
    // rule 1) to separate the main tree from standalone project() trees.
    std::set<fs::path> reachable;
    {
        std::vector<fs::path> queue{ model.root };
        reachable.insert( model.root );
        while ( !queue.empty() )
        {
            const fs::path dir = queue.back();
            queue.pop_back();
            const fs::path cm = dir / "CMakeLists.txt";
            if ( !fs::exists( cm ) )
                continue;
            const std::string text = strip_cmake_comments( slurp( cm ) );
            for ( std::sregex_iterator it( text.begin(), text.end(), subdir_regex() ), end; it != end; ++it )
            {
                const auto resolved = resolve_subdir_operand( ( *it )[1].str(), dir );
                if ( resolved && reachable.insert( *resolved ).second )
                    queue.push_back( *resolved );
            }
        }
    }

    // Collect scripts, skipping build/VCS output directories.
    std::vector<fs::path> paths;
    for ( auto it = fs::recursive_directory_iterator( model.root ), end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( it->is_directory() && is_excluded_dir( it->path().filename().string() ) )
        {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path &p = it->path();
        if ( it->is_regular_file() && ( p.filename() == "CMakeLists.txt" || p.extension() == ".cmake" ) )
            paths.push_back( p );
    }
    std::sort( paths.begin(), paths.end() );

    std::set<fs::path> standalone_roots;
    for ( const fs::path &p : paths )
    {
        const fs::path dir = fs::weakly_canonical( p.parent_path() );
        if ( dir == model.root || reachable.count( dir ) )
            continue;
        const std::string t = slurp( p );
        if ( t.find( "project(" ) != std::string::npos &&
             t.find( "cmake_minimum_required" ) != std::string::npos )
            standalone_roots.insert( dir );
    }

    for ( const fs::path &p : paths )
    {
        Script s;
        s.path = fs::weakly_canonical( p );
        s.text = strip_cmake_comments( slurp( p ) );
        {
            std::string cur;
            for ( const char c : s.text )
            {
                if ( c == '\n' )
                    s.lines.push_back( std::exchange( cur, {} ) );
                else
                    cur.push_back( c );
            }
            if ( !cur.empty() )
                s.lines.push_back( cur );
        }
        bool under_standalone = false;
        for ( const fs::path &sr : standalone_roots )
            if ( is_under( s.path, sr ) )
                under_standalone = true;
        s.main_tree = !under_standalone;
        const std::string fn = s.path.filename().string();
        s.find_module = fn.rfind( "Find", 0 ) == 0 || fn.find( "Config.cmake" ) != std::string::npos;
        s.frames = branch_frames( s.lines );
        model.index[s.path] = model.scripts.size();
        model.scripts.push_back( std::move( s ) );
    }

    // assigned vars: set()/option() first args in main-tree scripts (a
    // standalone tree's assignments say nothing about the main build).
    for ( const Script &s : model.scripts )
    {
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
            if ( cmd.name == "set" || cmd.name == "option" )
            {
                const std::vector<std::string> toks = split_args( cmd.args );
                if ( !toks.empty() && valid_target_name( toks[0] ) )
                    model.assigned_vars.insert( toks[0] );
            }
    }

    // Target-creating helpers: functions/macros that run
    // add_executable(${PARAM} ...) / add_library(${PARAM} ...) on a parameter,
    // and whose body shows how the implicit ${PARAM}.cpp source is located.
    const std::set<std::string> target_cmds = {
        "add_executable", "add_library", "qt_add_executable", "qt_add_library", "qt_add_plugin",
    };
    for ( const Script &s : model.scripts )
    {
        if ( !s.main_tree )
            continue;
        const std::vector<Cmd> cmds = scan_commands( s.text );
        for ( std::size_t ci = 0; ci < cmds.size(); ++ci )
        {
            const Cmd &cmd = cmds[ci];
            if ( cmd.name != "function" && cmd.name != "macro" )
                continue;
            const std::vector<std::string> params = split_args( cmd.args );
            if ( params.empty() )
                continue;
            const std::string fname = params[0];
            const std::set<std::string> pset( params.begin() + 1, params.end() );
            const std::string endkw = "end" + cmd.name + "(";
            // body: from after this function( to the next end<kw> — CMake has
            // no nested function definitions.
            const std::size_t body_begin = cmd.offset + cmd.name.size() + 1 + cmd.args.size() + 2;
            const std::size_t epos = s.text.find( endkw, body_begin );
            if ( epos == std::string::npos || epos < body_begin )
                continue;
            const std::string body = s.text.substr( body_begin, epos - body_begin );
            for ( const std::string &tc : target_cmds )
            {
                std::size_t pos = body.find( tc + "(" );
                for ( ; pos != std::string::npos; pos = body.find( tc + "(", pos + 1 ) )
                {
                    std::size_t b = pos + tc.size() + 1;
                    std::string first;
                    while ( b < body.size() && ( is_word_char( body[b] ) || body[b] == '$' || body[b] == '{' ||
                                                 body[b] == '}' ) )
                        first.push_back( body[b++] );
                    if ( first.size() <= 3 || first[0] != '$' || first[1] != '{' || first.back() != '}' )
                        continue;
                    const std::string param = first.substr( 2, first.size() - 3 );
                    if ( !pset.count( param ) )
                        continue;
                    Helper h;
                    h.param = param;
                    // how is the implicit ${PARAM}.cpp source referenced?
                    const std::string needle = "${" + param + "}";
                    for ( std::size_t np = body.find( needle ); np != std::string::npos;
                          np = body.find( needle, np + 1 ) )
                    {
                        const std::size_t after = np + needle.size();
                        if ( after + 4 > body.size() || body.compare( after, 4, ".cpp" ) != 0 )
                            continue;
                        // walk back over the static prefix of the path token
                        std::size_t start = np;
                        while ( start > 0 && ( is_word_char( body[start - 1] ) || body[start - 1] == '/' ||
                                body[start - 1] == '.' || body[start - 1] == '$' || body[start - 1] == '{' ||
                                body[start - 1] == '}' || body[start - 1] == '-' || body[start - 1] == '+' ) )
                            --start;
                        const std::string prefix = body.substr( start, np - start );
                        // resolve ${...} vars in the prefix into markers:
                        // current-dir stays per-call-site (CMake expands
                        // ${CMAKE_CURRENT_SOURCE_DIR} in the *calling*
                        // scope), root vars collapse to the repo root,
                        // unknown vars → no template (fail-open)
                        std::string resolved;
                        bool ok = true;
                        for ( std::size_t k = 0; k < prefix.size(); )
                        {
                            if ( prefix[k] == '$' )
                            {
                                const std::size_t close = prefix.find( '}', k );
                                if ( close == std::string::npos ) { ok = false; break; }
                                const std::string var = prefix.substr( k + 2, close - k - 2 );
                                if ( var == "CMAKE_CURRENT_SOURCE_DIR" )
                                    resolved += "\x01W";
                                else if ( var == "CMAKE_SOURCE_DIR" || var == "SICNU_SOURCE_DIR" ||
                                          var == "PROJECT_SOURCE_DIR" )
                                    resolved += "\x01R";
                                else { ok = false; break; }
                                k = close + 1;
                            }
                            else
                                resolved.push_back( prefix[k++] );
                        }
                        if ( ok )
                        {
                            h.template_dir = resolved;
                            h.has_template = true;
                        }
                        break;
                    }
                    model.helpers[fname] = std::move( h );
                }
            }
        }
    }

    // Target definitions: literal target commands plus first args of helper calls.
    for ( std::size_t si = 0; si < model.scripts.size(); ++si )
    {
        const Script &s = model.scripts[si];
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            const bool target_cmd = target_cmds.count( cmd.name ) != 0;
            auto hit = model.helpers.find( cmd.name );
            const bool helper_cmd = s.main_tree && hit != model.helpers.end();
            // custom targets share the CMP0002 namespace but cannot be
            // linked — def site only, never a link-closure target
            const bool custom_cmd = cmd.name == "add_custom_target";
            if ( !target_cmd && !helper_cmd && !custom_cmd )
                continue;
            const std::vector<std::string> toks = split_args( cmd.args );
            if ( toks.empty() || !valid_target_name( toks[0] ) || is_source_token( toks[0] ) )
                continue;
            if ( custom_cmd )
            {
                if ( s.main_tree )
                    model.target_site_map[toks[0]].push_back( { si, cmd.line } );
                continue;
            }
            if ( target_cmd && cmd.name == "add_library" )
            {
                // remember aliases separately (they cannot collide with defs at
                // configure time the same way, but they count as link targets)
                const std::vector<std::string> &a = toks;
                if ( a.size() >= 3 && a[1] == "ALIAS" )
                {
                    if ( s.main_tree )
                        model.aliases.insert( a[0] );
                    continue;
                }
            }
            if ( !s.main_tree )
                continue;
            model.targets.insert( toks[0] );
            model.target_site_map[toks[0]].push_back( { si, cmd.line } );
        }
    }

    // Generated outputs: configure_file targets and custom command OUTPUT.
    for ( const Script &s : model.scripts )
    {
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            if ( cmd.name == "configure_file" )
            {
                const std::vector<std::string> toks = split_args( cmd.args );
                if ( toks.size() >= 2 && toks[1].find( '$' ) == std::string::npos )
                    model.generated.insert( fs::path( toks[1] ).filename().string() );
            }
            else if ( cmd.name == "add_custom_command" || cmd.name == "add_custom_target" )
            {
                std::size_t pos = cmd.args.find( "OUTPUT" );
                if ( pos == std::string::npos )
                    continue;
                bool first = true;
                for ( const std::string &tok : split_args( cmd.args.substr( pos + 6 ) ) )
                {
                    if ( tok.find( '$' ) != std::string::npos )
                        break;
                    // stop at the next keyword (COMMAND, DEPENDS, …)
                    if ( !first && !tok.empty() && tok[0] >= 'A' && tok[0] <= 'Z' )
                        break;
                    first = false;
                    model.generated.insert( fs::path( tok ).filename().string() );
                }
            }
        }
    }
    return model;
}

// Rule 4 — duplicate target definitions (CMP0002 class, the #1276 incident).
//
// A pair of definition sites collides only when both can execute in one
// configure: neither is wrapped in a TARGET guard for the name, neither lives
// in a find module (loaded at most on one code path by convention), and — for
// two sites in the same script — they are not separated by an if/elseif/else
// branch boundary.
std::vector<std::string> check_duplicate_targets( const RepoModel &m )
{
    const auto pair_safe = [&m]( const DefSite &a, const DefSite &b, const std::string &name ) {
        const Script &sa = m.scripts[a.script];
        const Script &sb = m.scripts[b.script];
        if ( target_guarded( sa, a.line, name ) || sa.find_module )
            return true;
        if ( target_guarded( sb, b.line, name ) || sb.find_module )
            return true;
        // a statically dead branch cannot produce a definition
        if ( in_dead_branch( sa, a.line, m.assigned_vars ) ||
             in_dead_branch( sb, b.line, m.assigned_vars ) )
            return true;
        if ( a.script == b.script )
            return branch_separated( sa, a.line, b.line );
        return false;
    };
    std::vector<std::string> out;
    for ( const auto &entry : m.target_site_map )
    {
        const std::string &name = entry.first;
        const std::vector<DefSite> &sites = entry.second;
        if ( sites.size() < 2 )
            continue;
        bool collision = false;
        for ( std::size_t i = 0; i < sites.size() && !collision; ++i )
            for ( std::size_t j = i + 1; j < sites.size(); ++j )
                if ( !pair_safe( sites[i], sites[j], name ) )
                {
                    collision = true;
                    break;
                }
        if ( collision )
        {
            std::string msg = "duplicate target definition (CMP0002 class): " + name + " defined at";
            for ( const DefSite &d : sites )
                msg += "\n  " + m.scripts[d.script].path.string() + ":" + std::to_string( d.line );
            out.push_back( msg );
        }
    }
    return out;
}

// Rule 5 — every helper-registered test's implicit ${NAME}.cpp must exist
// (generate-time failure class; the reverse direction of rule 3).
std::vector<std::string> check_registered_test_sources( const RepoModel &m )
{
    std::vector<std::string> out;
    for ( std::size_t si = 0; si < m.scripts.size(); ++si )
    {
        const Script &s = m.scripts[si];
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            auto hit = m.helpers.find( cmd.name );
            if ( hit == m.helpers.end() || !hit->second.has_template )
                continue;
            if ( in_dead_branch( s, cmd.line, m.assigned_vars ) )
                continue;
            const std::vector<std::string> toks = split_args( cmd.args );
            if ( toks.empty() || !valid_target_name( toks[0] ) )
                continue;
            // template prefixes carry markers: \x01R = repo root, \x01W =
            // the calling script's dir (CMake expands current-dir vars in
            // the caller's scope); a bare template sits next to the caller
            const std::string prefix = hit->second.template_dir.empty()
                                           ? std::string( "\x01W/" )
                                           : hit->second.template_dir;
            const std::string resolved = after_root_marker( prefix + toks[0] + ".cpp",
                                                            m.root, s.path.parent_path(), true );
            if ( resolved.empty() )
                continue; // unresolvable template: fail-open
            const fs::path expected( resolved );
            if ( fs::exists( expected ) )
                continue;
            out.push_back( "helper-registered test source missing on disk: " + toks[0] + ".cpp (" +
                           s.path.filename().string() + ":" + std::to_string( cmd.line ) + " expected " +
                           expected.string() + ")" );
        }
    }
    return out;
}

// Rule 6 — sources listed by target commands must exist on disk
// (generate-time "Cannot find source file" class).
std::vector<std::string> check_listed_sources( const RepoModel &m )
{
    static const std::set<std::string> src_cmds = { "add_executable", "add_library", "qt_add_executable",
                                                    "qt_add_library", "target_sources"
                                                  };
    std::vector<std::string> out;
    for ( std::size_t si = 0; si < m.scripts.size(); ++si )
    {
        const Script &s = m.scripts[si];
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            if ( !src_cmds.count( cmd.name ) )
                continue;
            // normalize variable refs: root/current markers, others unknown
            std::string norm;
            for ( std::size_t k = 0; k < cmd.args.size(); )
            {
                if ( cmd.args[k] == '$' && k + 1 < cmd.args.size() && cmd.args[k + 1] == '{' )
                {
                    const std::size_t close = cmd.args.find( '}', k );
                    if ( close == std::string::npos )
                    {
                        norm.push_back( cmd.args[k++] );
                        continue;
                    }
                    const std::string var = cmd.args.substr( k + 2, close - k - 2 );
                    if ( var == "CMAKE_SOURCE_DIR" || var == "SICNU_SOURCE_DIR" || var == "PROJECT_SOURCE_DIR" )
                        norm += "\x01R";
                    else if ( var == "CMAKE_CURRENT_SOURCE_DIR" )
                        norm += "\x01W";
                    else
                        norm += "\x01U";
                    k = close + 1;
                }
                else
                    norm.push_back( cmd.args[k++] );
            }
            for ( const std::string &word : split_args( norm ) )
            {
                if ( word.find( "\x01U" ) != std::string::npos || word.find( '$' ) != std::string::npos ||
                     word.find( '*' ) != std::string::npos )
                    continue;
                if ( !is_source_token( word ) )
                    continue;
                const std::string resolved = after_root_marker( word, m.root, s.path.parent_path(), true );
                if ( resolved.empty() )
                    continue;
                fs::path p( resolved );
                if ( p.is_relative() )
                    p = s.path.parent_path() / p;
                if ( fs::exists( p ) )
                    continue;
                if ( m.generated.count( p.filename().string() ) )
                    continue;
                out.push_back( "listed source does not exist: " + p.string() + " (" +
                               s.path.filename().string() + ":" + std::to_string( cmd.line ) + ")" );
            }
        }
    }
    return out;
}

// Rule 7 — bare link names must resolve to an in-tree target; anything else
// silently degrades to -lname and breaks at link time (#1274 class).
std::vector<std::string> check_link_closure( const RepoModel &m )
{
    static const std::set<std::string> scope_kw = {
        "PUBLIC", "PRIVATE", "INTERFACE", "debug", "optimized", "general",
        "LINK_PUBLIC", "LINK_PRIVATE", "LINK_INTERFACE_LIBRARIES",
    };
    // Externally-defined link targets this tree links by name. System libs and
    // upstream package targets that export a non-namespaced name.
    static const std::set<std::string> allowlisted = {
        // POSIX / Windows system libraries
        "m", "pthread", "dl", "rt", "atomic", "util", "socket", "nsl", "resolv",
        "ws2_32", "wsock32", "winmm", "shlwapi", "bcrypt", "advapi32", "user32",
        "gdi32", "shell32", "ole32", "oleaut32", "uuid", "version", "imm32",
        "setupapi", "cfgmgr32", "psapi", "iphlpapi", "userenv", "dbghelp",
        "ntdll", "kernel32", "opengl32", "dwmapi", "uxtheme", "comctl32",
        "odbc32", "crypt32", "secur32", "mscms", "odbc", "odbccp32", "xkbcommon",
        "X11", "xcb", "GL", "EGL", "GLESv2",
        // common system libraries linked by bare name
        "z", "ssl", "crypto", "jpeg", "png", "curl", "expat", "sqlite3",
        "iconv", "xml2", "stdc++fs", "c", "stdc++", "qca", "openjp2", "blend2d",
        // FetchContent / upstream package targets without a namespace
        "Catch2WithMain",        // Catch2 v3 via FetchContent (tests/CMakeLists)
        // Documented conditional externals (fail-open, with reason):
        "jsoncpp_lib",           // upstream jsoncpp config target; leaf modules
                                 // keep a layered fallback chain around it and
                                 // #1277–#1279 own those files — not renamed here
        "exiv2lib",              // exiv2's official exported target name
    };
    std::vector<std::string> out;
    for ( std::size_t si = 0; si < m.scripts.size(); ++si )
    {
        const Script &s = m.scripts[si];
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            if ( cmd.name != "target_link_libraries" && cmd.name != "link_libraries" )
                continue;
            for ( const std::string &tok : split_args( cmd.args ) )
            {
                if ( scope_kw.count( tok ) )
                    continue;
                if ( tok.find( "::" ) != std::string::npos )
                    continue; // imported/alias namespace: generate-time checked
                if ( !tok.empty() && ( tok[0] == '-' || tok[0] == '$' || tok[0] == '\x01' ) )
                    continue; // flags, generator expressions, ${VAR} refs
                if ( !valid_target_name( tok ) )
                    continue;
                // bare ALL_CAPS tokens are variable-name spellings, not targets
                bool all_caps = true;
                for ( const char c : tok )
                    if ( !( ( c >= 'A' && c <= 'Z' ) || c == '_' || isdigit( static_cast<unsigned char>( c ) ) ) )
                        all_caps = false;
                if ( all_caps && tok.find( '_' ) != std::string::npos )
                    continue;
                if ( m.targets.count( tok ) || m.aliases.count( tok ) || allowlisted.count( tok ) )
                    continue;
                if ( in_dead_branch( s, cmd.line, m.assigned_vars ) )
                    continue;
                out.push_back( "phantom link name (resolves to -l" + tok + ", no in-tree target): " +
                               s.path.filename().string() + ":" + std::to_string( cmd.line ) );
            }
        }
    }
    return out;
}

// Rule 8 — bare ws2_32/wsock32 links must sit behind a WIN32/MSVC guard
// (the POSIX-platform link breakage class).
std::vector<std::string> check_platform_link_guards( const RepoModel &m )
{
    std::vector<std::string> out;
    for ( std::size_t si = 0; si < m.scripts.size(); ++si )
    {
        const Script &s = m.scripts[si];
        if ( !s.main_tree )
            continue;
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            if ( cmd.name != "target_link_libraries" && cmd.name != "link_libraries" )
                continue;
            bool has = false;
            for ( const std::string &tok : split_args( cmd.args ) )
            {
                // recognize the -l spelling too ("PRIVATE -lws2_32")
                const std::string bare =
                    ( tok.rfind( "-l", 0 ) == 0 && tok.size() > 2 ) ? tok.substr( 2 ) : tok;
                if ( bare == "ws2_32" || bare == "wsock32" )
                    has = true;
            }
            if ( !has || win32_guarded( s, cmd.line ) )
                continue;
            out.push_back( "bare ws2_32/wsock32 link without WIN32/MSVC guard (breaks POSIX): " +
                           s.path.filename().string() + ":" + std::to_string( cmd.line ) );
        }
    }
    return out;
}

} // namespace wiring

namespace
{

// The live model, built once for the repository under test.
const wiring::RepoModel &live_model()
{
    static const wiring::RepoModel model = wiring::build_model( fs::weakly_canonical( CMAKE_SOURCE_DIR ) );
    return model;
}

TEST_CASE( "build wiring drift: no duplicate target definitions", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_duplicate_targets( live_model() );
    INFO( "duplicate targets:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: registered test sources exist", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_registered_test_sources( live_model() );
    INFO( "missing test sources:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: listed sources exist", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_listed_sources( live_model() );
    INFO( "missing sources:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: link closure", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_link_closure( live_model() );
    INFO( "phantom links:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: platform link guards", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_platform_link_guards( live_model() );
    INFO( "unguarded win32 links:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

} // namespace

// ─── Kill tests: each rule must fail when its fault class is injected ───────
// into a synthetic tree, and stay green when the same wiring is legal
// (guarded / branch-separated / allowlisted). Mirrors the philosophy of
// scripts/dev/tests/mutation_checks.py at the C++ gate level.

namespace
{

struct Fixture
{
    fs::path root;

    ~Fixture()
    {
        std::error_code ec;
        fs::remove_all( root, ec );
    }

    static Fixture make( const std::string &name )
    {
        static std::mt19937 rng( std::random_device{}() );
        const fs::path base = fs::temp_directory_path() /
                              ( "bwdrift_fx_" + name + "_" + std::to_string( rng() ) );
        fs::remove_all( base );
        fs::create_directories( base / "src" / "lib" );
        fs::create_directories( base / "tests" );
        {
            std::ofstream( base / "CMakeLists.txt" ) <<
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(fx CXX)\n"
                "add_subdirectory(src/lib)\n"
                "add_subdirectory(tests)\n";
            std::ofstream( base / "src" / "lib" / "CMakeLists.txt" ) <<
                "add_library(fxlib a.cpp)\n";
            std::ofstream( base / "src" / "lib" / "a.cpp" ) << "int fx_a;\n";
            std::ofstream( base / "tests" / "CMakeLists.txt" ) <<
                "function(sicnu_fx_add_test NAME)\n"
                "  add_executable(${NAME} ${NAME}.cpp)\n"
                "endfunction()\n"
                "sicnu_fx_add_test(test_a)\n";
            std::ofstream( base / "tests" / "test_a.cpp" ) << "int main(){return 0;}\n";
        }
        return { base };
    }

    wiring::RepoModel model() const { return wiring::build_model( root ); }

    void append( const fs::path &rel, const std::string &text ) const
    {
        std::ofstream out( root / rel, std::ios::app );
        out << text;
    }

    void overwrite( const fs::path &rel, const std::string &text ) const
    {
        std::ofstream out( root / rel );
        out << text;
    }
};

std::size_t count_with_prefix( const std::vector<std::string> &v, const std::string &prefix )
{
    std::size_t n = 0;
    for ( const std::string &s : v )
        if ( s.rfind( prefix, 0 ) == 0 )
            ++n;
    return n;
}

} // namespace

TEST_CASE( "wiring oracle rules kill injected faults (mutation)", "[build_wiring][contract][mutation]" )
{
    SECTION( "pristine fixture is green under every rule" )
    {
        const Fixture fx = Fixture::make( "pristine" );
        const wiring::RepoModel m = fx.model();
        REQUIRE( wiring::check_duplicate_targets( m ).empty() );
        REQUIRE( wiring::check_registered_test_sources( m ).empty() );
        REQUIRE( wiring::check_listed_sources( m ).empty() );
        REQUIRE( wiring::check_link_closure( m ).empty() );
        REQUIRE( wiring::check_platform_link_guards( m ).empty() );
    }
    SECTION( "rule 4 kills an unguarded duplicate; accepts guarded and branch-separated forms" )
    {
        const Fixture fx = Fixture::make( "dup" );
        fx.append( "tests/CMakeLists.txt", "add_library(fxlib b.cpp)\n" );
        const wiring::RepoModel m = fx.model();
        REQUIRE( count_with_prefix( wiring::check_duplicate_targets( m ), "duplicate target" ) == 1 );

        Fixture fx2 = Fixture::make( "dup_guarded" );
        fx2.append( "tests/CMakeLists.txt", "if(NOT TARGET fxlib)\nadd_library(fxlib b.cpp)\nendif()\n" );
        REQUIRE( wiring::check_duplicate_targets( fx2.model() ).empty() );

        // two definitions in exclusive branches of one if/else chain can never
        // both execute — legal, same script
        Fixture fx3 = Fixture::make( "dup_branch" );
        fx3.overwrite( "src/lib/CMakeLists.txt",
                       "if(FOO)\nadd_library(fxlib c.cpp)\nelse()\nadd_library(fxlib d.cpp)\nendif()\n" );
        REQUIRE( wiring::check_duplicate_targets( fx3.model() ).empty() );

        // the same two branch definitions next to an unconditional definition
        // in another script collide with it (both can run) — illegal
        Fixture fx4 = Fixture::make( "dup_branch_cross" );
        fx4.overwrite( "src/lib/CMakeLists.txt",
                       "if(FOO)\nadd_library(fxlib c.cpp)\nelse()\nadd_library(fxlib d.cpp)\nendif()\n" );
        fx4.append( "tests/CMakeLists.txt", "add_library(fxlib e.cpp)\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_targets( fx4.model() ), "duplicate target" ) == 1 );

        // nested separate if() blocks in one branch are NOT branch separation:
        // both can execute when both conditions hold
        Fixture fx5 = Fixture::make( "dup_nested" );
        fx5.append( "src/lib/CMakeLists.txt",
                    "option(FOO \"\" OFF)\noption(BAR \"\" OFF)\n"
                    "if(FOO)\nadd_library(fxlib c.cpp)\nendif()\n"
                    "if(BAR)\nadd_library(fxlib d.cpp)\nendif()\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_targets( fx5.model() ), "duplicate target" ) == 1 );

        // a positive `if(TARGET fxlib)` is the opposite of a def guard: its
        // branch runs exactly when fxlib already exists — guaranteed collision
        Fixture fx6 = Fixture::make( "dup_positive_guard" );
        fx6.append( "tests/CMakeLists.txt", "if(TARGET fxlib)\nadd_library(fxlib dup.cpp)\nendif()\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_targets( fx6.model() ), "duplicate target" ) == 1 );

        // custom targets share the CMP0002 namespace; their dups are caught
        // even though they are not link-closure targets
        Fixture fx7 = Fixture::make( "dup_custom" );
        fx7.append( "src/lib/CMakeLists.txt", "add_custom_target(fx_doc ALL)\n" );
        fx7.append( "tests/CMakeLists.txt", "add_custom_target(fx_doc ALL)\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_targets( fx7.model() ), "duplicate target" ) == 1 );

        // a statically dead branch cannot produce a definition — no collision
        Fixture fx8 = Fixture::make( "dup_deadbranch" );
        fx8.append( "tests/CMakeLists.txt", "if(NEVER_SET)\nadd_library(fxlib z.cpp)\nendif()\n" );
        REQUIRE( wiring::check_duplicate_targets( fx8.model() ).empty() );
    }
    SECTION( "rule 5 kills a helper-registered test with no source file" )
    {
        const Fixture fx = Fixture::make( "missingsrc" );
        fx.append( "tests/CMakeLists.txt", "sicnu_fx_add_test(test_ghost)\n" );
        REQUIRE( count_with_prefix( wiring::check_registered_test_sources( fx.model() ),
                                    "helper-registered test source missing" ) == 1 );
    }
    SECTION( "rule 5 resolves current-dir templates in the calling scope" )
    {
        // helper defined in src/lib but called from tests/: ${CMAKE_CURRENT_
        // SOURCE_DIR} must expand against tests/, not the defining script
        const Fixture fx = Fixture::make( "caller_dir" );
        fx.append( "src/lib/CMakeLists.txt",
                   "function(sicnu_fx_case_test NAME)\n"
                   "  add_executable(${NAME} ${CMAKE_CURRENT_SOURCE_DIR}/cases/${NAME}.cpp)\n"
                   "endfunction()\n" );
        fx.append( "tests/CMakeLists.txt", "sicnu_fx_case_test(test_t9)\n" );
        REQUIRE( count_with_prefix( wiring::check_registered_test_sources( fx.model() ),
                                    "helper-registered test source missing" ) == 1 );

        Fixture fx2 = Fixture::make( "caller_dir_ok" );
        fx2.append( "src/lib/CMakeLists.txt",
                    "function(sicnu_fx_case_test NAME)\n"
                    "  add_executable(${NAME} ${CMAKE_CURRENT_SOURCE_DIR}/cases/${NAME}.cpp)\n"
                    "endfunction()\n" );
        fx2.append( "tests/CMakeLists.txt", "sicnu_fx_case_test(test_t9)\n" );
        fs::create_directories( fx2.root / "tests" / "cases" );
        std::ofstream( fx2.root / "tests" / "cases" / "test_t9.cpp", std::ios::out ) << "int main(){return 0;}\n";
        REQUIRE( wiring::check_registered_test_sources( fx2.model() ).empty() );
    }
    SECTION( "rule 6 kills a listed-but-absent source; accepts generated outputs" )
    {
        const Fixture fx = Fixture::make( "nosrc" );
        fx.append( "src/lib/CMakeLists.txt", "target_sources(fxlib PRIVATE nope.cpp)\n" );
        REQUIRE( count_with_prefix( wiring::check_listed_sources( fx.model() ), "listed source does not exist" ) == 1 );

        Fixture fx2 = Fixture::make( "gensrc" );
        fx2.append( "src/lib/CMakeLists.txt",
                    "configure_file(v.tpl gen_version.h)\n"
                    "set_property(TARGET fxlib APPEND PROPERTY SOURCES gen_version.h)\n" );
        std::ofstream( fx2.root / "src" / "lib" / "v.tpl" ) << "1\n";
        REQUIRE( wiring::check_listed_sources( fx2.model() ).empty() );
    }
    SECTION( "rule 7 kills a phantom link; accepts in-tree, namespaced and allowlisted names" )
    {
        const Fixture fx = Fixture::make( "phantom" );
        fx.append( "src/lib/CMakeLists.txt", "target_link_libraries(fxlib PRIVATE sicnu_ghost)\n" );
        REQUIRE( count_with_prefix( wiring::check_link_closure( fx.model() ), "phantom link name" ) == 1 );

        Fixture fx2 = Fixture::make( "phantom_ok" );
        fx2.append( "src/lib/CMakeLists.txt",
                    "target_link_libraries(fxlib PRIVATE m)\n"
                    "target_link_libraries(fxlib PRIVATE jsoncpp_lib)\n"
                    "target_link_libraries(fxlib PRIVATE Catch2::Catch2WithMain)\n" );
        REQUIRE( wiring::check_link_closure( fx2.model() ).empty() );
    }
    SECTION( "rule 7 skips links in statically dead branches; checks them once live" )
    {
        const Fixture fx = Fixture::make( "deadbranch" );
        fx.append( "src/lib/CMakeLists.txt",
                   "if(FORCE_STATIC_LIBS)\ntarget_link_libraries(fxlib PRIVATE sicnu_ghost)\nendif()\n" );
        REQUIRE( wiring::check_link_closure( fx.model() ).empty() );

        Fixture fx2 = Fixture::make( "livebranch" );
        fx2.append( "src/lib/CMakeLists.txt", "option(FORCE_STATIC_LIBS \"\" OFF)\n" );
        fx2.append( "src/lib/CMakeLists.txt",
                    "if(FORCE_STATIC_LIBS)\ntarget_link_libraries(fxlib PRIVATE sicnu_ghost)\nendif()\n" );
        REQUIRE( count_with_prefix( wiring::check_link_closure( fx2.model() ), "phantom link name" ) == 1 );

        // the else-branch of a dead variable ALWAYS runs (the condition is
        // false) — its links must not be skipped as dead
        Fixture fx3 = Fixture::make( "deadbranch_else" );
        fx3.append( "src/lib/CMakeLists.txt",
                    "if(FORCE_STATIC_LIBS)\ntarget_link_libraries(fxlib PRIVATE m)\n"
                    "else()\ntarget_link_libraries(fxlib PRIVATE sicnu_ghost)\nendif()\n" );
        REQUIRE( count_with_prefix( wiring::check_link_closure( fx3.model() ), "phantom link name" ) == 1 );
    }
    SECTION( "rule 8 kills an unguarded ws2_32 link; accepts the WIN32-guarded form" )
    {
        const Fixture fx = Fixture::make( "wsguard" );
        fx.append( "src/lib/CMakeLists.txt", "target_link_libraries(fxlib PRIVATE ws2_32)\n" );
        REQUIRE( count_with_prefix( wiring::check_platform_link_guards( fx.model() ),
                                    "bare ws2_32/wsock32 link without WIN32/MSVC guard" ) == 1 );

        Fixture fx2 = Fixture::make( "wsguarded" );
        fx2.append( "src/lib/CMakeLists.txt",
                    "if(WIN32)\ntarget_link_libraries(fxlib PRIVATE ws2_32)\nendif()\n" );
        REQUIRE( wiring::check_platform_link_guards( fx2.model() ).empty() );

        // the -l spelling is the same fault
        Fixture fx3 = Fixture::make( "wsflag" );
        fx3.append( "src/lib/CMakeLists.txt", "target_link_libraries(fxlib PRIVATE -lws2_32)\n" );
        REQUIRE( count_with_prefix( wiring::check_platform_link_guards( fx3.model() ),
                                    "bare ws2_32/wsock32 link without WIN32/MSVC guard" ) == 1 );

        // an else() branch is NOT a WIN32 guard: its condition is the
        // negation of the chain, which we cannot reconstruct
        Fixture fx4 = Fixture::make( "wselse" );
        fx4.append( "src/lib/CMakeLists.txt",
                    "if(WIN32)\ntarget_link_libraries(fxlib PRIVATE m)\n"
                    "else()\ntarget_link_libraries(fxlib PRIVATE ws2_32)\nendif()\n" );
        REQUIRE( count_with_prefix( wiring::check_platform_link_guards( fx4.model() ),
                                    "bare ws2_32/wsock32 link without WIN32/MSVC guard" ) == 1 );
    }
}
