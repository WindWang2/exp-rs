// fuzz_corpus.h — structured mutators and a delta-debugging minimizer for the
// boundary fuzz lanes (Track ds41-fuzz-boundaries, tests/fuzz ownership).
//
// Ground rules (same discipline as support/bounded_fuzz.h, which this header
// builds on and DOES NOT modify — that file is shared with other tracks):
//   * Deterministic: every generator is a pure function of (state, cap), so a
//     failure reproduces from (test case, seed, iteration index).
//   * Hard caps: generated inputs never exceed the documented parser bounds
//     plus slack, so a bug can neither hang a run nor exhaust memory.
//   * The minimizer (ddmin) keeps a failing input 1-minimal, so a recorded
//     corpus fixture is the smallest input that still reproduces the defect.
#pragma once

#include "support/bounded_fuzz.h"

#include <json/json.h>

#include <algorithm>
#include <cstddef>
#include <string>
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

/// The wrong-type replacements a hostile peer can send for a field that the
/// contract expects to be a string/number/bool/object/array.
const std::vector<Json::Value> &wrongTypeSoup();

/// Picks a random member path of @p seed and replaces its value with a random
/// wrong-typed value (string<->number<->bool<->array<->object<->null). When
/// @p seed has no members the whole value is replaced.
Json::Value mutateTypes( const Json::Value &seed, BoundedRandom &random );

/// Nested containers to @p depth (the classic stack-pressure shape). The
/// layer under test is the reader's own depth guard: the property asserted by
/// the lanes is that this is a TYPED refusal, never a crash.
std::string depthBomb( int depth );

// ---------------------------------------------------------------------------
// Path corpus
// ---------------------------------------------------------------------------

/// Directed traversal/containment corpus: parent climbs, absolute forms,
/// drive-relative and UNC forms, Unicode names, trailing separators, empty
/// and separator-only values, plus byte sequences that are NOT valid UTF-8
/// (those fail on every host, so the totality pin does not depend on the
/// machine's ANSI code page). Used by the path lane.
const std::vector<std::string> &pathCorpus();

/// Mutates a path seed with path-flavored noise (separators, ".", "..",
/// drive colons, slashes, non-ASCII bytes).
std::string mutatePath( const std::string &seed, BoundedRandom &random );

// ---------------------------------------------------------------------------
// Byte-stream mutation (IPC framing / checkpoint truncation)
// ---------------------------------------------------------------------------

/// Every prefix of @p seed plus single-byte flips — the shapes a cut pipe,
/// a half-written frame or a corrupted checkpoint produces. Bounded: prefixes
/// longer than @p maxPrefix are subsampled.
std::vector<std::string> streamMutations( const std::string &seed, size_t maxPrefix = 64 );

// ---------------------------------------------------------------------------
// Delta debugging (minimal reducer)
// ---------------------------------------------------------------------------

/// Reduces @p input to a 1-minimal subsequence that still satisfies
/// @p reproduces(input) == true.
///
/// Byte-granularity delta debugging: sweeping removals of 1, 2 and 4 bytes and
/// repeating the whole sweep until nothing can be removed any more. The outer
/// loop matters — removing two bytes can succeed where removing either one
/// alone fails (e.g. a `,"` pair), so a single pass is not a fixed point.
/// Every successful removal strictly shrinks the input, so the loop always
/// terminates; @p maxRounds bounds a pathological predicate.
template <typename Predicate>
std::string ddmin( std::string input, Predicate reproduces, size_t maxRounds = 256 )
{
    if ( !reproduces( input ) )
        return {}; // caller error: the input did not reproduce to begin with

    const size_t granularities[] = { 1, 2, 4 };
    bool reduced = true;
    size_t rounds = 0;
    while ( reduced && rounds++ < maxRounds )
    {
        reduced = false;
        for ( const size_t granularity : granularities )
        {
            size_t start = 0;
            while ( granularity <= input.size() )
            {
                if ( start + granularity > input.size() )
                    break;
                std::string candidate = input;
                candidate.erase( static_cast<std::string::difference_type>( start ),
                                 granularity );
                if ( reproduces( candidate ) )
                {
                    input = std::move( candidate );
                    reduced = true;
                    continue; // retry the same offset before advancing
                }
                start += granularity;
            }
        }
    }
    return input;
}

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
    const std::string key = path.substr(
        pos, slash == std::string::npos ? std::string::npos : slash - pos );
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

inline Json::Value mutateTypes( const Json::Value &seed, BoundedRandom &random )
{
    const std::vector<std::string> paths = jsonMemberPaths( seed );
    if ( paths.empty() )
        return randomJsonValue( random );

    // Prefer mutating a non-root member so the container shape usually survives.
    std::string target = paths[random.below( static_cast<uint32_t>( paths.size() ) )];
    if ( paths.size() > 1 && target.empty() )
        target = paths[1 + random.below( static_cast<uint32_t>( paths.size() - 1 ) )];

    Json::Value out;
    const Json::Value replacement =
        wrongTypeSoup()[random.below( static_cast<uint32_t>( wrongTypeSoup().size() ) )];
    if ( jsonSetAtPathRecursive( seed, replacement, target, 0, out ) )
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

inline const std::vector<std::string> &pathCorpus()
{
    static const std::vector<std::string> corpus = [] {
        std::vector<std::string> entries = {
            "",                       // empty
            ".",                      // current dir
            "..",                     // parent
            "../",                    // parent with separator
            "..\\",                   // Windows parent with separator
            "../../../../etc/passwd",
            "..\\..\\..\\windows\\system32\\drivers\\etc\\hosts",
            "/etc/passwd",            // POSIX absolute
            "C:\\Windows\\system32",  // drive absolute
            "C:/Windows/system32",
            "C:relative",             // drive-relative
            "\\\\server\\share\\x",   // UNC
            "sub/../ok.txt",          // contained climb
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
        // Not valid UTF-8 / not expressible in any code page. These fail on
        // EVERY host, so the D5 totality pin is host-independent.
        entries.push_back( std::string( "\xFF\xFE", 2 ) );
        entries.push_back( std::string( "a\x80" "b", 3 ) );
        entries.push_back( std::string( "lone\xC3.txt", 9 ) );  // lone lead byte: invalid UTF-8
        entries.push_back( std::string( "\xE4\xB8" "trunc.txt", 11 ) );
        return entries;
    }();
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
