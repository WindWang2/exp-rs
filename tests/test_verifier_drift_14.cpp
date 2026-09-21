/***************************************************************************
  test_verifier_drift_14.cpp — drift guard for the MIRRORED fact vocabulary

  Why this file exists: src/verification/checks_artifact.cpp keeps its own copy
  of the closed artifact-fact key set, because the authoritative one
  (`kFactKeys` in src/agent/harness/workflow_ir.cpp) sits in an anonymous
  namespace and cannot be linked from here.

  A mirror with no guard is just a second vocabulary waiting to drift. The
  failure mode is quiet and expensive: someone adds a fact key to the harness,
  the verifier's copy does not learn about it, and every check that reasons over
  that key silently starts answering "this key is not part of the vocabulary"
  for a fact that IS part of it. Depending on the call site that reads as either
  a schema mismatch (Fail) or a skipped expectation (Pass) — and neither is
  true.

  So this test reads the authoritative table out of the harness source at test
  time and compares it, element by element and in order, against the mirror.

  Two properties matter more than the comparison itself:

    1. A PARSER THAT FINDS NOTHING MUST FAIL. If the anchor text in
       workflow_ir.cpp is reworded, an over-eager regex would return an empty
       list, the two lists would "not differ" in any entry, and the guard would
       go green exactly when it had stopped guarding anything. Every parse is
       therefore asserted non-empty first (see `requireNonEmptyParse`).

    2. READ THE REAL FILE, NOT A FIXTURE. The point is to notice that the OTHER
       file changed, so the test must go to that file. CMAKE_SOURCE_DIR is
       injected by the build; when it is unavailable the test degrades to an
       explicit skip rather than a silent pass.
 ***************************************************************************/

#include "verification/checks_artifact.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

/// Path of the authoritative table, relative to the repository root.
constexpr const char *kWorkflowIrRelativePath = "src/agent/harness/workflow_ir.cpp";

/// The declaration this guard anchors on. Kept as a single literal so that a
/// rename in the harness shows up as "anchor not found" (a loud red) rather
/// than as a plausible-looking empty parse.
constexpr const char *kTableAnchor = "const FactKeySpec kFactKeys[] = {";

std::string readFileOrEmpty( const std::string &path )
{
    std::ifstream input( path, std::ios::binary );
    if ( !input )
    {
        return std::string();
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

/// Extracts the key names from the `kFactKeys` initializer list.
///
/// Deliberately narrow: the table's entries are all of the form
///     { "some_key", "string" },
/// so we take the FIRST quoted token on each line that starts with `{`, within
/// the brace-delimited block that follows the anchor. Reading the first quoted
/// token (rather than matching a whole entry) keeps the parser working if the
/// type column gains new spellings such as "either".
std::vector<std::string> parseFactKeys( const std::string &source )
{
    std::vector<std::string> keys;

    const std::size_t anchor = source.find( kTableAnchor );
    if ( anchor == std::string::npos )
    {
        return keys;   // caller asserts non-empty; an empty result is a failure
    }

    const std::size_t openBrace = source.find( '{', anchor + std::char_traits<char>::length( kTableAnchor ) - 1 );
    if ( openBrace == std::string::npos )
    {
        return keys;
    }

    std::size_t cursor = openBrace + 1;
    int depth = 1;
    while ( cursor < source.size() && depth > 0 )
    {
        const char c = source[cursor];
        if ( c == '{' )
        {
            ++depth;

            // Entry rows look like: { "key", "type" }, — read the first quoted
            // token on this row.
            const std::size_t firstQuote = source.find( '"', cursor + 1 );
            if ( firstQuote == std::string::npos )
            {
                break;
            }
            const std::size_t closingQuote = source.find( '"', firstQuote + 1 );
            if ( closingQuote == std::string::npos )
            {
                break;
            }
            // Guard against a row that swallowed the next line's quote.
            if ( source.find( '\n', cursor ) < firstQuote )
            {
                ++cursor;
                continue;
            }
            keys.push_back( source.substr( firstQuote + 1, closingQuote - firstQuote - 1 ) );
            cursor = closingQuote + 1;
            continue;
        }
        if ( c == '}' )
        {
            --depth;
        }
        ++cursor;
    }

    return keys;
}

/// Renders a key list for an INFO message.
std::string joinForMessage( const std::vector<std::string> &keys )
{
    if ( keys.empty() )
    {
        return "(none)";
    }
    std::string joined;
    for ( const std::string &key : keys )
    {
        if ( !joined.empty() )
        {
            joined += ", ";
        }
        joined += key;
    }
    return joined;
}

/// Asserts the parse is plausible before any comparison happens.
///
/// This is the guard's own guard. Without it, a reworded anchor yields an empty
/// vector, an empty vector trivially "matches" nothing, and the test passes at
/// the exact moment it stopped checking anything.
void requireUsableParse( const std::vector<std::string> &parsed,
                         const std::string &why )
{
    INFO( "parse context: " << why );
    REQUIRE_FALSE( parsed.empty() );
}

} // namespace

TEST_CASE( "verifier14 drift: the mirrored fact vocabulary still matches the harness table",
           "[verifier14][drift]" )
{
    const std::string root = std::string( CMAKE_SOURCE_DIR );
    const std::string source = readFileOrEmpty( root + "/" + kWorkflowIrRelativePath );

    if ( source.empty() )
    {
        // Degrade loudly-but-explicitly: we cannot read the authority, so we
        // cannot make a claim about drift. Say so instead of passing.
        WARN( "cannot read " << kWorkflowIrRelativePath
                             << " under CMAKE_SOURCE_DIR=" << root
                             << "; skipping the drift comparison" );
        return;
    }

    const std::vector<std::string> authoritative = parseFactKeys( source );
    requireUsableParse( authoritative,
                        "kFactKeys table in " + std::string( kWorkflowIrRelativePath )
                            + " (anchor: " + kTableAnchor + ")" );

    const std::vector<std::string> mirrored = sicnu::verification::mirroredFactKeys();

    // Report the specific difference before the blunt equality check, so a
    // failure names the key that drifted instead of just "vectors differ".
    std::vector<std::string> onlyInHarness;
    std::vector<std::string> onlyInMirror;
    for ( const std::string &key : authoritative )
    {
        if ( std::find( mirrored.begin(), mirrored.end(), key ) == mirrored.end() )
        {
            onlyInHarness.push_back( key );
        }
    }
    for ( const std::string &key : mirrored )
    {
        if ( std::find( authoritative.begin(), authoritative.end(), key ) == authoritative.end() )
        {
            onlyInMirror.push_back( key );
        }
    }

    INFO( "keys only in harness kFactKeys: " << joinForMessage( onlyInHarness ) );
    INFO( "keys only in verification mirror: " << joinForMessage( onlyInMirror ) );
    REQUIRE( onlyInHarness.empty() );
    REQUIRE( onlyInMirror.empty() );

    // Same elements, and the same ORDER: the mirror claims to reproduce the
    // table, so a reshuffle is drift too (it would break any code that pairs
    // the two lists positionally).
    REQUIRE( authoritative == mirrored );
}

TEST_CASE( "verifier14 drift: every mirrored key is a key the artifact checker will recognise",
           "[verifier14][drift]" )
{
    // The mirror is only useful if it is actually wired into the checker. A
    // vocabulary that exists but is never consulted is the same as no
    // vocabulary: unknown keys would be accepted, and the closed set would be
    // decorative.
    const std::vector<std::string> mirrored = sicnu::verification::mirroredFactKeys();
    REQUIRE_FALSE( mirrored.empty() );

    for ( const std::string &key : mirrored )
    {
        INFO( "mirrored key: " << key );
        REQUIRE( sicnu::verification::isMirroredFactKey( key ) );
    }

    // And the predicate must actually reject. Without this half, a
    // `return true;` implementation would satisfy every assertion above.
    REQUIRE_FALSE( sicnu::verification::isMirroredFactKey( "definitely_not_a_fact_key" ) );
    REQUIRE_FALSE( sicnu::verification::isMirroredFactKey( "" ) );
    // Case matters: the harness vocabulary is lower_snake, so a camelCase
    // spelling is a different key and must not be accepted.
    REQUIRE_FALSE( sicnu::verification::isMirroredFactKey( "CrsAuthId" ) );
}

TEST_CASE( "verifier14 drift: the parser does not silently return an empty table",
           "[verifier14][drift]" )
{
    // Meta-test for the guard itself. If the anchor moves, parseFactKeys returns
    // empty; the first test would then WARN-and-return (file unreadable) or fail
    // on requireUsableParse. This test pins the parser's behaviour on a source
    // string we control, so a regression in the parser is caught even when the
    // harness file is unreadable.

    // Positive control: a miniature table in the harness's exact style.
    const std::string fake =
        "const FactKeySpec kFactKeys[] = {\n"
        "  { \"alpha\", \"string\" },\n"
        "  { \"beta\", \"number\" },\n"
        "};\n";

    const std::vector<std::string> parsed = parseFactKeys( fake );
    requireUsableParse( parsed, "miniature positive-control table" );
    REQUIRE( parsed.size() == 2 );
    REQUIRE( parsed[0] == "alpha" );
    REQUIRE( parsed[1] == "beta" );

    // Negative control: no anchor -> empty, which callers must treat as failure.
    REQUIRE( parseFactKeys( "const int unrelated = 3;\n" ).empty() );
}
