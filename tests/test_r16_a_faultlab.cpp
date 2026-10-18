// tests/test_r16_a_faultlab.cpp — R16-A T02 regressions for the faultlab
// canonical/digest contract and the scenario loader's operand honesty.
//
// Cross-track item (T01): faultlab's canonical writer diverged from the
// verification/grader canonical families on exactly three points:
//   1. non-finite numbers were written into digests as the pseudo-JSON
//      literals "Infinity"/"-Infinity" (on jsoncpp 1.9.8, "1e999" parses
//      to +Inf with no parse error — older readers fail the text; the
//      digest floor below guards both worlds), so a scenario expectation
//      operand carried it all the way into reportDigest;
//   2. negative zero serialized as "-0" while verification (roundSignificant,
//      T01) and grader (canonicalNumber) both normalize it to "0";
//   3. the canonical walk had no recursion budget — a programmatically
//      constructed deep Json::Value crashed sha256HexOf with a stack
//      overflow (verified: exit 139 / SIGSEGV at depth 100000).
// NaN itself is the fault lab's documented no-data spelling (the header
// contract, pinned by the "canonical grid digest is stable and NaN-safe"
// test) and STAYS hashable; the digest layer refuses only what no fixture
// or scenario can legally carry.
#include "faultlab/fault_fixtures.h"
#include "faultlab/fault_types.h"
#include "faultlab/fault_sandbox.h"
#include "faultlab/fault_runner.h"
#include "faultlab/fault_report.h"
#include "faultlab/util/canonical_json.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace sicnu::faultlab;

namespace
{

/// Minimal valid scenario document (same shape as test_faultlab.cpp's
/// helper; kept local so this file stays independently runnable).
Json::Value validScenarioDoc()
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = "fs_band_role_swap_ndvi";
    doc["title"] = "Band role swap on an index pair";

    Json::Value fault( Json::objectValue );
    fault["family"] = "band_role_swap";
    Json::Value params( Json::objectValue );
    params["role_a"] = "red";
    params["role_b"] = "nir";
    fault["params"] = params;
    fault["seed"] = 42u;
    doc["fault"] = fault;

    Json::Value fixture( Json::objectValue );
    fixture["fixture_id"] = "two_band_index_pair";
    fixture["params"] = Json::objectValue;
    fixture["seed"] = 42u;
    doc["base_fixture"] = fixture;

    Json::Value expected( Json::objectValue );
    Json::Value obs( Json::arrayValue );
    Json::Value o1( Json::objectValue );
    o1["id"] = "band_roles";
    o1["relation"] = "changed";
    obs.append( o1 );
    Json::Value o2( Json::objectValue );
    o2["id"] = "index_mean";
    o2["relation"] = "delta_le";
    o2["value"] = -0.5;
    obs.append( o2 );
    expected["observables"] = obs;
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = "all_negative_index";
    expected["diagnosis"] = diagnosis;
    doc["expected"] = expected;

    Json::Value lo( Json::objectValue );
    lo["id"] = "LO-03";
    lo["statement"] = "Recognize that band metadata, not band position, defines an index.";
    doc["learning_objective"] = lo;
    return doc;
}

} // namespace

// ---------------------------------------------------------------------------
// WP-A — canonical number spellings
// ---------------------------------------------------------------------------

TEST_CASE( "r16-a faultlab: negative zero canonicalizes onto the aligned '0' spelling",
           "[faultlab][r16a][canonical]" )
{
    // verification's roundSignificant(-0.0) and grader's canonicalNumber both
    // print "0"; faultlab printed "-0" (%.17g), the last holdout of the three
    // digest families. -0.0 == 0.0 numerically; one spelling for both.
    Json::Value doc( Json::objectValue );
    doc["neg"] = -0.0;
    doc["pos"] = 0.0;
    CHECK( canonical::toCanonicalString( doc ) == "{\"neg\":0,\"pos\":0}" );

    // Both zeros digest identically (same numeric value, same bytes).
    Json::Value negZero( Json::objectValue );
    negZero["v"] = -0.0;
    Json::Value posZero( Json::objectValue );
    posZero["v"] = 0.0;
    CHECK( canonical::sha256HexOf( negZero ) == canonical::sha256HexOf( posZero ) );
}

TEST_CASE( "r16-a faultlab: NaN keeps the no-data spelling, ±Infinity is refused",
           "[faultlab][r16a][canonical]" )
{
    // NaN is the fault lab's no-data marker (fault_types.h: "NaN marks a
    // no-data sample"); the canonical form documents it as the literal NaN.
    // The spelling is now pinned exactly, so a future formatter change cannot
    // silently move every grid digest.
    Json::Value nanDoc( Json::objectValue );
    nanDoc["v"] = std::nan( "" );
    CHECK( canonical::toCanonicalString( nanDoc ) == "{\"v\":NaN}" );

    // A negative NaN (sign bit set) must normalize onto the same spelling,
    // not "-nan": the digest may not depend on payload bits nobody controls.
    Json::Value negNan( Json::objectValue );
    negNan["v"] = -std::nan( "" );
    CHECK( canonical::toCanonicalString( negNan ) == "{\"v\":NaN}" );

    // ±Infinity has no feature meaning anywhere in the lab (noData is NaN,
    // fill_value/gain/offset/cell_size are all finite-gated) and cannot be
    // spelled as JSON at all: an unhashable value must not reach a digest.
    // Fail-closed spelling of the value-returning API: the empty string.
    Json::Value inf( Json::objectValue );
    inf["v"] = HUGE_VAL;
    CHECK( canonical::sha256HexOf( inf ).empty() );
    Json::Value negInf( Json::objectValue );
    negInf["v"] = -HUGE_VAL;
    CHECK( canonical::sha256HexOf( negInf ).empty() );
    CHECK( canonical::toCanonicalString( inf ).empty() );
}

// ---------------------------------------------------------------------------
// WP-C — scenario loader operand honesty (fail-open #1464 class)
// ---------------------------------------------------------------------------

TEST_CASE( "r16-a faultlab: non-finite expectation operands are refused typed",
           "[faultlab][r16a][loader]" )
{
    // jsoncpp parses "1e999" to +Infinity WITHOUT a parse error on 1.9.8
    // (probed on the linked build; older readers like 1.9.5 fail the text
    // instead — the value-level guard still matters for programmatic
    // values on every version), so the operand sailed through
    // isNumeric()/asDouble() into ExpectationResult.expected and from
    // there into reportDigest as the literal "Infinity". The loader is
    // the door: finite or refuse.
    SECTION( "delta_ge value = +Infinity" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["value"] = HUGE_VAL;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        REQUIRE( !result.diagnostics.empty() );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "delta_ge value = -Infinity" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["value"] = -HUGE_VAL;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "in_range range_lo = +Infinity" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["relation"] = "in_range";
        doc["expected"]["observables"][1]["range_lo"] = HUGE_VAL;
        doc["expected"]["observables"][1]["range_hi"] = 2.0;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "in_range range_hi = -Infinity" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["relation"] = "in_range";
        doc["expected"]["observables"][1]["range_lo"] = 0.0;
        doc["expected"]["observables"][1]["range_hi"] = -HUGE_VAL;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
}

TEST_CASE( "r16-a faultlab: present-but-mistyped expectation operands are refused, never silently defaulted",
           "[faultlab][r16a][loader]" )
{
    // A present operand with the wrong JSON type used to be SKIPPED
    // (expectation.value kept its 0.0 default), so {"relation":"delta_ge",
    // "value":"0.5"} graded as delta >= 0 — a vacuous pass on garbage. Same
    // doctrine as the r8/r12 transform gates: declared-but-unusable is a
    // typed refusal, never a silent default.
    SECTION( "string value on a numeric relation" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["value"] = "0.5";
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        REQUIRE( !result.diagnostics.empty() );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "boolean value on a numeric relation" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["value"] = true;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "string range_lo on in_range" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][1]["relation"] = "in_range";
        doc["expected"]["observables"][1]["range_lo"] = "0";
        doc["expected"]["observables"][1]["range_hi"] = 2.0;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "numeric text operand" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][0]["relation"] = "equals";
        doc["expected"]["observables"][0]["text"] = 7;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "absent operands keep the legacy defaults (changed carries none)" )
    {
        const auto result = loadFaultScenario( validScenarioDoc() );
        REQUIRE( result.ok );
        REQUIRE( result.value.expectations.size() == 2 );
        CHECK( result.value.expectations[0].relation == ExpectationRelation::Changed );
        CHECK( result.value.expectations[0].value == 0.0 );
    }
}

TEST_CASE( "r16-a faultlab: non-empty base_fixture.params is refused, not silently ignored",
           "[faultlab][r16a][loader]" )
{
    // makeFixture documents "v1 fixtures take no parameters" and ignores the
    // params object entirely: an author declaring {"width": 64} silently got
    // the 16x16 fixture — the #1464 accepted-but-inert shape at the fixture
    // level (the transform level, cell_size, was closed in R8). Fail closed.
    Json::Value doc = validScenarioDoc();
    doc["base_fixture"]["params"]["width"] = 64;
    const auto result = loadFaultScenario( doc );
    CHECK_FALSE( result.ok );
    REQUIRE( !result.diagnostics.empty() );
    CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );

    // The empty object stays legal (schema symmetry, every existing producer).
    Json::Value emptyParams = validScenarioDoc();
    emptyParams["base_fixture"]["params"] = Json::objectValue;
    CHECK( loadFaultScenario( emptyParams ).ok );
}

// ---------------------------------------------------------------------------
// WP-A — typed refusal channel + recursion budget
// ---------------------------------------------------------------------------

namespace
{

/// A `depth`-deep object chain built in O(depth) by descending through
/// stable member references (jsoncpp's object map is node-based, so a
/// reference taken after each insertion stays valid while deeper levels are
/// added). This is the programmatic shape that used to overflow the
/// canonical walker's stack (SIGSEGV, exit 139 at depth 100000).
Json::Value deepChain( int depth )
{
    Json::Value root( Json::objectValue );
    Json::Value *cursor = &root;
    for ( int i = 0; i < depth; ++i )
    {
        Json::Value &next = ( *cursor )["n"];
        if ( !next.isObject() )
            next = Json::Value( Json::objectValue );
        cursor = &next;
    }
    ( *cursor )["leaf"] = 1;
    return root;
}

} // namespace

TEST_CASE( "r16-a faultlab: a deep Json::Value is a typed refusal, not a stack overflow",
           "[faultlab][r16a][canonical]" )
{
    // Depth budget is 256 canonical levels — double the scenario reader's
    // stackLimit (128), so every document that can PARSE still digests; a
    // 200-level chain is comfortably legal.
    std::string out;
    std::string error;
    CHECK( canonical::trySha256HexOf( deepChain( 200 ), out, error ) );
    CHECK( out.size() == 64 );

    // The pre-fix walker recursed per level and SIGSEGV'd around 100k
    // (probe transcript, exit 139). The fixed walker refuses at 257 levels
    // BEFORE recursing deep; the refusal depth is kept at 2000 because
    // jsoncpp's own Value destructor recurses per nesting level too (an
    // upstream property every jsoncpp consumer in this repo shares), so a
    // 200k-level value must not even be constructed in-process.
    Json::Value bomb = deepChain( 2000 );
    out.clear();
    error.clear();
    CHECK_FALSE( canonical::trySha256HexOf( bomb, out, error ) );
    CHECK( error.find( "depth budget" ) != std::string::npos );
    CHECK( out.empty() ); // untouched on failure
    // Value-returning form spells the same refusal as the empty digest.
    CHECK( canonical::sha256HexOf( bomb ).empty() );
    CHECK( canonical::toCanonicalString( bomb ).empty() );
}

TEST_CASE( "r16-a faultlab: try* canonical forms carry the refusal reason",
           "[faultlab][r16a][canonical]" )
{
    Json::Value doc( Json::objectValue );
    doc["v"] = 1;
    std::string out;
    std::string error;
    REQUIRE( canonical::tryToCanonicalString( doc, out, error ) );
    CHECK( out == "{\"v\":1}" );
    CHECK( error.empty() );

    Json::Value inf( Json::objectValue );
    inf["v"] = HUGE_VAL;
    std::string text;
    CHECK_FALSE( canonical::tryToCanonicalString( inf, text, error ) );
    CHECK( error.find( "non-finite" ) != std::string::npos );
    CHECK( text.empty() ); // a partial canonical form must not leak out
}

TEST_CASE( "r16-a faultlab: a single oversized scalar cannot bypass the output budget",
           "[faultlab][r16a][canonical]" )
{
    // counted() samples the byte budget at node ENTRY, so the walk used to
    // let one scalar blow straight past the 256 MiB ceiling mid-write: a
    // control-char string amplifies ~6x through \uXXXX escaping. ~43 MiB of
    // \x01 serializes to ~258 MiB — over budget, and it must be refused
    // after the walk, not accepted (the post-walk re-check is the cap).
    // One-shot ~400 MB transient RSS; cheap next to the budget it guards.
    const std::string payload( 43 * 1024 * 1024, '\x01' );
    Json::Value doc( Json::objectValue );
    doc["big"] = payload;
    std::string text;
    std::string error;
    CHECK_FALSE( canonical::tryToCanonicalString( doc, text, error ) );
    CHECK( error.find( "output byte budget" ) != std::string::npos );
    CHECK( text.empty() );
    CHECK( canonical::sha256HexOf( doc ).empty() );
}

TEST_CASE( "r16-a faultlab: builtin fixture digests are unchanged by the alignment",
           "[faultlab][r16a][canonical]" )
{
    // Golden contract: two_band_index_pair @ seed 42 carries no negative
    // zero and no infinity, so its digest is IDENTICAL before and after the
    // -0.0/±Infinity/depth alignment. If this pin ever moves, either the
    // fixture factory or the canonical number contract changed — re-pin in
    // the same commit, deliberately (the parity-test discipline).
    const auto fixture = makeFixture( "two_band_index_pair", Json::Value( Json::objectValue ), 42u );
    REQUIRE( fixture.ok );
    CHECK( canonical::sha256HexOf( fixture.value.toJson() ) ==
           "3e89e8507280924157c066f6dcd611b4f50dfe2769151d4d04ffeb50b95a9fa2" );

    // Every fixture still digests to a real (non-empty) digest — the NaN
    // no-data samples of quality_masked_pair included.
    for ( const std::string &id : fixtureIds() )
    {
        const auto made = makeFixture( id, Json::Value( Json::objectValue ), 42u );
        INFO( id );
        REQUIRE( made.ok );
        CHECK( canonical::sha256HexOf( made.value.toJson() ).size() == 64 );
    }
}

TEST_CASE( "r16-a faultlab: a report carrying an unhashable number gets no digest",
           "[faultlab][r16a][canonical]" )
{
    // The loader now refuses non-finite operands at the door, but the report
    // value object is public API: a hand-built report with an infinite
    // evidence number must fail closed at the digest layer too, never emit
    // canonical text with the literal "Infinity" in it.
    FaultRunReport report;
    report.scenarioId = "fs_probe";
    ExpectationResult poisoned;
    poisoned.id = "index_mean";
    poisoned.relation = ExpectationRelation::DeltaGe;
    poisoned.expected = HUGE_VAL;
    poisoned.observed = 0.8;
    report.expectationResults.push_back( poisoned );
    CHECK( faultReportToJson( report ).empty() );
    CHECK( faultReportDigest( report ).empty() );

    // The finite twin digests normally.
    report.expectationResults.front().expected = 0.5;
    CHECK( faultReportToJson( report ).find( "index_mean" ) != std::string::npos );
    CHECK( faultReportDigest( report ).size() == 64 );
}

// ---------------------------------------------------------------------------
// WP-C — reproducibility + sandbox lifecycle in a private root
// ---------------------------------------------------------------------------

TEST_CASE( "r16-a faultlab: same seed replays to identical digests with no sandbox residue",
           "[faultlab][r16a][runner]" )
{
    const auto loaded = loadFaultScenario( validScenarioDoc() );
    REQUIRE( loaded.ok );

    // A PRIVATE sandbox root makes the residue assertion deterministic: no
    // other test or process can leave directories in here.
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ( "r16a-faultlab-residue-" + std::to_string( ::getpid() ) );
    std::filesystem::create_directories( root );

    FaultRunOptions options;
    options.sandboxRoot = root.string();

    std::string firstDigest;
    std::string firstReplay;
    for ( int run = 0; run < 3; ++run )
    {
        const auto result = runFaultScenario( loaded.value, options );
        REQUIRE( result.ok );
        const FaultRunReport &report = result.value;
        CHECK( report.passed );
        CHECK( report.sandboxRemoved );
        CHECK( report.sourceUnchanged );
        CHECK( report.replayDeterministic );
        if ( run == 0 )
        {
            firstDigest = faultReportDigest( report );
            firstReplay = report.replayDigest;
            REQUIRE( firstDigest.size() == 64 );
            REQUIRE( firstReplay.size() == 64 );
        }
        else
        {
            CHECK( faultReportDigest( report ) == firstDigest );
            CHECK( report.replayDigest == firstReplay );
        }
    }

    // After the runs the private root holds nothing: every sandbox was
    // cleaned and verified, and the runner never leaks its temp copies.
    int residue = 0;
    for ( const auto &entry : std::filesystem::directory_iterator( root ) )
    {
        ++residue;
        INFO( "residue: " << entry.path().string() );
    }
    CHECK( residue == 0 );
    std::filesystem::remove_all( root );
}
