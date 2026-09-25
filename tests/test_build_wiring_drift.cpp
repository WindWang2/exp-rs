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
    // ── body facts for embed/link/automoc binding (call sites) ──
    std::vector<std::string> params;  // declared parameter names, in order
    std::set<std::string> multi_kws;  // cmake_parse_arguments multi-value keywords
    std::set<std::string> single_kws; // cmake_parse_arguments single-value keywords
    std::string parse_prefix;         // cmake_parse_arguments <PREFIX> ("" when absent)
    std::vector<std::string> body_target_args; // tokens after ${PARAM} in the target cmd
    std::vector<std::vector<std::string>> body_link_lists; // per target_link_libraries
    std::vector<std::string> body_include_dirs; // target_include_directories tokens
    bool body_automoc = false; // set_target_properties(... ${PARAM} ... AUTOMOC ON)
    bool body_qt_cmd = false;  // target created via qt_add_* (implies AUTOMOC)
    // helper invocations inside the body (e.g. sicnu_link_jsoncpp(${NAME}))
    std::vector<std::pair<std::string, std::string>> body_helper_calls;
    std::map<std::string, std::set<std::string>> body_set_vars; // literal set() values in body
};

// Per-target wiring facts harvested mechanically from the scripts (embed
// sources, link tokens, automoc evidence, include dirs). Populated for
// main-tree targets only; helper-created targets get theirs from call-site
// binding of the helper body.
struct TargetInfo
{
    std::size_t script = 0;
    std::set<fs::path> sources;        // path-resolved sources under the repo
    std::vector<std::string> links;    // raw link tokens (incl. unknown ${VAR})
    bool has_unknown_link = false;     // an unresolvable token — closure unknown
    int automoc = -1;                  // -1 unknown, 0 off, 1 on
    std::set<fs::path> include_dirs;   // target_include_directories (resolved)
    bool helper_created = false;
    bool imported = false;             // add_library(... IMPORTED): link leaf
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
    std::map<std::string, std::string> alias_of; // ALIAS name → real target
    std::map<std::string, TargetInfo> target_info;
    // per-script literal set()/list(APPEND) variable → tokens (embed lists
    // like SICNU_FAULT_REGISTRY_SOURCE, staged source lists, …)
    std::map<fs::path, std::map<std::string, std::vector<std::string>>> script_set_vars;
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

// Keywords that appear inside target command source lists but are not sources.
bool is_source_noise( const std::string &tok )
{
    static const std::set<std::string> noise = {
        "STATIC", "SHARED", "MODULE", "INTERFACE", "OBJECT", "UNKNOWN", "IMPORTED",
        "ALIAS", "GLOBAL", "EXCLUDE_FROM_ALL", "WIN32", "MACOSX_BUNDLE", "PRIVATE",
        "PUBLIC", "HEADERS", "SOURCES", "PROPERTIES", "APPEND",
    };
    return noise.count( tok ) != 0;
}

// Link-list keywords that are not link items.
bool is_link_scope_kw( const std::string &tok )
{
    static const std::set<std::string> kws = {
        "PUBLIC", "PRIVATE", "INTERFACE", "debug", "optimized", "general",
        "LINK_PUBLIC", "LINK_PRIVATE", "LINK_INTERFACE_LIBRARIES",
    };
    return kws.count( tok ) != 0;
}

// Expand one raw CMake token against root/current-dir markers and the literal
// values of script-local set() variables. A token that *is* exactly ${VAR}
// expands to all of the variable's tokens; anything needing an unknown
// variable stays unresolved (the caller records it — fail-open).
void expand_wiring_token_impl( const std::string &tok,
                               const std::map<std::string, std::vector<std::string>> &vars,
                               std::vector<std::string> &out, bool &unknown,
                               std::set<std::string> &stack )
{
    if ( out.size() > 4096 )
    {
        unknown = true; // runaway list: stop expanding, fail open
        return;
    }
    if ( tok.size() > 3 && tok[0] == '$' && tok[1] == '{' && tok.back() == '}' )
    {
        const std::string var = tok.substr( 2, tok.size() - 3 );
        const auto it = vars.find( var );
        if ( it != vars.end() )
        {
            if ( !stack.insert( var ).second )
            {
                unknown = true; // self-referencing list(X APPEND … ${X}): stop
                return;
            }
            for ( const std::string &v : it->second )
                expand_wiring_token_impl( v, vars, out, unknown, stack );
            stack.erase( var );
            return;
        }
    }
    std::string norm;
    for ( std::size_t k = 0; k < tok.size(); )
    {
        if ( tok[k] == '$' && k + 1 < tok.size() && tok[k + 1] == '{' )
        {
            const std::size_t close = tok.find( '}', k );
            if ( close == std::string::npos )
            {
                norm.push_back( tok[k++] );
                continue;
            }
            const std::string var = tok.substr( k + 2, close - k - 2 );
            if ( var == "CMAKE_SOURCE_DIR" || var == "SICNU_SOURCE_DIR" || var == "PROJECT_SOURCE_DIR" )
                norm += "\x01R";
            else if ( var == "CMAKE_CURRENT_SOURCE_DIR" || var == "CMAKE_CURRENT_LIST_DIR" )
                norm += "\x01W";
            else
            {
                const auto it = vars.find( var );
                if ( it != vars.end() && it->second.size() == 1 )
                {
                    std::vector<std::string> nested;
                    bool nested_unknown = false;
                    expand_wiring_token_impl( it->second[0], vars, nested, nested_unknown, stack );
                    if ( nested_unknown || nested.empty() )
                    {
                        unknown = true;
                        return;
                    }
                    norm += nested[0];
                }
                else
                {
                    unknown = true;
                    return;
                }
            }
            k = close + 1;
        }
        else
            norm.push_back( tok[k++] );
    }
    out.push_back( norm );
}

void expand_wiring_token( const std::string &tok,
                          const std::map<std::string, std::vector<std::string>> &vars,
                          std::vector<std::string> &out, bool &unknown )
{
    std::set<std::string> stack;
    expand_wiring_token_impl( tok, vars, out, unknown, stack );
}

// Resolve an expanded token (with \x01R/\x01W markers) to an absolute path
// under the repo; "" when it still references unknown variables or escapes.
std::string resolve_expanded_path( const std::string &tok, const fs::path &root,
                                   const fs::path &script_dir )
{
    if ( tok.find( '$' ) != std::string::npos )
        return "";
    const std::string resolved = after_root_marker( tok, root, script_dir, true );
    if ( resolved.empty() )
        return "";
    fs::path p( resolved );
    if ( p.is_relative() )
        p = script_dir / p;
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical( p, ec );
    if ( ec )
        return "";
    if ( !is_under( canon, root ) )
        return ""; // escapes the repository: not checkable here
    return canon.string();
}

} // namespace

// Bound facts for one helper invocation or literal target definition.
struct BoundTarget
{
    std::vector<fs::path> sources;   // absolute, inside the repo
    std::vector<std::string> links;  // raw (unresolved names kept verbatim)
    bool unknown_link = false;
    bool automoc = false;
    bool qt_cmd = false;
    std::set<fs::path> include_dirs;
};

// Bind one helper invocation to concrete values. `bindings` maps parameter /
// keyword names to raw call-site tokens (resolved against the calling script
// at expand time). Recurses into helpers the body calls, depth-capped
// (sicnu_link_jsoncpp-style link helpers).
BoundTarget bind_helper_call( const RepoModel &m, const std::string &helper_name,
                              const std::map<std::string, std::vector<std::string>> &bindings,
                              const fs::path &root, const fs::path &calling_dir, int depth,
                              const std::map<std::string, std::vector<std::string>> &calling_vars )
{
    BoundTarget out;
    if ( depth > 3 )
        return out;
    const auto hit = m.helpers.find( helper_name );
    if ( hit == m.helpers.end() )
        return out;
    const Helper &h = hit->second;

    const auto expand = [&]( const std::string &raw, std::vector<std::string> &dst,
                             bool &unknown, bool paths ) {
        // ${PARAM} / ${KW_GROUP} → bound call-site tokens
        if ( raw.size() > 3 && raw[0] == '$' && raw[1] == '{' && raw.back() == '}' )
        {
            std::string name = raw.substr( 2, raw.size() - 3 );
            // parse_arguments exposes keyword groups as <PREFIX>_<KW>; call
            // sites pass the bare KW spelling (${D17_LIBS} ← LIBS x y)
            if ( !bindings.count( name ) && !h.parse_prefix.empty() &&
                 name.rfind( h.parse_prefix + "_", 0 ) == 0 )
                name = name.substr( h.parse_prefix.size() + 1 );
            const auto bit = bindings.find( name );
            if ( bit != bindings.end() )
            {
                for ( const std::string &v : bit->second )
                {
                    if ( !paths )
                    {
                        dst.push_back( v );
                        continue;
                    }
                    // call-site path tokens may compose calling-script
                    // variables (${D17_ENGINE_SOURCES}) on top of the
                    // root/current markers — expand them first
                    std::vector<std::string> ex;
                    bool vunk = false;
                    expand_wiring_token( v, calling_vars, ex, vunk );
                    for ( const std::string &e : ex )
                    {
                        const std::string rp = resolve_expanded_path( e, root, calling_dir );
                        if ( !rp.empty() )
                            dst.push_back( rp );
                        else if ( e.find( '$' ) != std::string::npos )
                            unknown = true;
                    }
                    if ( ex.empty() && vunk )
                        unknown = true;
                }
                return;
            }
            const auto vit = h.body_set_vars.find( name );
            if ( vit != h.body_set_vars.end() )
            {
                for ( const std::string &v : vit->second )
                {
                    if ( paths )
                    {
                        const std::string rp = resolve_expanded_path( v, root, calling_dir );
                        if ( !rp.empty() )
                            dst.push_back( rp );
                        else if ( v.find( '$' ) != std::string::npos )
                            unknown = true;
                    }
                    else
                        dst.push_back( v );
                }
                return;
            }
            if ( paths )
            {
                const std::string rp = resolve_expanded_path( raw, root, calling_dir );
                if ( !rp.empty() )
                {
                    dst.push_back( rp );
                    return;
                }
            }
            unknown = true;
            return;
        }
        if ( paths )
        {
            const std::string rp = resolve_expanded_path( raw, root, calling_dir );
            if ( !rp.empty() )
                dst.push_back( rp );
            else if ( raw.find( '$' ) != std::string::npos )
                unknown = true;
        }
        else
            dst.push_back( raw );
    };

    // sources: the creating command's argument tokens after ${PARAM}
    for ( const std::string &tok : h.body_target_args )
    {
        if ( is_source_noise( tok ) )
            continue;
        std::vector<std::string> ex;
        bool unk = false;
        expand( tok, ex, unk, /*paths=*/true );
        for ( const std::string &e : ex )
        {
            std::error_code ec;
            if ( fs::exists( fs::path( e ), ec ) )
                out.sources.push_back( fs::weakly_canonical( fs::path( e ), ec ) );
        }
    }

    // links
    for ( const std::vector<std::string> &list : h.body_link_lists )
    {
        for ( std::size_t i = 0; i < list.size(); ++i )
        {
            if ( i == 0 && ( list[i] == "${" + h.param + "}" ||
                             bindings.count( list[i].size() > 3
                                                 ? list[i].substr( 2, list[i].size() - 3 )
                                                 : std::string() ) ) )
                continue; // the subject target itself
            if ( is_link_scope_kw( list[i] ) )
                continue;
            std::vector<std::string> ex;
            bool unk = false;
            expand( list[i], ex, unk, /*paths=*/false );
            if ( unk )
                out.unknown_link = true;
            for ( const std::string &e : ex )
                out.links.push_back( e );
        }
    }

    // include dirs
    for ( const std::string &tok : h.body_include_dirs )
    {
        if ( tok == "PRIVATE" || tok == "PUBLIC" || tok == "INTERFACE" || tok == "SYSTEM" ||
             tok == "BEFORE" || tok == "${" + h.param + "}" )
            continue;
        std::vector<std::string> ex;
        bool unk = false;
        expand( tok, ex, unk, /*paths=*/true );
        for ( const std::string &e : ex )
            out.include_dirs.insert( fs::path( e ) );
    }

    out.automoc = h.body_automoc;
    out.qt_cmd = h.body_qt_cmd;

    // nested helper calls (e.g. sicnu_link_jsoncpp(${NAME})): bind the nested
    // call's first argument and merge the nested body's links into the subject
    for ( const auto &nh : h.body_helper_calls )
    {
        std::map<std::string, std::vector<std::string>> nested_bindings;
        const auto nested = m.helpers.find( nh.first );
        if ( nested == m.helpers.end() || nested->second.param.empty() )
            continue;
        std::vector<std::string> argval;
        bool unk = false;
        expand( nh.second, argval, unk, /*paths=*/false );
        if ( !argval.empty() )
            nested_bindings[nested->second.param] = argval;
        const BoundTarget nested_out =
            bind_helper_call( m, nh.first, nested_bindings, root, calling_dir, depth + 1,
                              calling_vars );
        out.links.insert( out.links.end(), nested_out.links.begin(), nested_out.links.end() );
        out.unknown_link = out.unknown_link || nested_out.unknown_link;
    }
    return out;
}

// Parse a helper call site's arguments into positional + keyword-group
// bindings (cmake convention: keywords are ALL-CAPS tokens).
std::map<std::string, std::vector<std::string>> parse_call_args( const Helper &h,
                                                                 const std::vector<std::string> &args )
{
    std::map<std::string, std::vector<std::string>> bindings;
    std::size_t pos_idx = 1; // params[0] is the helper name; declared params follow
    std::string current_kw;
    for ( const std::string &tok : args )
    {
        // keyword spelling: declared parse_arguments keyword, or (when the
        // helper uses parse_arguments at all) an ALL-CAPS token. Namespaced
        // values (Sicnu::x) and ${VAR} tokens are values, never keywords.
        const bool has_parse_kws = !h.multi_kws.empty() || !h.single_kws.empty();
        bool all_caps = !tok.empty() && tok[0] >= 'A' && tok[0] <= 'Z';
        for ( const char c : tok )
            if ( !( ( c >= 'A' && c <= 'Z' ) || c == '_' || isdigit( static_cast<unsigned char>( c ) ) ) )
                all_caps = false;
        const bool is_kw = h.multi_kws.count( tok ) || h.single_kws.count( tok ) ||
                           ( has_parse_kws && all_caps );
        if ( is_kw )
        {
            current_kw = tok;
            continue;
        }
        if ( !current_kw.empty() )
            bindings[current_kw].push_back( tok );
        else if ( pos_idx < h.params.size() )
            bindings[h.params[pos_idx++]].push_back( tok );
        else
            bindings["__positional"].push_back( tok );
    }
    return bindings;
}

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
        {
            const std::vector<std::string> toks = split_args( cmd.args );
            if ( ( cmd.name == "set" || cmd.name == "option" ) && !toks.empty() &&
                 valid_target_name( toks[0] ) )
                model.assigned_vars.insert( toks[0] );
            if ( cmd.name == "set" && toks.size() >= 2 && valid_target_name( toks[0] ) )
            {
                // keep ${…}-composed tokens too: the chain-resolution pass
                // below expands the resolvable ones (${CMAKE_SOURCE_DIR}/x,
                // references to earlier variables of the same script)
                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                    model.script_set_vars[s.path][toks[0]].push_back( toks[ti] );
            }
            else if ( cmd.name == "list" && toks.size() >= 3 && toks[0] == "APPEND" &&
                      valid_target_name( toks[1] ) )
            {
                // list(APPEND VAR a b …): the QGIS-style staged source lists
                for ( std::size_t ti = 2; ti < toks.size(); ++ti )
                    model.script_set_vars[s.path][toks[1]].push_back( toks[ti] );
            }
        }
    }

    // Resolve set()-variable chains within each script: values referencing
    // other variables of the same script expand to their literal tokens
    // (multi-file source lists are commonly composed in stages).
    for ( auto &sv : model.script_set_vars )
    {
        for ( int pass = 0; pass < 8; ++pass )
        {
            bool changed = false;
            for ( auto &entry : sv.second )
            {
                std::vector<std::string> next;
                next.reserve( entry.second.size() );
                for ( const std::string &tok : entry.second )
                {
                    if ( tok.find( '$' ) == std::string::npos )
                    {
                        next.push_back( tok );
                        continue;
                    }
                    std::vector<std::string> ex;
                    bool unk = false;
                    std::set<std::string> chain_stack{ entry.first };
                    expand_wiring_token_impl( tok, sv.second, ex, unk, chain_stack );
                    if ( ex.empty() )
                    {
                        next.push_back( tok );
                        continue;
                    }
                    for ( const std::string &e : ex )
                        next.push_back( e );
                    changed = true;
                }
                entry.second.swap( next );
            }
            if ( !changed )
                break;
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
                    h.params = params;
                    h.body_qt_cmd = tc.rfind( "qt_", 0 ) == 0;
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

                    // ── body facts for call-site binding ──
                    // cmake_parse_arguments(PREFIX "" "" "MULTI;KWS" ${ARGN})
                    // declares the keyword groups call sites use.
                    for ( const Cmd &bc : scan_commands( body ) )
                    {
                        if ( bc.name == "cmake_parse_arguments" )
                        {
                            std::vector<std::string> a = split_args( bc.args );
                            if ( !a.empty() && a[0] == "PARSE_ARGV" && a.size() >= 3 )
                                a.erase( a.begin(), a.begin() + 3 );
                            // strip the wrapping quotes of <options>/<single>/<multi>
                            for ( auto &t : a )
                            {
                                t.erase( std::remove( t.begin(), t.end(), '"' ), t.end() );
                            }
                            // <prefix> <options> <single> <multi> ${ARGN}
                            if ( a.size() >= 4 )
                            {
                                h.parse_prefix = a[0];
                                const auto read_kw_list = [&]( const std::string &semi ) {
                                    std::set<std::string> out;
                                    std::string cur;
                                    for ( const char c : semi )
                                    {
                                        if ( c == ';' )
                                        {
                                            if ( !cur.empty() )
                                                out.insert( cur );
                                            cur.clear();
                                        }
                                        else
                                            cur.push_back( c );
                                    }
                                    if ( !cur.empty() )
                                        out.insert( cur );
                                    return out;
                                };
                                h.single_kws = read_kw_list( a[2] );
                                h.multi_kws = read_kw_list( a[3] );
                            }
                        }
                        else if ( bc.name == "set" )
                        {
                            const std::vector<std::string> toks = split_args( bc.args );
                            if ( toks.size() >= 2 )
                                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                                    if ( toks[ti].find( '$' ) == std::string::npos )
                                        h.body_set_vars[toks[0]].insert( toks[ti] );
                        }
                        else if ( target_cmds.count( bc.name ) || bc.name == "target_link_libraries" ||
                                  bc.name == "target_include_directories" ||
                                  bc.name == "set_target_properties" || bc.name == "set_property" )
                        {
                            const std::vector<std::string> toks = split_args( bc.args );
                            if ( toks.empty() )
                                continue;
                            if ( target_cmds.count( bc.name ) )
                            {
                                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                                    h.body_target_args.push_back( toks[ti] );
                            }
                            else if ( bc.name == "target_link_libraries" )
                            {
                                h.body_link_lists.push_back( toks );
                            }
                            else if ( bc.name == "target_include_directories" )
                            {
                                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                                    h.body_include_dirs.push_back( toks[ti] );
                            }
                            else if ( bc.name == "set_target_properties" || bc.name == "set_property" )
                            {
                                for ( std::size_t ti = 0; ti + 1 < toks.size(); ++ti )
                                    if ( toks[ti] == "AUTOMOC" &&
                                         ( toks[ti + 1] == "ON" || toks[ti + 1] == "TRUE" ) )
                                        h.body_automoc = true;
                            }
                            else if ( toks[0].size() > 3 && toks[0].rfind( "${", 0 ) == 0 &&
                                      toks[0].back() == '}' )
                            {
                                // nested helper call (e.g. sicnu_link_jsoncpp(${NAME}));
                                // recorded by shape, resolved at bind time when
                                // every helper is known
                                h.body_helper_calls.push_back( { bc.name, toks[0] } );
                            }
                        }
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

    // ── Per-target wiring facts: literal commands first ──
    for ( std::size_t si = 0; si < model.scripts.size(); ++si )
    {
        const Script &s = model.scripts[si];
        if ( !s.main_tree || s.find_module )
            continue;
        const auto &vars = model.script_set_vars[s.path];
        for ( const Cmd &cmd : scan_commands( s.text ) )
        {
            const bool target_cmd = target_cmds.count( cmd.name ) != 0;
            const auto hit = model.helpers.find( cmd.name );
            const bool helper_cmd = hit != model.helpers.end();
            const std::vector<std::string> toks = split_args( cmd.args );
            if ( target_cmd )
            {
                if ( toks.empty() || !valid_target_name( toks[0] ) || is_source_token( toks[0] ) )
                    continue;
                if ( toks.size() >= 3 && cmd.name == "add_library" && toks[1] == "ALIAS" )
                {
                    model.alias_of[toks[0]] = toks[2];
                    continue;
                }
                TargetInfo &info = model.target_info[toks[0]];
                info.script = si;
                if ( cmd.name.rfind( "qt_", 0 ) == 0 )
                    info.automoc = 1;
                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                    if ( toks[ti] == "IMPORTED" )
                        info.imported = true;
                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                {
                    if ( is_source_noise( toks[ti] ) )
                        continue;
                    // expand variables FIRST: a ${VAR} token is not spelled
                    // with a source extension, so the extension filter only
                    // applies to the expansion results
                    std::vector<std::string> ex;
                    bool unk = false;
                    expand_wiring_token( toks[ti], vars, ex, unk );
                    for ( const std::string &e : ex )
                    {
                        if ( !is_source_token( e ) )
                            continue;
                        const std::string rp = resolve_expanded_path( e, model.root, s.path.parent_path() );
                        if ( !rp.empty() )
                            info.sources.insert( fs::path( rp ) );
                    }
                }
            }
            else if ( cmd.name == "target_sources" )
            {
                // target_sources(T vis x.cpp …) appends to an existing target
                if ( toks.empty() || !valid_target_name( toks[0] ) ||
                     toks[0].find( '$' ) != std::string::npos )
                    continue;
                auto info_it = model.target_info.find( toks[0] );
                if ( info_it == model.target_info.end() )
                    continue;
                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                {
                    if ( is_source_noise( toks[ti] ) )
                        continue;
                    std::vector<std::string> ex;
                    bool unk = false;
                    expand_wiring_token( toks[ti], vars, ex, unk );
                    for ( const std::string &e : ex )
                    {
                        if ( !is_source_token( e ) )
                            continue;
                        const std::string rp = resolve_expanded_path( e, model.root, s.path.parent_path() );
                        if ( !rp.empty() )
                            info_it->second.sources.insert( fs::path( rp ) );
                    }
                }
            }
            else if ( cmd.name == "target_link_libraries" || cmd.name == "link_libraries" )
            {
                std::size_t begin = 0;
                if ( cmd.name == "target_link_libraries" )
                {
                    if ( toks.empty() || !valid_target_name( toks[0] ) )
                        continue;
                    if ( toks[0].find( '$' ) != std::string::npos )
                        continue;
                    begin = 1;
                }
                const std::string subject = begin == 1 ? toks[0] : "";
                // only annotate targets the tree actually creates — these
                // commands also mention imported names (jsoncpp, Qt6::Network)
                // and must not fabricate in-tree entries for them
                auto subject_it = subject.empty()
                                      ? model.target_info.end()
                                      : model.target_info.find( subject );
                if ( begin == 1 && subject_it == model.target_info.end() )
                    continue;
                for ( std::size_t ti = begin; ti < toks.size(); ++ti )
                {
                    if ( is_link_scope_kw( toks[ti] ) )
                        continue;
                    if ( begin == 0 )
                    {
                        // directory-scope link_libraries(): every target this
                        // script defines inherits the dependency
                        for ( auto &entry : model.target_info )
                            if ( model.scripts[entry.second.script].path == s.path )
                            {
                                entry.second.links.push_back( toks[ti] );
                                entry.second.has_unknown_link =
                                    entry.second.has_unknown_link ||
                                    toks[ti].find( '$' ) != std::string::npos;
                            }
                        continue;
                    }
                    subject_it->second.links.push_back( toks[ti] );
                    subject_it->second.has_unknown_link =
                        subject_it->second.has_unknown_link ||
                        toks[ti].find( '$' ) != std::string::npos;
                }
            }
            else if ( cmd.name == "target_include_directories" )
            {
                if ( toks.empty() || toks[0].find( '$' ) != std::string::npos )
                    continue;
                auto info_it = model.target_info.find( toks[0] );
                if ( info_it == model.target_info.end() )
                    continue; // never fabricate entries for imported names
                for ( std::size_t ti = 1; ti < toks.size(); ++ti )
                {
                    if ( toks[ti] == "PRIVATE" || toks[ti] == "PUBLIC" || toks[ti] == "INTERFACE" ||
                         toks[ti] == "SYSTEM" || toks[ti] == "BEFORE" )
                        continue;
                    std::vector<std::string> ex;
                    bool unk = false;
                    expand_wiring_token( toks[ti], vars, ex, unk );
                    for ( const std::string &e : ex )
                    {
                        const std::string rp = resolve_expanded_path( e, model.root, s.path.parent_path() );
                        if ( !rp.empty() )
                            info_it->second.include_dirs.insert( fs::path( rp ) );
                    }
                }
            }
            else if ( cmd.name == "set_target_properties" || cmd.name == "set_property" )
            {
                // literal-name AUTOMOC evidence; set_target_properties may
                // name several targets before the PROPERTIES keyword
                std::vector<std::string> names;
                if ( cmd.name == "set_target_properties" )
                {
                    std::size_t props = toks.size();
                    for ( std::size_t ti = 0; ti < toks.size(); ++ti )
                        if ( toks[ti] == "PROPERTIES" )
                        {
                            props = ti;
                            break;
                        }
                    for ( std::size_t ti = 0; ti < props; ++ti )
                        if ( toks[ti].find( '$' ) == std::string::npos )
                            names.push_back( toks[ti] );
                }
                else // set_property(TARGET t1 t2 … PROPERTY …)
                {
                    bool collect = false;
                    for ( std::size_t ti = 0; ti < toks.size(); ++ti )
                    {
                        if ( toks[ti] == "TARGET" )
                        {
                            collect = true;
                            continue;
                        }
                        if ( toks[ti] == "PROPERTY" || toks[ti] == "PROPERTIES" )
                        {
                            collect = false;
                            continue;
                        }
                        if ( collect && toks[ti].find( '$' ) == std::string::npos )
                            names.push_back( toks[ti] );
                    }
                }
                for ( std::size_t ti = 0; ti + 1 < toks.size(); ++ti )
                {
                    if ( toks[ti] == "AUTOMOC" )
                    {
                        const int v = ( toks[ti + 1] == "ON" || toks[ti + 1] == "TRUE" ) ? 1 : 0;
                        for ( const std::string &n : names )
                        {
                            auto n_it = model.target_info.find( n );
                            if ( n_it != model.target_info.end() )
                                n_it->second.automoc = v;
                        }
                    }
                }
            }
            else if ( helper_cmd && s.main_tree )
            {
                // helper call site: bind body facts to the created target
                const Helper &h = hit->second;
                if ( toks.empty() )
                    continue;
                const std::map<std::string, std::vector<std::string>> bindings =
                    parse_call_args( h, toks );
                const auto name_it = bindings.find( h.param );
                if ( name_it == bindings.end() || name_it->second.size() != 1 ||
                     !valid_target_name( name_it->second[0] ) )
                    continue;
                const std::string subject = name_it->second[0];
                const BoundTarget bound =
                    bind_helper_call( model, cmd.name, bindings, model.root,
                                      s.path.parent_path(), 0, vars );
                TargetInfo &info = model.target_info[subject];
                info.script = si;
                info.helper_created = true;
                for ( const fs::path &p : bound.sources )
                    info.sources.insert( p );
                for ( const std::string &l : bound.links )
                    info.links.push_back( l );
                info.has_unknown_link = info.has_unknown_link || bound.unknown_link;
                if ( bound.automoc || bound.qt_cmd )
                    info.automoc = 1;
                info.include_dirs.insert( bound.include_dirs.begin(), bound.include_dirs.end() );
                model.targets.insert( subject );
            }
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

// ─────────────────────────────────────────────────────────────────────────────
// TU-level facts for embed-drift rules (post-#1295–#1313 incident classes).
//
// Rules 9–13 close the gap the script-level rules cannot see: tests that
// compile production .cpp files directly ("embeds"). Master keeps accumulating
// them (137 test targets embed 395 production sources), and CI repeatedly
// broke after merges because an embed target froze a stale copy of a module's
// dependency shape:
//
//   9.  the same directory add_subdirectory'ed twice in one configure
//       (directory-level CMP0002);
//   10. an embedded TU includes headers that belong to a named dependency
//       (jsoncpp, Qt6::Network) and uses their symbols, but the embed
//       target's link closure provides neither (#1298/#1302/#1309:
//       <json/json.h> via object_identity.h, <QHostInfo> in
//       workflow_run_lock.cpp);
//   11. an embedded TU includes a project header whose same-stem .cpp exists,
//       uses its demandable identifiers, and neither embeds that .cpp nor
//       links a target that compiles it (#1301/#1303/#1304:
//       workflow_run_lock.cpp, atomic_fs.cpp, fault_registry.cpp);
//   12. a test TU uses a Qt class from the recurring CI-breaker table without
//       a direct include (#1306/#1308–#1313: QJsonDocument, QTemporaryDir …)
//       — compiles on the author's transitive-include stack, breaks on
//       another compiler/Qt version;
//   13. an embedded TU touches a Q_OBJECT metaobject while the embed target
//       has AUTOMOC off and no AUTOMOC-on linked target compiles the same
//       class (the vtable-ref class; test_teaching_cockpit_smoke needed its
//       explicit AUTOMOC ON).
//
// Everything here is evidence-first: a rule fires only on facts readable in
// the tree (an include line, a Q_OBJECT macro, a stem-sibling .cpp). Where a
// fact is unknowable statically — unresolved variables, ambiguous include
// resolution, include-without-use — the check records `unknown` and stays
// silent, never guesses a green.
//
// Scope of 10/11/13: targets defined by scripts under tests/ (the embed
// surface). Production targets compile their own sources and have their own
// build lanes.
// ─────────────────────────────────────────────────────────────────────────────
namespace tu
{

// One C++ source/header, preprocessed the way wiring checks need it.
struct CppFile
{
    bool exists = false;
    std::string with_strings; // comments stripped, literals kept (include lines)
    std::string code;         // comments AND string/char literal contents stripped
};

std::map<fs::path, CppFile> &file_cache()
{
    static std::map<fs::path, CppFile> cache;
    return cache;
}

// Single-pass lexer: drops comments; two outputs — one keeping string
// literals (include paths live there), one with literal contents blanked
// (identifier scanning must not match string text).
CppFile lex_cpp( const fs::path &p )
{
    CppFile out;
    std::ifstream in( p );
    if ( !in.is_open() )
        return out;
    out.exists = true;
    const std::string raw{ std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
    enum class St { Code, LineComment, BlockComment, Str, Char } st = St::Code;
    std::string kept, blank;
    for ( std::size_t i = 0; i < raw.size(); ++i )
    {
        const char c = raw[i];
        const char next = i + 1 < raw.size() ? raw[i + 1] : '\0';
        switch ( st )
        {
            case St::Code:
                if ( c == '/' && next == '/' ) { st = St::LineComment; ++i; }
                else if ( c == '/' && next == '*' ) { st = St::BlockComment; ++i; }
                else if ( c == '"' ) { st = St::Str; blank += ' '; kept += c; }
                else if ( c == '\'' ) { st = St::Char; blank += ' '; kept += c; }
                else { kept += c; blank += c; }
                break;
            case St::LineComment:
                if ( c == '\n' ) { st = St::Code; kept += c; blank += c; }
                break;
            case St::BlockComment:
                if ( c == '*' && next == '/' ) { st = St::Code; ++i; }
                if ( c == '\n' ) { kept += c; blank += c; }
                break;
            case St::Str:
                if ( c == '\\' ) { ++i; blank += ' '; }
                else if ( c == '"' ) { st = St::Code; kept += c; blank += c; }
                else { kept += c; blank += ( c == '\n' ? '\n' : ' ' ); }
                break;
            case St::Char:
                if ( c == '\\' ) { ++i; blank += ' '; }
                else if ( c == '\'' ) { st = St::Code; kept += c; blank += c; }
                else { kept += c; blank += ( c == '\n' ? '\n' : ' ' ); }
                break;
        }
    }
    out.with_strings = std::move( kept );
    out.code = std::move( blank );
    return out;
}

const CppFile &load_cpp( const fs::path &p )
{
    auto &cache = file_cache();
    const auto it = cache.find( p );
    if ( it != cache.end() )
        return it->second;
    const CppFile f = lex_cpp( p );
    return cache.emplace( p, std::move( f ) ).first->second;
}

std::vector<std::string> angle_includes( const std::string &text )
{
    static const std::regex re( R"(^\s*#\s*include\s*<([^>]+)>)" );
    std::vector<std::string> out;
    std::istringstream in( text );
    std::string line;
    while ( std::getline( in, line ) )
    {
        std::smatch m;
        if ( std::regex_search( line, m, re ) )
            out.push_back( m[1].str() );
    }
    return out;
}

std::vector<std::string> quoted_includes( const std::string &text )
{
    static const std::regex re( R"re(^\s*#\s*include\s*"([^"]+)")re" );
    std::vector<std::string> out;
    std::istringstream in( text );
    std::string line;
    while ( std::getline( in, line ) )
    {
        std::smatch m;
        if ( std::regex_search( line, m, re ) )
            out.push_back( m[1].str() );
    }
    return out;
}

// Quoted-include resolution in compiler search order: the includer's own
// directory first, then the include roots (repo convention: src/). Returns
// the first candidate that exists; nullopt when none does.
std::optional<fs::path> resolve_quoted( const fs::path &includer_dir, const std::string &inc,
                                        const std::set<fs::path> &search_roots )
{
    const fs::path rel( inc );
    if ( rel.is_absolute() )
        return fs::exists( rel ) ? std::optional<fs::path>( rel ) : std::nullopt;
    std::error_code ec;
    fs::path cand = fs::weakly_canonical( includer_dir / rel, ec );
    if ( !ec && fs::exists( cand ) )
        return cand;
    for ( const fs::path &root_dir : search_roots )
    {
        cand = fs::weakly_canonical( root_dir / rel, ec );
        if ( !ec && fs::exists( cand ) )
            return cand;
    }
    return std::nullopt;
}

// Identifiers worth demanding a body for, with the kind of demand:
//   FunctionDecl — declared ";"-terminated in the header: a TU that mentions
//                  one needs its out-of-line definition (inline/header-defined
//                  functions are excluded — they demand nothing);
//   Tag          — class/struct/enum name: only construction / new /
//                  make_unique/make_shared proves an out-of-line need, so
//                  bare type mentions (parameters, pointers, nested types)
//                  are not enough.
struct Demandable
{
    enum class Kind { FunctionDecl, Tag };
    std::map<std::string, Kind> by_name;
};

Demandable demandable_identifiers( const std::string &code )
{
    Demandable out;
    // tag declarations only: `class X {` / `class X :` / line-end opening —
    // `class X;` is a forward declaration and names an outside entity; the
    // (?!:) keeps `class A::B` elaborated declarations (outside types) from
    // registering a bogus one-word tag
    // enums are header-complete: using one never needs a body
    static const std::regex tag_re( R"(\b(?:class|struct)\s+([A-Za-z_]\w*)\s*(?:\{|\z|:(?!:)))" );
    for ( std::sregex_iterator it( code.begin(), code.end(), tag_re ), end; it != end; ++it )
        out.by_name.emplace( ( *it )[1].str(), Demandable::Kind::Tag );
    // function declarations: name( … ) followed by `;` (no body in the header)
    static const std::regex fn_re( R"(\b([a-z][A-Za-z0-9_]{5,})\s*\()" );
    static const std::set<std::string> kw = {
        "if", "while", "for", "switch", "return", "sizeof", "catch", "defined",
        "else", "do", "case", "alignof", "noexcept", "decltype", "static_assert",
    };
    for ( std::sregex_iterator it( code.begin(), code.end(), fn_re ), end; it != end; ++it )
    {
        const std::string name = ( *it )[1].str();
        if ( kw.count( name ) )
            continue;
        const bool snake = name.find( '_' ) != std::string::npos;
        if ( !snake && name.size() < 10 )
            continue; // generic short spellings collide with unrelated classes
        // walk to the balanced closing paren of this declaration
        const std::size_t open = code.find( '(', ( *it ).position() + name.size() );
        std::size_t depth = 0, i = open;
        for ( ; i < code.size(); ++i )
        {
            if ( code[i] == '(' )
                ++depth;
            else if ( code[i] == ')' && --depth == 0 )
                break;
        }
        std::size_t j = i + 1;
        while ( j < code.size() && isspace( static_cast<unsigned char>( code[j] ) ) )
            ++j;
        // noexcept/override/const qualifiers may sit before the `;`
        std::size_t k = j;
        while ( k < code.size() && ( is_word_char( code[k] ) || isspace( static_cast<unsigned char>( code[k] ) ) ) )
            ++k;
        bool body = k < code.size() && code[k] == '{';
        if ( !body )
            out.by_name.emplace( name, Demandable::Kind::FunctionDecl );
    }
    return out;
}

// Everything the drift rules need about one TU: project headers reachable via
// quoted includes (resolved against the search roots), the angle includes of
// each reached file (attributed — a need exists only where the include AND a
// real symbol use sit in the same file), Q_OBJECT presence, and the
// demandable identifiers of each header.
struct Closure
{
    std::set<fs::path> project_headers; // reached headers, not the TU itself
    std::set<fs::path> files;           // TU + all reached headers
    std::map<fs::path, std::set<std::string>> angle_by_file;
    bool has_qobject = false;
    std::map<fs::path, Demandable> identifiers_by_header;
};

Closure analyze_tu( const fs::path &tu_path, const std::set<fs::path> &search_roots )
{
    static std::map<std::pair<fs::path, std::string>, Closure> memo;
    const std::string roots_key = [&] {
        std::string s;
        for ( const fs::path &r : search_roots )
            s += r.string() + ";";
        return s;
    }();
    const auto memo_it = memo.find( { tu_path, roots_key } );
    if ( memo_it != memo.end() )
        return memo_it->second;

    Closure out;
    std::set<fs::path> visited{ tu_path };
    std::vector<fs::path> queue{ tu_path };
    while ( !queue.empty() && out.files.size() < 256 )
    {
        const fs::path cur = queue.back();
        queue.pop_back();
        const CppFile &f = load_cpp( cur );
        if ( !f.exists )
            continue;
        out.files.insert( cur );
        if ( f.code.find( "Q_OBJECT" ) != std::string::npos ||
             f.code.find( "Q_GADGET" ) != std::string::npos ||
             f.code.find( "Q_NAMESPACE" ) != std::string::npos )
            out.has_qobject = true;
        for ( const std::string &a : angle_includes( f.with_strings ) )
            out.angle_by_file[cur].insert( a );
        for ( const std::string &q : quoted_includes( f.with_strings ) )
        {
            const auto resolved = resolve_quoted( cur.parent_path(), q, search_roots );
            if ( !resolved || visited.count( *resolved ) )
                continue;
            visited.insert( *resolved );
            if ( resolved->extension() == ".h" || resolved->extension() == ".hpp" )
            {
                out.project_headers.insert( *resolved );
                const CppFile &hf = load_cpp( *resolved );
                const Demandable ids = demandable_identifiers( hf.code );
                if ( !ids.by_name.empty() )
                    out.identifiers_by_header.emplace( *resolved, ids );
            }
            queue.push_back( *resolved );
        }
    }
    return memo.emplace( std::make_pair( tu_path, roots_key ), std::move( out ) ).first->second;
}

bool mentions_identifier( const std::string &code, const std::string &ident )
{
    if ( ident.empty() )
        return false;
    for ( std::size_t pos = code.find( ident ); pos != std::string::npos;
          pos = code.find( ident, pos + 1 ) )
    {
        const std::size_t end = pos + ident.size();
        const bool left_ok = pos == 0 || !is_word_char( code[pos - 1] );
        const bool right_ok = end >= code.size() || !is_word_char( code[end] );
        if ( left_ok && right_ok )
            return true;
    }
    return false;
}

} // namespace tu

// Link names reachable from t's link lists, followed through in-tree targets
// and aliases down to the leaves (external/namespaced names). `unknown` when
// any visited target has an unresolvable link token — the closure then proves
// nothing either way.
std::set<std::string> link_closure( const RepoModel &m, const std::string &t, bool &unknown )
{
    std::set<std::string> leaves;
    std::set<std::string> visited;
    std::vector<std::string> queue{ t };
    while ( !queue.empty() )
    {
        const std::string cur = queue.back();
        queue.pop_back();
        if ( !visited.insert( cur ).second )
            continue;
        const std::string real = m.alias_of.count( cur ) ? m.alias_of.at( cur ) : cur;
        const auto info = m.target_info.find( real );
        if ( info == m.target_info.end() || info->second.imported )
        {
            // absent from the model, or an IMPORTED target created in-tree
            // (the canonical `jsoncpp` fallback): a link leaf either way
            leaves.insert( cur );
            continue;
        }
        if ( info->second.has_unknown_link )
            unknown = true;
        for ( const std::string &tok : info->second.links )
        {
            if ( tok.find( '$' ) != std::string::npos )
            {
                unknown = true;
                continue;
            }
            const std::string r2 = m.alias_of.count( tok ) ? m.alias_of.at( tok ) : tok;
            if ( m.target_info.count( r2 ) )
                queue.push_back( r2 );
            else
                leaves.insert( tok );
        }
    }
    return leaves;
}

// In-tree target names reachable from t (alias-resolved, excluding t itself).
std::set<std::string> link_closure_targets( const RepoModel &m, const std::string &t )
{
    std::set<std::string> out;
    std::set<std::string> visited;
    std::vector<std::string> queue{ t };
    while ( !queue.empty() )
    {
        const std::string cur = queue.back();
        queue.pop_back();
        if ( !visited.insert( cur ).second )
            continue;
        const std::string real = m.alias_of.count( cur ) ? m.alias_of.at( cur ) : cur;
        if ( real != t && m.target_info.count( real ) )
            out.insert( real );
        const auto info = m.target_info.find( real );
        if ( info == m.target_info.end() )
            continue;
        for ( const std::string &tok : info->second.links )
        {
            if ( tok.find( '$' ) != std::string::npos )
                continue;
            const std::string r2 = m.alias_of.count( tok ) ? m.alias_of.at( tok ) : tok;
            if ( m.target_info.count( r2 ) )
                queue.push_back( r2 );
        }
    }
    return out;
}

// map<compiled .cpp path, target names compiling it>
std::map<fs::path, std::set<std::string>> source_owners( const RepoModel &m )
{
    std::map<fs::path, std::set<std::string>> out;
    for ( const auto &entry : m.target_info )
        for ( const fs::path &s : entry.second.sources )
            out[s].insert( entry.first );
    return out;
}

bool tests_scoped( const RepoModel &m, const TargetInfo &info )
{
    return info.script < m.scripts.size() && is_under( m.scripts[info.script].path, m.root / "tests" );
}

// Rule 9 — duplicate add_subdirectory (directory-level CMP0002 class). Two
// calls resolving to the same directory that can both execute in one
// configure process the target tree twice.
std::vector<std::string> check_duplicate_subdirs( const RepoModel &m )
{
    struct Site
    {
        std::size_t script;
        std::size_t line;
    };
    std::map<fs::path, std::vector<Site>> sites;
    for ( std::size_t si = 0; si < m.scripts.size(); ++si )
    {
        const Script &s = m.scripts[si];
        if ( !s.main_tree || s.find_module )
            continue;
        for ( std::sregex_iterator it( s.text.begin(), s.text.end(), subdir_regex() ), end;
              it != end; ++it )
        {
            const auto line = 1 + static_cast<std::size_t>( std::count(
                s.text.begin(),
                s.text.begin() + static_cast<std::ptrdiff_t>( ( *it ).position() ), '\n' ) );
            if ( in_dead_branch( s, line, m.assigned_vars ) )
                continue;
            const auto resolved = resolve_subdir_operand( ( *it )[1].str(), s.path.parent_path() );
            if ( resolved )
                sites[*resolved].push_back( { si, line } );
        }
    }
    std::vector<std::string> out;
    for ( const auto &entry : sites )
    {
        const std::vector<Site> &v = entry.second;
        if ( v.size() < 2 )
            continue;
        for ( std::size_t i = 0; i < v.size(); ++i )
            for ( std::size_t j = i + 1; j < v.size(); ++j )
            {
                const Script &sa = m.scripts[v[i].script];
                const Script &sb = m.scripts[v[j].script];
                if ( in_dead_branch( sa, v[i].line, m.assigned_vars ) ||
                     in_dead_branch( sb, v[j].line, m.assigned_vars ) )
                    continue;
                if ( v[i].script == v[j].script && branch_separated( sa, v[i].line, v[j].line ) )
                    continue;
                out.push_back( "duplicate add_subdirectory (CMP0002 class): " + entry.first.string() +
                               "\n  " + sa.path.string() + ":" + std::to_string( v[i].line ) +
                               "\n  " + sb.path.string() + ":" + std::to_string( v[j].line ) );
                j = v.size();
                break;
            }
    }
    return out;
}

// Rule 10 — an embedded production TU's named-dependency includes must be
// provided by the embed target's link closure (#1298/#1302/#1309 class).
//
// Evidence table: header spelling → the link names that provide it. Only
// include lines of the embedded TU's own object fire the rule, and only when
// the same file really uses the dependency's symbols — include-without-use is
// not a link need (the CI-green atomic_fs.cpp embeds carry <json/json.h>
// without touching Json::). One dependency may have several exported link
// spellings (jsoncpp upstream exports `jsoncpp` and `jsoncpp_lib`); the
// family is satisfied by any of them.
struct RequiredLib
{
    std::string name; // presentation name
    std::set<std::string> spellings;
};

std::optional<RequiredLib> required_lib_for_include( const std::string &inc )
{
    if ( inc.rfind( "json/", 0 ) == 0 )
        return RequiredLib{ "jsoncpp", { "jsoncpp", "jsoncpp_lib" } };
    if ( inc.rfind( "QtNetwork/", 0 ) == 0 || inc == "QHostInfo" )
        return RequiredLib{ "Qt6::Network", { "Qt6::Network" } };
    return std::nullopt;
}

// Symbol spellings that prove the dependency is really used in the file that
// carries the include.
bool uses_lib_symbols( const std::string &code, const std::string &inc )
{
    if ( inc.rfind( "json/", 0 ) == 0 )
        return code.find( "Json::" ) != std::string::npos;
    if ( inc.rfind( "QtNetwork/", 0 ) == 0 || inc == "QHostInfo" )
    {
        const std::string cls =
            inc == "QHostInfo" ? inc : inc.substr( std::string( "QtNetwork/" ).size() );
        return !cls.empty() && tu::mentions_identifier( code, cls );
    }
    return false;
}

std::vector<std::string> check_embed_link_requirements( const RepoModel &m )
{
    std::vector<std::string> out;
    for ( const auto &entry : m.target_info )
    {
        const std::string &name = entry.first;
        const TargetInfo &info = entry.second;
        if ( !tests_scoped( m, info ) || info.sources.empty() )
            continue;
        std::set<fs::path> roots = info.include_dirs;
        roots.insert( m.root / "src" );
        bool closure_unknown = false;
        const std::set<std::string> leaves = link_closure( m, name, closure_unknown );
        std::set<std::string> missing;
        std::map<std::string, std::string> evidence;
        for ( const fs::path &src : info.sources )
        {
            if ( !is_under( src, m.root / "src" ) )
                continue; // embed rules cover production sources only
            const tu::Closure c = tu::analyze_tu( src, roots );
            // the TU's include needs live in its own text or its stem header
            // (range_cache_disk.cpp's <json/json.h> sits in
            // range_cache_disk.h) — one compilation unit. Includes reached
            // through unrelated headers demand nothing: their inline
            // functions may never be called by this TU.
            const fs::path stem_header = src.parent_path() / ( src.stem().string() + ".h" );
            for ( const auto &fa : c.angle_by_file )
            {
                const bool own_unit = fs::weakly_canonical( fa.first ) ==
                                              fs::weakly_canonical( src ) ||
                                      fs::weakly_canonical( fa.first ) ==
                                          fs::weakly_canonical( stem_header );
                if ( !own_unit )
                    continue;
                const tu::CppFile &ff = tu::load_cpp( fa.first );
                for ( const std::string &inc : fa.second )
                {
                    const auto req = required_lib_for_include( inc );
                    // When even the over-approximated closure (every link
                    // edge followed) lacks the dependency, the embed is
                    // certainly missing it — that is the #1298/#1302 class.
                    // Where the closure DOES contain it, provability ends:
                    // whether that transit is PUBLIC-reliable is not
                    // statically decidable (sicnu_link_jsoncpp exists because
                    // it sometimes is not), so the check records unknown and
                    // stays silent rather than false-red.
                    if ( !req || closure_unknown || missing.count( req->name ) )
                        continue;
                    if ( !uses_lib_symbols( ff.code, inc ) )
                        continue;
                    const std::set<std::string> &pool = leaves;
                    bool provided = false;
                    for ( const std::string &sp : req->spellings )
                        if ( pool.count( sp ) )
                        {
                            provided = true;
                            break;
                        }
                    if ( !provided )
                    {
                        missing.insert( req->name );
                        evidence.emplace( req->name,
                                          src.filename().string() + " includes <" + inc + "> and uses it" );
                    }
                }
            }
        }
        for ( const std::string &req : missing )
            out.push_back( "embedded source needs [" + req + "] but link closure of " + name +
                           " provides neither it nor a wrapper: " + evidence[req] );
    }
    return out;
}

// Rule 11 — an embedded TU that includes a project header AND uses its
// demandable identifiers must get the header's implementation: either embed
// the same-stem .cpp itself, or link a target that compiles it
// (#1301/#1303/#1304 class). Identifier attribution is per header, so an
// unrelated `size()` mention cannot fake usage of a header that declares one.
std::vector<std::string> check_embed_companion_sources( const RepoModel &m )
{
    const std::map<fs::path, std::set<std::string>> owners = source_owners( m );
    std::vector<std::string> out;
    for ( const auto &entry : m.target_info )
    {
        const std::string &name = entry.first;
        const TargetInfo &info = entry.second;
        if ( !tests_scoped( m, info ) || info.sources.empty() )
            continue;
        std::set<fs::path> roots = info.include_dirs;
        roots.insert( m.root / "src" );
        for ( const fs::path &src : info.sources )
        {
            if ( !is_under( src, m.root / "src" ) )
                continue;
            const tu::Closure c = tu::analyze_tu( src, roots );
            const tu::CppFile &tu_file = tu::load_cpp( src );
            for ( const auto &hi : c.identifiers_by_header )
            {
                const fs::path &h = hi.first;
                // use-evidence, per header: a declared-not-defined function
                // mentioned anywhere, or a type tag used in a form that needs
                // out-of-line code (construction, new, make_unique/shared)
                bool used = false;
                for ( const auto &id : hi.second.by_name )
                {
                    if ( !tu::mentions_identifier( tu_file.code, id.first ) )
                        continue;
                    if ( id.second == tu::Demandable::Kind::FunctionDecl )
                    {
                        used = true;
                        break;
                    }
                    const auto usage = [&]( const std::string &pat ) {
                        return tu::mentions_identifier( tu_file.code, pat );
                    };
                    if ( usage( "new " + id.first ) || usage( "make_unique<" + id.first ) ||
                         usage( "make_shared<" + id.first ) || usage( id.first + "::" ) )
                    {
                        used = true;
                        break;
                    }
                    // direct construction: `Tag name(`
                    const std::string decl1 = id.first + " ";
                    for ( std::size_t pos = tu_file.code.find( decl1 ); pos != std::string::npos;
                          pos = tu_file.code.find( decl1, pos + 1 ) )
                    {
                        std::size_t e = pos + decl1.size();
                        while ( e < tu_file.code.size() && is_word_char( tu_file.code[e] ) )
                            ++e;
                        while ( e < tu_file.code.size() &&
                                ( tu_file.code[e] == ' ' || tu_file.code[e] == '\t' ||
                                  tu_file.code[e] == '\n' ) )
                            ++e;
                        if ( e < tu_file.code.size() && tu_file.code[e] == '(' )
                        {
                            used = true;
                            break;
                        }
                    }
                    if ( used )
                        break;
                }
                if ( !used )
                    continue;
                // companion: same-stem body file next to the header
                fs::path body;
                for ( const char *ext : { ".cpp", ".cc", ".cxx" } )
                {
                    const fs::path cand = h.parent_path() / ( h.stem().string() + ext );
                    std::error_code ec;
                    if ( fs::exists( cand, ec ) )
                    {
                        body = cand;
                        break;
                    }
                }
                if ( body.empty() )
                    continue; // header-only: nothing to provide
                if ( info.sources.count( fs::weakly_canonical( body ) ) )
                    continue; // embedded directly
                // provided via a linked target that compiles it?
                bool linked = false;
                for ( const std::string &dep : link_closure_targets( m, name ) )
                {
                    const auto dep_info = m.target_info.find( dep );
                    if ( dep_info != m.target_info.end() &&
                         dep_info->second.sources.count( fs::weakly_canonical( body ) ) )
                    {
                        linked = true;
                        break;
                    }
                }
                if ( linked )
                    continue;
                const auto owner_it = owners.find( fs::weakly_canonical( body ) );
                std::string owner_note;
                if ( owner_it != owners.end() && !owner_it->second.empty() )
                {
                    owner_note = " (compiled by: ";
                    bool first = true;
                    for ( const std::string &o : owner_it->second )
                    {
                        if ( !first )
                            owner_note += ", ";
                        owner_note += o;
                        first = false;
                    }
                    owner_note += ")";
                }
                out.push_back( "embedded " + src.filename().string() + " uses " + h.filename().string() +
                               " but its body " + body.filename().string() +
                               " is neither embedded nor linked by " + name + owner_note );
            }
        }
    }
    return out;
}

// Rule 12 — a test TU using a class from the recurring CI-breaker table must
// include it (directly or via a forward declaration). The include may exist
// transitively on the author's machine and vanish on another Qt/compiler.
std::vector<std::string> check_test_tu_includes( const RepoModel &m )
{
    static const std::set<std::string> kQtClasses = {
        "QJsonDocument", "QJsonObject", "QJsonArray", "QJsonValue", "QJsonParseError",
        "QTemporaryDir", "QTemporaryFile",
        "QNetworkAccessManager", "QNetworkReply", "QNetworkRequest",
        "QHostInfo", "QLocalServer", "QLocalSocket", "QLockFile",
        "QSignalSpy",
    };
    const auto stem_lower = []( const std::string &inc ) {
        std::string s = fs::path( inc ).filename().string();
        const std::size_t dot = s.rfind( '.' );
        if ( dot != std::string::npos )
            s = s.substr( 0, dot );
        for ( char &c : s )
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        return s;
    };
    const auto mentions_word = []( const std::string &haystack, const std::string &word ) {
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
    };
    std::vector<std::string> out;
    for ( auto it = fs::recursive_directory_iterator( m.root / "tests" ),
               end = fs::recursive_directory_iterator();
          it != end; ++it )
    {
        if ( it->is_directory() && is_excluded_dir( it->path().filename().string() ) )
        {
            it.disable_recursion_pending();
            continue;
        }
        if ( !it->is_regular_file() || it->path().extension() != ".cpp" )
            continue;
        // the gate itself spells the table's class names as data — exempt it
        if ( it->path().filename() == "test_build_wiring_drift.cpp" )
            continue;
        const tu::CppFile &f = tu::load_cpp( it->path() );
        if ( !f.exists )
            continue;
        std::set<std::string> lowered_stems;
        for ( const std::string &inc : tu::angle_includes( f.with_strings ) )
            lowered_stems.insert( stem_lower( inc ) );
        for ( const std::string &inc : tu::quoted_includes( f.with_strings ) )
            lowered_stems.insert( stem_lower( inc ) );
        for ( const std::string &cls : kQtClasses )
        {
            if ( !tu::mentions_identifier( f.code, cls ) )
                continue;
            if ( lowered_stems.count( stem_lower( cls ) ) )
                continue;
            // forward declaration also satisfies the need (pointer/ref use)
            if ( mentions_word( f.code, std::string( "class " ) + cls ) ||
                 mentions_word( f.code, std::string( "struct " ) + cls ) )
                continue;
            out.push_back( "test TU uses " + cls + " without including <" + cls + ">: " +
                           it->path().string() );
        }
    }
    return out;
}

// Rule 13 — a moc-needing embed has moc output available: AUTOMOC on the
// embed target, or a linked AUTOMOC-on target compiling the same class
// (the vtable-ref class; test_teaching_admin_dock_smoke borrows its moc from
// Sicnu::teaching_admin, test_teaching_cockpit_smoke turns AUTOMOC ON).
//
// "Moc-needing" is deliberately narrow: a Q_OBJECT/Q_GADGET/Q_NAMESPACE macro
// in the embedded TU's own text or its stem header, PLUS code that touches
// the metaobject through spellings that always go through it (emit,
// staticMetaObject, qobject_cast). Bare `tr(` is excluded on purpose — it is
// also the spelling of QObject::tr and inherited-static tr, which need no own
// moc — and bare pointer-includes of Q_OBJECT headers link fine without moc.
bool touches_metaobject( const std::string &code )
{
    static const std::regex re( R"(\b(?:emit|staticMetaObject|qobject_cast)\b)" );
    return std::regex_search( code, re );
}

std::vector<std::string> check_embed_automoc( const RepoModel &m )
{
    std::vector<std::string> out;
    for ( const auto &entry : m.target_info )
    {
        const std::string &name = entry.first;
        const TargetInfo &info = entry.second;
        if ( !tests_scoped( m, info ) || info.sources.empty() )
            continue;
        if ( info.automoc == 1 )
            continue;
        std::set<fs::path> roots = info.include_dirs;
        roots.insert( m.root / "src" );
        std::set<std::string> providers;
        for ( const std::string &dep : link_closure_targets( m, name ) )
        {
            const auto dep_info = m.target_info.find( dep );
            if ( dep_info != m.target_info.end() && dep_info->second.automoc == 1 )
                providers.insert( dep );
        }
        const auto provides_header = [&]( const std::string &provider, const fs::path &h ) {
            const auto dep_info = m.target_info.find( provider );
            if ( dep_info == m.target_info.end() )
                return false;
            const fs::path canon_h = fs::weakly_canonical( h );
            if ( dep_info->second.sources.count( canon_h ) )
                return true;
            const fs::path body = h.parent_path() / ( h.stem().string() + ".cpp" );
            if ( dep_info->second.sources.count( fs::weakly_canonical( body ) ) )
                return true;
            // a provider-compiled TU that includes the class header gets the
            // moc generated into the provider by AUTOMOC
            std::set<fs::path> proot = dep_info->second.include_dirs;
            proot.insert( m.root / "src" );
            for ( const fs::path &s : dep_info->second.sources )
                if ( tu::analyze_tu( s, proot ).project_headers.count( canon_h ) )
                    return true;
            return false;
        };
        for ( const fs::path &src : info.sources )
        {
            if ( !is_under( src, m.root / "src" ) )
                continue;
            const tu::CppFile &sf = tu::load_cpp( src );
            if ( !touches_metaobject( sf.code ) )
                continue;
            // the metaobject usage is moc-relevant only for Q_OBJECT-bearing
            // code: the TU itself, or the same-stem class header
            fs::path stem_header = src.parent_path() / ( src.stem().string() + ".h" );
            const tu::CppFile &hf = tu::load_cpp( stem_header );
            const bool qobject_here =
                sf.code.find( "Q_OBJECT" ) != std::string::npos ||
                sf.code.find( "Q_GADGET" ) != std::string::npos ||
                sf.code.find( "Q_NAMESPACE" ) != std::string::npos;
            const bool qobject_stem =
                hf.exists && ( hf.code.find( "Q_OBJECT" ) != std::string::npos ||
                               hf.code.find( "Q_GADGET" ) != std::string::npos ||
                               hf.code.find( "Q_NAMESPACE" ) != std::string::npos );
            if ( !qobject_here && !qobject_stem )
                continue;
            bool covered = false;
            if ( hf.exists )
                for ( const std::string &p : providers )
                    if ( provides_header( p, stem_header ) )
                    {
                        covered = true;
                        break;
                    }
            if ( covered )
                continue;
            out.push_back( "embedded " + src.filename().string() +
                           " touches a Q_OBJECT metaobject but " + name +
                           " has AUTOMOC off and no AUTOMOC-on linked target compiles the class" );
            break; // one report per target is enough to route the fix
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

TEST_CASE( "build wiring drift: no duplicate add_subdirectory", "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_duplicate_subdirs( live_model() );
    INFO( "duplicate subdirs:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: embedded sources' named dependencies are linked",
           "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_embed_link_requirements( live_model() );
    INFO( "embed link requirements:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: embedded sources' companion bodies are provided",
           "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_embed_companion_sources( live_model() );
    INFO( "embed companion bodies:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: test TUs include the Qt classes they use",
           "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_test_tu_includes( live_model() );
    INFO( "missing direct includes:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
    REQUIRE( v.empty() );
}

TEST_CASE( "build wiring drift: Q_OBJECT embeds have moc output available",
           "[build_wiring][contract][drift]" )
{
    const std::vector<std::string> v = wiring::check_embed_automoc( live_model() );
    INFO( "automoc drift:\n" << [&] { std::string s; for ( const auto &e : v ) s += e + "\n"; return s; }() );
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
        REQUIRE( wiring::check_duplicate_subdirs( m ).empty() );
        REQUIRE( wiring::check_embed_link_requirements( m ).empty() );
        REQUIRE( wiring::check_embed_companion_sources( m ).empty() );
        REQUIRE( wiring::check_test_tu_includes( m ).empty() );
        REQUIRE( wiring::check_embed_automoc( m ).empty() );
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
    SECTION( "rule 9 kills a duplicate add_subdirectory; accepts branch-separated forms" )
    {
        const Fixture fx = Fixture::make( "dup_subdir" );
        fx.append( "CMakeLists.txt", "add_subdirectory(src/lib)\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_subdirs( fx.model() ),
                                    "duplicate add_subdirectory" ) == 1 );

        // the same directory from exclusive branches of one chain cannot
        // both execute — legal
        Fixture fx2 = Fixture::make( "dup_subdir_branch" );
        fx2.overwrite( "CMakeLists.txt",
                       "cmake_minimum_required(VERSION 3.20)\nproject(fx CXX)\n"
                       "add_subdirectory(tests)\n"
                       "if(FOO)\nadd_subdirectory(src/lib)\nelse()\nadd_subdirectory(src/lib)\nendif()\n" );
        REQUIRE( wiring::check_duplicate_subdirs( fx2.model() ).empty() );

        // duplicates across scripts (root and a subdir script) collide too
        Fixture fx3 = Fixture::make( "dup_subdir_cross" );
        fx3.append( "src/lib/CMakeLists.txt",
                    "if(FOO)\nadd_subdirectory(sub)\nelse()\nadd_subdirectory(sub)\nendif()\n" );
        fx3.append( "CMakeLists.txt", "add_subdirectory(src/lib/sub)\n" );
        REQUIRE( count_with_prefix( wiring::check_duplicate_subdirs( fx3.model() ),
                                    "duplicate add_subdirectory" ) == 1 );
    }
    SECTION( "rule 10 kills an embed missing a required external link; accepts direct and transitive provision" )
    {
        const auto json_fixture = []( const std::string &name ) {
            Fixture f = Fixture::make( name );
            f.overwrite( "src/lib/a.cpp",
                         "#include <json/json.h>\nint fx_a() { Json::Value v; return v.size(); }\n" );
            f.overwrite( "tests/CMakeLists.txt",
                         "add_executable(test_req test_req.cpp ${CMAKE_SOURCE_DIR}/src/lib/a.cpp)\n" );
            f.append( "tests/test_req.cpp", "int main(){return 0;}\n" );
            return f;
        };
        // no link at all — red (the #1298/#1309 shape)
        const Fixture fx = json_fixture( "reqlink" );
        REQUIRE( count_with_prefix( wiring::check_embed_link_requirements( fx.model() ),
                                    "embedded source needs [jsoncpp]" ) == 1 );

        // the jsoncpp spelling satisfies the same requirement (#1298 used the
        // sicnu_link_jsoncpp helper, which links `jsoncpp`)
        Fixture fx2 = json_fixture( "reqlink_ok" );
        fx2.append( "tests/CMakeLists.txt", "target_link_libraries(test_req PRIVATE jsoncpp)\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx2.model() ).empty() );

        Fixture fx2b = json_fixture( "reqlink_ok_alias" );
        fx2b.append( "tests/CMakeLists.txt", "target_link_libraries(test_req PRIVATE jsoncpp_lib)\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx2b.model() ).empty() );

        // transitive provision: links a lib that links jsoncpp — accepted,
        // because the closure here is the over-approximation (every edge
        // followed): if even it lacked jsoncpp the link would certainly fail
        Fixture fx3 = json_fixture( "reqlink_trans" );
        fx3.overwrite( "src/lib/CMakeLists.txt",
                       "add_library(fxlib a.cpp)\ntarget_link_libraries(fxlib PRIVATE jsoncpp)\n" );
        fx3.append( "tests/CMakeLists.txt", "target_link_libraries(test_req PRIVATE fxlib)\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx3.model() ).empty() );

        // unknown link composition (${VAR} the model cannot resolve) must
        // stay silent rather than guess red or green
        Fixture fx4 = json_fixture( "reqlink_unknown" );
        fx4.append( "tests/CMakeLists.txt",
                    "target_link_libraries(test_req PRIVATE ${SOME_EXTERNAL_LIST})\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx4.model() ).empty() );

        // Qt6::Network evidence via <QHostInfo> + real usage (the #1302
        // workflow_run_lock shape)
        Fixture fx5 = json_fixture( "reqlink_net" );
        fx5.overwrite( "src/lib/a.cpp",
                       "#include <QHostInfo>\nstd::string fx_a() { return QHostInfo::localHostName().toStdString(); }\n" );
        REQUIRE( count_with_prefix( wiring::check_embed_link_requirements( fx5.model() ),
                                    "embedded source needs [Qt6::Network]" ) == 1 );

        Fixture fx6 = json_fixture( "reqlink_net_ok" );
        fx6.overwrite( "src/lib/a.cpp",
                       "#include <QHostInfo>\nstd::string fx_a() { return QHostInfo::localHostName().toStdString(); }\n" );
        fx6.append( "tests/CMakeLists.txt", "target_link_libraries(test_req PRIVATE Qt6::Network)\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx6.model() ).empty() );

        // include-without-use is NOT a requirement
        Fixture fx7 = json_fixture( "reqlink_nouse" );
        fx7.overwrite( "src/lib/a.cpp", "#include <json/json.h>\nint fx_a;\n" );
        REQUIRE( wiring::check_embed_link_requirements( fx7.model() ).empty() );
    }
    SECTION( "rule 11 kills an embed that uses a header without its body; accepts embed/link/no-use" )
    {
        const auto companion_fixture = []( const std::string &name, const std::string &tu_body ) {
            Fixture f = Fixture::make( name );
            f.append( "src/lib/util.h", "int fx_util_value();\n" );
            f.append( "src/lib/util.cpp", "int fx_util_value() { return 7; }\n" );
            f.overwrite( "src/lib/CMakeLists.txt", "add_library(fxlib a.cpp util.cpp)\n" );
            f.overwrite( "src/lib/a.cpp", tu_body );
            f.overwrite( "tests/CMakeLists.txt",
                         "add_executable(test_embed test_embed.cpp ${CMAKE_SOURCE_DIR}/src/lib/a.cpp)\n"
                         "target_link_libraries(test_embed PRIVATE m)\n" );
            f.append( "tests/test_embed.cpp", "int main(){return 0;}\n" );
            return f;
        };
        // uses fx_util_value(), embeds only a.cpp, links nothing that
        // compiles util.cpp — red (the #1303/#1304 atomic_fs /
        // fault_registry shape)
        const Fixture fx = companion_fixture( "companion", "#include \"util.h\"\nint fx_a() { return fx_util_value(); }\n" );
        REQUIRE( count_with_prefix( wiring::check_embed_companion_sources( fx.model() ),
                                    "embedded a.cpp uses util.h but its body util.cpp" ) == 1 );

        // green: the body is embedded alongside
        Fixture fx2 = companion_fixture( "companion_embed", "#include \"util.h\"\nint fx_a() { return fx_util_value(); }\n" );
        fx2.append( "tests/CMakeLists.txt",
                    "target_sources(test_embed PRIVATE ${CMAKE_SOURCE_DIR}/src/lib/util.cpp)\n" );
        REQUIRE( wiring::check_embed_companion_sources( fx2.model() ).empty() );

        // green: a linked target compiles the body
        Fixture fx3 = companion_fixture( "companion_link", "#include \"util.h\"\nint fx_a() { return fx_util_value(); }\n" );
        fx3.append( "tests/CMakeLists.txt", "target_link_libraries(test_embed PRIVATE fxlib)\n" );
        REQUIRE( wiring::check_embed_companion_sources( fx3.model() ).empty() );

        // green: include without use is not drift (nothing to link)
        Fixture fx4 = companion_fixture( "companion_nouse", "#include \"util.h\"\nint fx_a;\n" );
        REQUIRE( wiring::check_embed_companion_sources( fx4.model() ).empty() );
    }
    SECTION( "rule 12 kills a test TU using a Qt class with no direct include; accepts include and forward declaration" )
    {
        const auto include_fixture = []( const std::string &name, const std::string &tu_body ) {
            Fixture f = Fixture::make( name );
            f.append( "tests/test_json.cpp", tu_body );
            f.overwrite( "tests/CMakeLists.txt", "add_executable(test_json test_json.cpp)\n" );
            return f;
        };
        const Fixture fx =
            include_fixture( "qtinclude", "int f(const QJsonDocument &d);\nint fx_g(){return 0;}\n" );
        REQUIRE( count_with_prefix( wiring::check_test_tu_includes( fx.model() ),
                                    "test TU uses QJsonDocument" ) == 1 );

        const Fixture fx2 = include_fixture( "qtinclude_ok",
                                             "#include <QJsonDocument>\nint f(const QJsonDocument &d);\n" );
        REQUIRE( wiring::check_test_tu_includes( fx2.model() ).empty() );

        const Fixture fx3 = include_fixture( "qtinclude_fwd",
                                             "class QJsonDocument;\nint f(const QJsonDocument &d);\n" );
        REQUIRE( wiring::check_test_tu_includes( fx3.model() ).empty() );
    }
    SECTION( "rule 13 kills a moc-needing embed without automoc; accepts AUTOMOC ON or a linked automoc provider" )
    {
        const auto moc_fixture = []( const std::string &name ) {
            Fixture f = Fixture::make( name );
            // the Q_OBJECT class header shares the TU's stem — the shape the
            // live targets (workbench_host.cpp/.h …) actually have
            f.append( "src/lib/obj_user.h", "class FxObj {\n  Q_OBJECT\n public:\n  void ping();\n};\n" );
            f.append( "src/lib/obj_user.cpp",
                      "#include \"obj_user.h\"\nvoid FxObj::ping() { emit ping(); }\n" );
            f.overwrite( "tests/CMakeLists.txt",
                         "add_executable(test_moc test_moc.cpp ${CMAKE_SOURCE_DIR}/src/lib/obj_user.cpp)\n"
                         "target_link_libraries(test_moc PRIVATE Qt6::Core)\n" );
            f.append( "tests/test_moc.cpp", "int main(){return 0;}\n" );
            return f;
        };
        const Fixture fx = moc_fixture( "automoc" );
        REQUIRE( count_with_prefix( wiring::check_embed_automoc( fx.model() ),
                                    "embedded obj_user.cpp touches a Q_OBJECT metaobject" ) == 1 );

        // green: the embed target turns AUTOMOC on itself
        Fixture fx2 = moc_fixture( "automoc_on" );
        fx2.append( "tests/CMakeLists.txt",
                    "set_target_properties(test_moc PROPERTIES AUTOMOC ON)\n" );
        REQUIRE( wiring::check_embed_automoc( fx2.model() ).empty() );

        // green: a linked qt_add_library target compiles the same class and
        // brings the moc symbols (the "borrow the moc" pattern)
        Fixture fx3 = moc_fixture( "automoc_provider" );
        fx3.append( "src/lib/CMakeLists.txt", "qt_add_library(fxmoc_lib STATIC obj_user.cpp)\n" );
        fx3.append( "tests/CMakeLists.txt", "target_link_libraries(test_moc PRIVATE fxmoc_lib)\n" );
        REQUIRE( wiring::check_embed_automoc( fx3.model() ).empty() );
    }
}
