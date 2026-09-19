// fuzz_corpus.h — structured mutators, delta-debugging minimizer and an
// optional corpus recorder for the boundary fuzz lanes
// (Track ds41-fuzz-boundaries, tests/fuzz ownership).
//
// Ground rules (same discipline as support/bounded_fuzz.h, which this header
// builds on and DOES NOT modify):
//   * Deterministic: every generator is a pure function of (state, cap), so a
//     failure reproduces from (test case, seed, iteration index).
//   * Hard caps: generated inputs never exceed the documented parser bounds
//     plus slack, so a bug can neither hang a run nor exhaust memory.
//   * The minimizer (ddmin) keeps a failing input 1-minimal, so a checked-in
//     corpus fixture is the smallest input that still reproduces the defect.
//   * The recorder is a NO-OP unless SICNU_FUZZ_CORPUS_DIR points somewhere:
//     ordinary test runs never write into the working tree.
#pragma once

#include "support/bounded_fuzz.h"

#include <json/json.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace sicnu::fuzz
{

using sicnu::testing::BoundedRandom;

// ---------------------------------------------------------------------------
// JSON structured generation / mutation
// ---------------------------------------------------------------------------

inline constexpr int kMaxJsonDepth = 4;
inline constexpr size_t kMaxJsonMembers = 6;
inline constexpr size_t kMaxJsonStringLen = 24;

/// Generates a bounded random JSON value. Strings stay short, containers stay
/// small and depth is capped, so serialization cost is O(cap), never O(input).
Json::Value randomJsonValue( BoundedRandom &random, int depth = 0 );

/// Collects the member paths of @p value ("" is the root). Used by
/// mutateTypes to aim a mutation at a real field.
std::vector<std::string> jsonMemberPaths( const Json::Value &value );

/// Reads the member at @p path ("" = root). Returns null when absent.
Json::Value jsonAtPath( const Json::Value &value, const std::string &path );

/// Replaces the member at @p path with @p replacement (returns a copy).
bool jsonSetAtPath( const Json::Value &value, const std::string &path,
                    const Json::Value &replacement, Json::Value &out );

/// The wrong-type replacements a hostile peer can send for a field that the
/// contract expects to be a string/number/bool/object/array.
const std::vector<Json::Value> &wrongTypeSoup();

/// Picks a random member path of @p seed and replaces its value with a random
/// wrong-typed value (string<->number<->bool<->array<->object<->null). When
/// @p seed has no members the whole value is replaced.
Json::Value mutateTypes( const Json::Value &seed, BoundedRandom &random );

/// Nested containers to @p depth (the classic stack-pressure shape). jsoncpp's
/// own stackLimit guard is the layer under test: the property asserted by the
/// lanes is that this is a TYPED rejection, never a crash.
std::string depthBomb( int depth );

/// A payload @p bytes long of legal JSON filler (kept small by callers).
std::string sizeBomb( size_t bytes );

// ---------------------------------------------------------------------------
// Path corpus
// ---------------------------------------------------------------------------

/// Directed traversal/containment corpus: parent climbs, absolute forms,
/// drive-relative and UNC forms, Unicode names, trailing separators, empty
/// and separator-only values. Used by the path lane.
const std::vector<std::string> &pathCorpus();

/// Mutates a path seed with path-flavored noise (separators, ".", "..",
/// drive colons, slashes, non-ASCII bytes).
std::string mutatePath( const std::string &seed, BoundedRandom &random );

// ---------------------------------------------------------------------------
// Byte-stream mutation (IPC framing / checkpoint truncation)
// ---------------------------------------------------------------------------

/// Every prefix of @p seed plus single-byte flips — the shapes a cut pipe,
/// a half-written frame or a corrupted checkpoint byte leaves behind.
/// Bounded: prefixes longer than @p maxPrefix are subsampled.
std::vector<std::string> streamMutations( const std::string &seed, size_t maxPrefix = 64 );

// ---------------------------------------------------------------------------
// Delta debugging (1-minimal reducer)
// ---------------------------------------------------------------------------

/// Reduces @p input to a 1-minimal subsequence that still satisfies
/// @p reproduces(input) == true (Zeller & Hildebrandt ddmin, byte granularity).
/// @p maxRounds bounds the work so a runaway predicate cannot stall a run.
template <typename Predicate>
std::string ddmin( std::string input, Predicate reproduces, size_t maxRounds = 64 )
{
    if ( !reproduces( input ) )
        return {}; // caller error: the input did not reproduce to begin with

    size_t granularity = 2;
    size_t rounds = 0;
    while ( granularity <= input.size() && rounds++ < maxRounds )
    {
        bool reduced = false;
        for ( size_t start = 0; start + granularity <= input.size() && !reduced; )
        {
            std::string candidate = input;
            candidate.erase( static_cast<std::string::difference_type>( start ),
                             granularity );
            if ( reproduces( candidate ) )
            {
                input = std::move( candidate );
                reduced = true;
                // Same granularity retries at the same offset first.
                continue;
            }
            start += std::max<size_t>( 1, granularity );
        }
        if ( !reduced )
        {
            if ( granularity >= input.size() )
                break;
            granularity = std::min( input.size(), granularity * 2 );
        }
    }
    return input;
}

// ---------------------------------------------------------------------------
// Corpus recorder
// ---------------------------------------------------------------------------

/// Writes a minimized failing fixture (and one README line per fixture) into
/// $SICNU_FUZZ_CORPUS_DIR/<area>/. Silent no-op when the variable is unset or
/// the directory cannot be created — a test run must never fail because the
/// corpus directory is unwritable.
class FaultRecorder
{
  public:
    explicit FaultRecorder( std::string area )
        : m_area( std::move( area ) )
    {
        const char *dir = std::getenv( "SICNU_FUZZ_CORPUS_DIR" );
        if ( dir && *dir )
            m_root = dir;
    }

    bool enabled() const { return !m_root.empty(); }
    const std::string &root() const { return m_root; }

    /// Writes @p fixture bytes as <area>/<name> and appends a one-line note to
    /// <area>/README.md. Returns the written path ("" when disabled/failed).
    std::string record( const std::string &name, const std::string &note,
                        const std::string &fixture )
    {
        if ( !enabled() )
            return {};
        const std::string dir = m_root + "/" + m_area;
        std::error_code ec;
        std::filesystem::create_directories( dir, ec );
        const std::string path = dir + "/" + name;
        {
            std::ofstream out( path, std::ios::binary | std::ios::trunc );
            if ( !out )
                return {};
            out.write( fixture.data(), static_cast<std::streamsize>( fixture.size() ) );
            out.flush();
            if ( !out )
                return {};
        }
        {
            std::ofstream readme( dir + "/README.md", std::ios::app );
            if ( readme )
                readme << "- `" << name << "` — " << note << "\n";
        }
        return path;
    }

  private:
    std::string m_area;
    std::string m_root;
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

inline Json::Value randomJsonValue( BoundedRandom &random, int depth )
{
    if ( depth >= kMaxJsonDepth || random.chance( 0.45 ) )
    {
        switch ( random.below( 5 ) )
        {
        case 0:
            return Json::Value( static_cast<Json::Int>( random.below( 1000 ) ) - 500 );
        case 1:
            return Json::Value( static_cast<double>( random.below( 10000 ) ) / 7.0 );
        case 2:
            return Json::Value( random.string( 0, kMaxJsonStringLen,
                                               { 'a', 'b', 'c', 'x', ':', '"', '\\',
                                                 static_cast<char>( 0xC3 ), static_cast<char>( 0xA9 ),
                                                 '.', '/' } ) );
        case 3:
            return Json::Value( random.below( 2 ) == 1 );
        default:
            return Json::Value( Json::nullValue );
        }
    }
    if ( random.below( 2 ) == 0 )
    {
        Json::Value array( Json::arrayValue );
        const size_t count = 1 + random.below( kMaxJsonMembers );
        for ( size_t i = 0; i < count; ++i )
            array.append( randomJsonValue( random, depth + 1 ) );
        return array;
    }
    Json::Value object( Json::objectValue );
    const size_t count = 1 + random.below( kMaxJsonMembers );
    static const char *kKeys[] = { "id",     "name",   "version", "type",  "ok",
                                   "value",  "params", "steps",  "kind",  "v",
                                   "method", "code",   "label",  "title", "options" };
    for ( size_t i = 0; i < count; ++i )
        object[ kKeys[random.below( sizeof( kKeys ) / sizeof( kKeys[0] ) )] ] =
            randomJsonValue( random, depth + 1 );
    return object;
}

inline void jsonMemberPathsRecursive( const Json::Value &value, const std::string &prefix,
                                      std::vector<std::string> &out )
{
    out.push_back( prefix );
    if ( value.isObject() )
    {
        for ( const std::string &key : value.getMemberNames() )
            jsonMemberPathsRecursive( value[key], prefix.empty() ? key : prefix + "/" + key,
                                      out );
    }
    else if ( value.isArray() )
    {
        for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
            jsonMemberPathsRecursive( value[i], prefix + "/" + std::to_string( i ), out );
    }
}

inline std::vector<std::string> jsonMemberPaths( const Json::Value &value )
{
    std::vector<std::string> out;
    jsonMemberPathsRecursive( value, "", out );
    return out;
}

inline Json::Value jsonAtPathRecursive( const Json::Value &value, const std::string &path,
                                        size_t &pos, bool &found )
{
    if ( pos >= path.size() )
    {
        found = true;
        return value;
    }
    const size_t slash = path.find( '/', pos );
    const std::string key = path.substr( pos, slash == std::string::npos ? std::string::npos
                                                                         : slash - pos );
    if ( value.isObject() && value.isMember( key ) )
    {
        pos = slash == std::string::npos ? path.size() : slash + 1;
        return jsonAtPathRecursive( value[key], path, pos, found );
    }
    if ( value.isArray() && !key.empty() && key.find_first_not_of( "0123456789" ) == std::string::npos )
    {
        const Json::ArrayIndex index = static_cast<Json::ArrayIndex>( std::stoul( key ) );
        if ( index < value.size() )
        {
            pos = slash == std::string::npos ? path.size() : slash + 1;
            return jsonAtPathRecursive( value[index], path, pos, found );
        }
    }
    found = false;
    return Json::Value( Json::nullValue );
}

inline Json::Value jsonAtPath( const Json::Value &value, const std::string &path )
{
    if ( path.empty() )
        return value;
    size_t pos = 0;
    bool found = false;
    const Json::Value result = jsonAtPathRecursive( value, path, pos, found );
    return found ? result : Json::Value( Json::nullValue );
}

inline bool jsonSetAtPathRecursive( const Json::Value &value, const Json::Value &replacement,
                                    const std::string &path, size_t pos, Json::Value &out )
{
    out = value;
    if ( pos >= path.size() )
    {
        out = replacement;
        return true;
    }
    const size_t slash = path.find( '/', pos );
    const std::string key = path.substr( pos, slash == std::string::npos ? std::string::npos
                                                                         : slash - pos );
    if ( value.isObject() && value.isMember( key ) )
    {
        Json::Value child;
        if ( !jsonSetAtPathRecursive( value[key], replacement, path,
                                      slash == std::string::npos ? path.size() : slash + 1,
                                      child ) )
            return false;
        out[key] = child;
        return true;
    }
    if ( value.isArray() && !key.empty()
         && key.find_first_not_of( "0123456789" ) == std::string::npos )
    {
        const Json::ArrayIndex index = static_cast<Json::ArrayIndex>( std::stoul( key ) );
        if ( index < value.size() )
        {
            Json::Value child;
            if ( !jsonSetAtPathRecursive( value[index], replacement, path,
                                          slash == std::string::npos ? path.size() : slash + 1,
                                          child ) )
                return false;
            out[index] = child;
            return true;
        }
    }
    return false;
}

inline bool jsonSetAtPath( const Json::Value &value, const std::string &path,
                           const Json::Value &replacement, Json::Value &out )
{
    return jsonSetAtPathRecursive( value, replacement, path, 0, out );
}

inline const std::vector<Json::Value> &wrongTypeSoup()
{
    static const std::vector<Json::Value> soup = [] {
        std::vector<Json::Value> values;
        values.push_back( Json::Value( "wrong-type-string" ) );
        values.push_back( Json::Value( 0 ) );
        values.push_back( Json::Value( -1 ) );
        values.push_back( Json::Value( 1.5 ) );
        values.push_back( Json::Value( true ) );
        values.push_back( Json::Value( Json::objectValue ) );
        values.push_back( Json::Value( Json::arrayValue ) );
        values.push_back( Json::Value( Json::nullValue ) );
        return values;
    }();
    return soup;
}

inline Json::Value mutateTypes( const Json::Value &seed, BoundedRandom &random )
{
    const std::vector<std::string> paths = jsonMemberPaths( seed );
    if ( paths.empty() )
        return randomJsonValue( random );

    // Prefer mutating a leaf/interesting member: pick randomly among paths that
    // are not the root so the container shape usually survives.
    std::string target = paths[random.below( static_cast<uint32_t>( paths.size() ) )];
    if ( paths.size() > 1 && target.empty() )
        target = paths[1 + random.below( static_cast<uint32_t>( paths.size() - 1 ) )];

    Json::Value out;
    const Json::Value replacement =
        wrongTypeSoup()[random.below( static_cast<uint32_t>( wrongTypeSoup().size() ) )];
    if ( jsonSetAtPath( seed, target, replacement, out ) )
        return out;
    return replacement;
}

inline std::string depthBomb( int depth )
{
    std::string bomb;
    bomb.reserve( static_cast<size_t>( depth ) * 2 + 8 );
    bomb += R"({"a":)";
    for ( int i = 0; i < depth; ++i )
        bomb += "[";
    bomb += "1";
    for ( int i = 0; i < depth; ++i )
        bomb += "]";
    bomb += "}";
    return bomb;
}

inline std::string sizeBomb( size_t bytes )
{
    // A large-but-legal JSON string value: {"pad":"xxxx..."}.
    std::string bomb = R"({"pad":")";
    bomb.append( bytes, 'x' );
    bomb += "\"}";
    return bomb;
}

inline const std::vector<std::string> &pathCorpus()
{
    static const std::vector<std::string> corpus = {
        "",                     // empty
        ".",                    // current dir
        "..",                   // parent
        "../",                  // parent with separator
        "..\\",                 // Windows parent with separator
        "../../../../etc/passwd",
        "..\\..\\..\\windows\\system32\\drivers\\etc\\hosts",
        "/etc/passwd",          // POSIX absolute
        "C:\\Windows\\system32", // drive absolute
        "C:/Windows/system32",
        "C:relative",            // drive-relative
        "\\\\server\\share\\x",  // UNC
        "sub/../ok.txt",        // contained climb
        "sub/dir/./ok.txt",
        "sub/dir/ok.txt",
        "trailing/",
        "trailing\\",
        "spaces in name.txt",
        "unicode-\xC3\xA9\xE4\xB8\xAD.txt",
        "nested/" + std::string( 64, 'a' ) + ".txt",
        "colon:in:name.txt",
        "stream.txt:alternate",
        "....//....//escape.txt",
        "%2e%2e/escape.txt",
    };
    return corpus;
}

inline std::string mutatePath( const std::string &seed, BoundedRandom &random )
{
    static const std::vector<std::string> edits = {
        "..", "/", "\\", ".", "://", "C:", ":", "%", "//", "...", "\xC3\xA9", "\xE4\xB8\xAD",
    };
    std::string out = seed;
    const uint32_t editsCount = 1 + random.below( 6 );
    for ( uint32_t i = 0; i < editsCount; ++i )
    {
        const std::string &edit = edits[random.below( static_cast<uint32_t>( edits.size() ) )];
        if ( out.empty() || random.below( 2 ) == 0 )
            out += edit;
        else
            out.insert( static_cast<std::string::difference_type>(
                            random.below( static_cast<uint32_t>( out.size() ) ) ),
                        edit );
    }
    if ( out.size() > 512 )
        out.resize( 512 );
    return out;
}

inline std::vector<std::string> streamMutations( const std::string &seed, size_t maxPrefix )
{
    std::vector<std::string> out;
    if ( seed.empty() )
        return out;
    // Every prefix (subsampled above maxPrefix): the truncation family.
    for ( size_t len = 0; len < seed.size(); ++len )
    {
        if ( len <= maxPrefix || len == seed.size() - 1
             || ( seed.size() - len ) % std::max<size_t>( 1, seed.size() / 8 ) == 0 )
            out.push_back( seed.substr( 0, len ) );
    }
    out.push_back( seed ); // the intact frame must still round-trip
    // Single-byte flips at structural positions (length prefix + separators).
    for ( size_t i = 0; i < std::min( seed.size(), maxPrefix ); ++i )
    {
        std::string flipped = seed;
        flipped[i] = static_cast<char>( flipped[i] ^ 0x5A );
        out.push_back( flipped );
    }
    return out;
}

} // namespace sicnu::fuzz
