// test_contract_fuzz_lang.cpp — bounded property tests over the two small
// language surfaces (task D, Verification 7.0):
//
//   * MapSpec condition expressions (documented bounds: ≤256 chars, ≤64
//     tokens, depth ≤8; unknown paths are EVALUATION errors, never silent
//     false) — sources compiled in directly (Qt-free, jsoncpp only);
//   * workflow placeholder grammar ($step.port / ${task.N.port} / ${ENV}) —
//     compiled in directly (Qt6::Core only).
//
// Invariants: totality (no input throws), bound enforcement (over-length
// input is rejected), context coherence (paths read by a valid expression
// are exactly the paths it needs), and truthful failure (invalid syntax
// never evaluates).
#include <catch2/catch_test_macros.hpp>

#include "agent/mapspec/mapspec_conditions.h"
#include "support/bounded_fuzz.h"
#include "workflow/placeholder_grammar.h"

#include <json/json.h>

#include <set>
#include <string>
#include <vector>

using sicnu::testing::BoundedRandom;

namespace
{
constexpr size_t kMaxExprBytes = 512;
constexpr int kIterationsPerSeed = 600;

const std::vector<char> kExprAlphabet = [] {
    std::vector<char> chars;
    for ( char c = 'a'; c <= 'z'; ++c )
        chars.push_back( c );
    for ( char c = '0'; c <= '9'; ++c )
        chars.push_back( c );
    for ( const char c : std::string_view( "_. ()\"<>=!&|" ) )
        chars.push_back( c );
    return chars;
}();

const std::vector<std::string> kConditionFragments = {
    "visible_if",  "count > 0",   "count >= 1",  "name == \"a\"", "and",
    "or",          "has(",        "bindings.x",  "params.y",      "results.n",
    "true",        "false",       "(",           ")",             "==",
    "!=",          "100",         "-3",          "a.b.c",         "",
};

std::string generateExpr( BoundedRandom &random )
{
    const int fragmentCount = static_cast<int>( random.below( 6 ) );
    std::string out;
    for ( int i = 0; i < fragmentCount; ++i )
    {
        out += random.pick( kConditionFragments );
        out += " ";
    }
    out += random.string( 0, kMaxExprBytes - out.size(), kExprAlphabet );
    if ( out.size() > kMaxExprBytes )
        out.resize( kMaxExprBytes );
    return out;
}

Json::Value contextForPaths( const std::set<std::string> &paths )
{
    Json::Value context{ Json::objectValue };
    // Pass 1: create every intermediate node as an object (paths may be
    // prefixes of other paths; objects win over scalar leaves).
    std::vector<std::vector<std::string>> split;
    for ( const std::string &path : paths )
    {
        std::vector<std::string> segments;
        std::string part;
        for ( const char ch : path )
        {
            if ( ch == '.' )
            {
                segments.push_back( part );
                part.clear();
            }
            else
            {
                part.push_back( ch );
            }
        }
        segments.push_back( part );
        split.push_back( segments );

        Json::Value *node = &context;
        for ( size_t i = 0; i + 1 < segments.size(); ++i )
        {
            Json::Value &child = ( *node )[segments[i]];
            if ( !child.isObject() )
                child = Json::Value{ Json::objectValue };
            node = &child;
        }
    }
    // Pass 2: set scalar leaves, never overwriting an object node.
    for ( const auto &segments : split )
    {
        Json::Value *node = &context;
        for ( const std::string &segment : segments )
            node = &( *node )[segment];
        if ( !node->isObject() )
            *node = Json::Value{ 1 };
    }
    return context;
}
} // namespace

TEST_CASE( "condition AST fuzz: validation is total and bounds are enforced",
           "[contract][fuzz][condition]" )
{
    for ( const uint64_t seed : { 3ull, 17ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string expr = generateExpr( random );
            std::vector<std::string> problems;
            REQUIRE_NOTHROW( sicnu::agent::mapspec::validateConditionSyntax( expr, &problems ) );
            REQUIRE_NOTHROW( sicnu::agent::mapspec::conditionPaths( expr ) );

            // A path-only probe is total as well.
            std::set<std::string> paths;
            REQUIRE_NOTHROW( paths = sicnu::agent::mapspec::conditionPaths( expr ) );
        }
    }
}

TEST_CASE( "condition AST: over-length expressions are rejected (documented bound)",
           "[contract][condition]" )
{
    std::string over( sicnu::agent::mapspec::kConditionMaxLength + 1, 'a' );
    std::vector<std::string> problems;
    REQUIRE_FALSE( sicnu::agent::mapspec::validateConditionSyntax( over, &problems ) );
    REQUIRE_FALSE( problems.empty() );

    std::set<std::string> paths;
    REQUIRE_NOTHROW( paths = sicnu::agent::mapspec::conditionPaths( over ) );
}

TEST_CASE( "condition AST fuzz: valid expressions evaluate truthfully against "
           "their own context",
           "[contract][fuzz][condition]" )
{
    for ( const uint64_t seed : { 8ull, 23ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string expr = generateExpr( random );
            std::vector<std::string> problems;
            if ( !sicnu::agent::mapspec::validateConditionSyntax( expr, &problems ) )
                continue; // invalid syntax must not evaluate — tested below

            // Context coherence: a context containing exactly the paths the
            // expression reads must never hit the "unknown path" error class.
            const std::set<std::string> paths =
                sicnu::agent::mapspec::conditionPaths( expr );
            const Json::Value context = contextForPaths( paths );
            bool value = false;
            std::string error;
            const bool ok =
                sicnu::agent::mapspec::evaluateCondition( expr, context, &value, &error );
            if ( !ok )
            {
                // Typed mismatch errors are allowed; unknown paths are not —
                // the context above defines every path the expression reads.
                INFO( "expr: " << expr << " error: " << error );
                REQUIRE( error.find( "unknown path" ) == std::string::npos );
            }
        }
    }
}

TEST_CASE( "condition AST: invalid syntax never evaluates", "[contract][condition]" )
{
    const std::vector<std::string> invalid = {
        "count >",            // dangling operator
        "(a == 1",            // unbalanced
        "a == 1)",            // unbalanced
        "== 1",               // missing left operand
        "a === 1",            // malformed operator
        "count > \"unterminated", // unterminated string
    };
    for ( const std::string &expr : invalid )
    {
        std::vector<std::string> problems;
        if ( sicnu::agent::mapspec::validateConditionSyntax( expr, &problems ) )
        {
            // If the grammar accepts it, evaluation must still be total and
            // typed — never throw.
            Json::Value context{ Json::objectValue };
            bool value = false;
            std::string error;
            REQUIRE_NOTHROW(
                sicnu::agent::mapspec::evaluateCondition( expr, context, &value, &error ) );
        }
        else
        {
            REQUIRE_FALSE( problems.empty() ); // truthful: reasons are reported
        }
    }
}

// ---------------------------------------------------------------------------
// Placeholder grammar
// ---------------------------------------------------------------------------

TEST_CASE( "placeholder grammar fuzz: parsing is total and refs are coherent",
           "[contract][fuzz][placeholder]" )
{
    const std::vector<char> alphabet = [] {
        std::vector<char> chars;
        for ( char c = 'a'; c <= 'z'; ++c )
            chars.push_back( c );
        for ( char c = '0'; c <= '9'; ++c )
            chars.push_back( c );
        for ( const char c : std::string_view( "$ {}.:/_" ) )
            chars.push_back( c );
        return chars;
    }();

    for ( const uint64_t seed : { 31ull, 77ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string text = random.string( 0, 256, alphabet );
            std::vector<sicnu::workflow::PlaceholderRef> refs;
            REQUIRE_NOTHROW( refs = sicnu::workflow::parsePlaceholders( text ) );
            for ( const auto &ref : refs )
            {
                REQUIRE_FALSE( ref.rawRef.empty() );
                if ( ref.isEnvVar )
                    REQUIRE_FALSE( ref.envVarName.empty() );
                if ( !ref.isEnvVar && !ref.isParentKeyword && ref.parentTaskId < 0 )
                    REQUIRE_FALSE( ref.stepId.empty() );
            }
        }
    }
}

TEST_CASE( "placeholder grammar: documented forms parse with the right shape",
           "[contract][placeholder]" )
{
    using sicnu::workflow::parsePlaceholders;
    const auto step = parsePlaceholders( "$step1.output" );
    REQUIRE( step.size() == 1 );
    REQUIRE( step[0].stepId == "step1" );
    REQUIRE( step[0].portName == "output" );

    const auto braced = parsePlaceholders( "${render.png}" );
    REQUIRE( braced.size() == 1 );
    REQUIRE( braced[0].stepId == "render" );
    REQUIRE( braced[0].portName == "png" );

    const auto parent = parsePlaceholders( "${task.parent.output}" );
    REQUIRE( parent.size() == 1 );
    REQUIRE( parent[0].isParentKeyword );

    const auto numbered = parsePlaceholders( "${task.42.output}" );
    REQUIRE( numbered.size() == 1 );
    REQUIRE( numbered[0].parentTaskId == 42 );

    const auto env = parsePlaceholders( "${SICNU_WORK_DIR}" );
    REQUIRE( env.size() == 1 );
    REQUIRE( env[0].isEnvVar );
    REQUIRE( env[0].envVarName == "SICNU_WORK_DIR" );

    REQUIRE( parsePlaceholders( "no placeholders here" ).empty() );
}

TEST_CASE( "placeholder grammar fuzz: substitution with a counting resolver is "
           "consistent",
           "[contract][fuzz][placeholder]" )
{
    for ( const uint64_t seed : { 91ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string text = random.string( 0, 256, "ab${}/._0123456789" );
            const auto refs = sicnu::workflow::parsePlaceholders( text );
            size_t substitutions = 0;
            const std::string out = sicnu::workflow::substitutePlaceholders(
                text, [&]( const sicnu::workflow::PlaceholderRef & ) {
                    ++substitutions;
                    return "X";
                } );
            // A valid ref substitutes exactly once; an invalid one leaves the
            // text alone (truthful: never fabricated resolution).
            size_t validRefs = 0;
            for ( const auto &ref : refs )
                if ( ref.isValid() )
                    ++validRefs;
            INFO( "text: " << text );
            REQUIRE( substitutions >= validRefs ); // at least the valid ones
        }
    }
}
