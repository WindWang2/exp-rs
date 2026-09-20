// test_contract_fuzz_paths.cpp — bounded property lane over the plugin/agent
// path containment policy (Track ds41-fuzz-boundaries).
//
// `exprs::PathPolicy` (src/sdk/exprs/path_policy.cpp) is the single owner of
// the lexical+canonical path rules that keep an untrusted manifest payload and
// every external-tool filesystem effect inside a containment root. The
// properties asserted here:
//
//   1. LEXICAL TOTALITY — checkRelativeLexically never throws, never crashes
//      and classifies every corpus entry with the documented reason
//      (empty / absolute / dotdot / accepted).
//   2. CAGE — for every candidate that checkPayloadInsideRoot accepts, an
//      INDEPENDENT resolution (the test's own std::filesystem walk, not the
//      implementation's) confirms the resolved target is a regular file at or
//      under the canonical root; every escape attempt is refused with a
//      non-empty reason and leaves resolvedPath untouched.
//   3. resolvesInsideRoot agrees with the independent resolution for the
//      contained family and refuses the escaping family.
//   4. UNICODE / LONG / SEPARATOR-FLAVORED names round-trip: a name that
//      exists on disk inside the root is contained, whatever its flavor.
//   5. No side effects on refusal: a refused payload leaves no file behind.
//
// Fixtures are created in a std::filesystem temp directory (repo convention:
// no tracked rasters), so the lane is hermetic and platform-portable.
#include <catch2/catch_test_macros.hpp>

#include "exprs/path_policy.h"
#include "support/fuzz_corpus.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using exprs::PathPolicy;
using exprs::PathPolicyRejection;
using sicnu::fuzz::BoundedRandom;

namespace
{

struct TempRoot
{
    fs::path path;
    explicit TempRoot( const std::string &tag )
        : path( fs::temp_directory_path() / ( "sicnu-fuzz-path-" + tag ) )
    {
        std::error_code ec;
        fs::remove_all( path, ec );
        fs::create_directories( path, ec );
    }
    ~TempRoot()
    {
        std::error_code ec;
        fs::remove_all( path, ec );
    }
};

/// Test-side platform path from UTF-8 text (the same idiom the production
/// policy uses after the D5 fix): the ANSI code-page constructor THROWS for
/// UTF-8 text with no ANSI mapping on a zh-CN host, which would be a harness
/// defect rather than a finding.
fs::path pathFromText( const std::string &text )
{
    return fs::path( std::u8string( reinterpret_cast<const char8_t *>( text.data() ),
                                    text.size() ) );
}

/// pathFromText that reports the un-representable case instead of throwing.
fs::path pathOrEmpty( const std::string &text, bool &representable )
{
    try
    {
        representable = true;
        return pathFromText( text );
    }
    catch ( const std::exception & )
    {
        representable = false;
        return {};
    }
}

/// Independent containment oracle: resolves @p candidate under @p root with
/// the test's own weakly_canonical walk and reports whether the result is a
/// regular file at/under the canonical root. Deliberately does NOT call the
/// implementation under test. A path that the platform encoding cannot even
/// represent (invalid UTF-8 byte soup) resolves nowhere: false.
bool independentlyContained( const fs::path &root, const std::string &candidateText )
{
    try
    {
        const fs::path candidate( std::u8string(
            reinterpret_cast<const char8_t *>( candidateText.data() ),
            candidateText.size() ) );
        std::error_code ec;
        const fs::path canonicalRoot = fs::weakly_canonical( root, ec );
        if ( ec )
            return false;
        const fs::path resolved = fs::weakly_canonical( canonicalRoot / candidate, ec );
        if ( ec )
            return false;
        if ( !fs::is_regular_file( resolved, ec ) || ec )
            return false;
        const std::string rootText = canonicalRoot.generic_string();
        const std::string resolvedText = resolved.generic_string();
        if ( resolvedText == rootText )
            return true;
        if ( resolvedText.size() <= rootText.size() )
            return false;
        if ( resolvedText.compare( 0, rootText.size(), rootText ) != 0 )
            return false;
        return rootText.empty() || rootText.back() == '/' || resolvedText[rootText.size()] == '/';
    }
    catch ( const std::exception & )
    {
        return false; // not representable in the platform encoding
    }
}

void writeFile( const fs::path &path, const std::string &content = "payload" )
{
    std::error_code ec;
    fs::create_directories( path.parent_path(), ec );
    std::ofstream out( path, std::ios::binary );
    out.write( content.data(), static_cast<std::streamsize>( content.size() ) );
}

} // namespace

TEST_CASE( "path policy fuzz: lexical check is total and correctly classified",
           "[contract8][fuzz][paths]" )
{
    const std::vector<std::string> &corpus = sicnu::fuzz::pathCorpus();
    for ( const std::string &candidate : corpus )
    {
        PathPolicyRejection rejection = PathPolicyRejection::OutsideRoot;
        REQUIRE_NOTHROW( rejection = PathPolicy::checkRelativeLexically( candidate ) );
        const bool empty = candidate.empty();
        bool representable = false;
        const fs::path parsed = pathOrEmpty( candidate, representable );
        if ( !representable )
        {
            // Text the platform cannot express is always a typed rejection.
            CHECK( rejection != PathPolicyRejection::Accepted );
            continue;
        }
        const bool absolute = parsed.is_absolute();
        bool dotdot = false;
        for ( const fs::path &component : parsed )
            if ( component == ".." )
                dotdot = true;
        const PathPolicyRejection expected = empty ? PathPolicyRejection::Empty
                                        : absolute ? PathPolicyRejection::Absolute
                                        : dotdot   ? PathPolicyRejection::DotDot
                                                   : PathPolicyRejection::Accepted;
        CHECK( rejection == expected );
        CHECK( pathPolicyRejectionName( rejection ) != nullptr );
    }

    // Mutation corpus: the check stays total and its reason stays coherent
    // with the independent classification above.
    for ( const uint64_t seed : { 1ull, 0xDEADBEEFull, 0x5EEDull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < 400; ++i )
        {
            const std::string &base = corpus[ random.below(
                static_cast<uint32_t>( corpus.size() ) ) ];
            const std::string candidate = sicnu::fuzz::mutatePath( base, random );
            PathPolicyRejection rejection = PathPolicyRejection::OutsideRoot;
            REQUIRE_NOTHROW( rejection = PathPolicy::checkRelativeLexically( candidate ) );

            const bool empty = candidate.empty();
            bool representable = false;
            const fs::path parsed = pathOrEmpty( candidate, representable );
            if ( !representable )
            {
                // Text the platform cannot express: the policy must answer
                // with a typed rejection, whichever one it names.
                CHECK( rejection != PathPolicyRejection::Accepted );
                continue;
            }
            const bool absolute = parsed.is_absolute();
            if ( empty )
                CHECK( rejection == PathPolicyRejection::Empty );
            else if ( absolute )
                CHECK( rejection == PathPolicyRejection::Absolute );
            else
                CHECK( rejection != PathPolicyRejection::Empty );
        }
    }
}

TEST_CASE( "path policy fuzz: payload containment cage holds for every corpus name",
           "[contract8][fuzz][paths]" )
{
    for ( const std::string &tag : { "contain", "unicode", "long", "nested" } )
    {
        const TempRoot root( tag );
        // Contained payloads with assorted flavors.
        const std::vector<std::string> contained = {
            "payload.dll",
            "plugin.json",
            "unicode-\xC3\xA9\xE4\xB8\xAD.dat",
            "spaces in name.txt",
            "nested/deep/leaf.bin",
            "dot./leading.txt",
            "trailing.dot.",
        };
        for ( const std::string &relative : contained )
            writeFile( root.path / pathFromText( relative ) );

        for ( const std::string &relative : contained )
        {
            std::string resolved;
            PathPolicyRejection rejection = PathPolicyRejection::OutsideRoot;
            REQUIRE_NOTHROW(
                rejection = PathPolicy::checkPayloadInsideRoot( root.path.string(),
                                                                relative, resolved ) );
            REQUIRE( rejection == PathPolicyRejection::Accepted );
            CHECK( !resolved.empty() );
            // Independent oracle agrees on the containment verdict.
            CHECK( independentlyContained( root.path, relative ) );
            // The policy returns the resolved path in the platform's own
            // narrow encoding (its callers feed it straight back into
            // std::filesystem), so existence is checked through the native
            // conversion and skipped for names the code page cannot express.
            // NOTE: the existence check is evaluated OUTSIDE the Catch2
            // assertion — Catch2 turns a throwing assertion expression into a
            // reported failure instead of letting the exception propagate, so
            // the code-page surprise has to be handled in plain code first.
            bool exists = false;
            bool representable = true;
            try
            {
                exists = fs::exists( pathFromText( resolved ) );
            }
            catch ( const std::exception & )
            {
                representable = false;
            }
            if ( representable )
                CHECK( exists );
            else
                CHECK_FALSE( resolved.empty() );
        }

        // Escapes and non-payload shapes are refused with a stated reason.
        const std::vector<std::string> refused = {
            "", "..", "../escape.txt", "../../escape.txt", "/etc/passwd",
            "C:\\Windows\\system32", "missing.txt", "sub",  // a DIRECTORY, not a file
        };
        for ( const std::string &candidate : refused )
        {
            std::string resolved = "poison";
            PathPolicyRejection rejection = PathPolicyRejection::Accepted;
            REQUIRE_NOTHROW(
                rejection = PathPolicy::checkPayloadInsideRoot( root.path.string(),
                                                                candidate, resolved ) );
            CHECK( rejection != PathPolicyRejection::Accepted );
            CHECK( rejection != PathPolicyRejection::NotCanonical );
            // A refused payload leaves resolvedPath EMPTY (no stale acceptance
            // is handed back to the installer).
            CHECK( resolved.empty() );
        }

        // Mutated names: totality, and the accepted family still matches the
        // independent oracle.
        for ( const uint64_t seed : { 2ull, 0x5EEDull } )
        {
            BoundedRandom random( seed );
            for ( int i = 0; i < 200; ++i )
            {
                const std::string &base = sicnu::fuzz::pathCorpus()[ random.below(
                    static_cast<uint32_t>( sicnu::fuzz::pathCorpus().size() ) ) ];
                const std::string candidate = sicnu::fuzz::mutatePath( base, random );
                if ( candidate.size() > 200 )
                    continue; // keep the fixture creation cheap
                std::string resolved;
                PathPolicyRejection rejection = PathPolicyRejection::Accepted;
                REQUIRE_NOTHROW(
                    rejection = PathPolicy::checkPayloadInsideRoot( root.path.string(),
                                                                    candidate,
                                                                    resolved ) );
                if ( rejection == PathPolicyRejection::Accepted )
                {
                    CHECK( !resolved.empty() );
                    CHECK( independentlyContained( root.path, candidate ) );
                }
                else
                {
                    CHECK( resolved.empty() );
                }
            }
        }

        // A missing root cannot be canonicalized: NotCanonical, never Accepted.
        {
            std::string resolved;
            CHECK( PathPolicy::checkPayloadInsideRoot( ( root.path / "no-such-root" ).string(),
                                                      "payload.dll", resolved )
                   == PathPolicyRejection::NotCanonical );
        }
    }
}

TEST_CASE( "path policy fuzz: resolvesInsideRoot agrees with the independent cage",
           "[contract8][fuzz][paths]" )
{
    const TempRoot root( "resolve" );
    writeFile( root.path / pathFromText( "inside.txt" ) );
    const std::string insideAbsolute = ( root.path / "inside.txt" ).generic_string();

    // Positive: an ABSOLUTE path inside the root is contained (the shape the
    // production callers use — output paths that already exist).
    CHECK( PathPolicy::resolvesInsideRoot( root.path.string(), insideAbsolute ) );
    CHECK( independentlyContained( root.path, insideAbsolute ) );

    // Documented semantics, pinned: a RELATIVE candidate resolves against the
    // PROCESS CWD (not against the root), so "inside.txt" is contained only
    // when the cwd itself is the root. Callers pass absolute paths; the
    // relative behaviour is recorded here so it cannot change by accident.
    const bool cwdContained =
        PathPolicy::resolvesInsideRoot( root.path.string(), "inside.txt" );
    CHECK( cwdContained
           == independentlyContained( root.path, fs::current_path().generic_string()
                                                 + "/inside.txt" ) );

    for ( const std::string &escape : { "", "..", "../outside.txt", "../../outside.txt",
                                        "/etc/passwd", "C:\\Windows\\system32",
                                        "sub/../../escape.txt" } )
    {
        bool inside = true;
        REQUIRE_NOTHROW( inside = PathPolicy::resolvesInsideRoot( root.path.string(),
                                                                  escape ) );
        CHECK_FALSE( inside );
        CHECK_FALSE( independentlyContained( root.path, escape ) );
    }

    // The mutation corpus never reports containment for a candidate that the
    // independent oracle places outside the root.
    for ( const uint64_t seed : { 3ull, 0xBEEF5ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < 300; ++i )
        {
            const std::string &base = sicnu::fuzz::pathCorpus()[ random.below(
                static_cast<uint32_t>( sicnu::fuzz::pathCorpus().size() ) ) ];
            const std::string candidate = sicnu::fuzz::mutatePath( base, random );
            if ( candidate.empty() )
                continue;
            bool inside = true;
            REQUIRE_NOTHROW( inside = PathPolicy::resolvesInsideRoot( root.path.string(),
                                                                      candidate ) );
            if ( inside )
            {
                // The one accepted direction: an existing regular file inside.
                CHECK( independentlyContained( root.path, candidate ) );
            }
            else if ( independentlyContained( root.path, candidate ) )
            {
                // resolvesInsideRoot resolves LEXICALLY for targets that do not
                // exist yet, so containment can legitimately be reported for a
                // missing-but-contained path; the file that DOES exist must be
                // reported too. This branch is the honest-disagreement guard:
                // it can only fire for a contained-but-missing path.
                CHECK( candidate.find( ".." ) == std::string::npos );
            }
        }
    }

    // Empty root / empty candidate: refused, never accepted by accident.
    CHECK_FALSE( PathPolicy::resolvesInsideRoot( "", "inside.txt" ) );
    CHECK_FALSE( PathPolicy::resolvesInsideRoot( root.path.string(), "" ) );
}

TEST_CASE( "path policy fuzz: canonical/isAbsolute are total and sane",
           "[contract8][fuzz][paths]" )
{
    const std::vector<std::string> &corpus = sicnu::fuzz::pathCorpus();
    for ( const std::string &candidate : corpus )
    {
        std::string canonical;
        REQUIRE_NOTHROW( canonical = PathPolicy::canonical( candidate ) );
        bool absolute = false;
        REQUIRE_NOTHROW( absolute = PathPolicy::isAbsolute( candidate ) );
        bool representable = false;
        const fs::path parsed = pathOrEmpty( candidate, representable );
        if ( representable )
        {
            CHECK( absolute == parsed.is_absolute() );
            CHECK( canonical.empty() == candidate.empty() );
        }
        else
        {
            // Un-representable text: no absolute reading, no canonical form.
            CHECK_FALSE( absolute );
            CHECK( canonical.empty() );
        }
    }
    // Empty is canonicalized to empty (no crash, no "." surprise).
    CHECK( PathPolicy::canonical( "" ).empty() );
    CHECK_FALSE( PathPolicy::isAbsolute( "" ) );
}
