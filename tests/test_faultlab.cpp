// test_faultlab.cpp — core semantics of the Scientific Fault Injection Lab
// Framework (RS14-13). Slice A: scenario schema gate, sandbox contract,
// deterministic core types. Slice tests are appended per TDD slice; every
// TEST_CASE name is prefixed "fault lab: " so `ctest -R 'fault lab'` selects
// the suite regardless of executable name.
#include "faultlab/deterministic.h"
#include "faultlab/fault_registry.h"
#include "faultlab/fault_sandbox.h"
#include "faultlab/fault_types.h"
#include "faultlab/util/canonical_json.h"
#include "faultlab/util/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace sicnu::faultlab;

namespace
{
/// Minimal valid scenario document (sicnu.lab.faults/1). Individual tests
/// mutate copies to probe the validation rules.
Json::Value validScenarioDoc()
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = "fs_band_role_swap_ndvi";
    doc["title"] = "Band role swap on an index pair";
    doc["title_zh"] = "指数波段对上的波段角色互换";

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

    Json::Value sandbox( Json::objectValue );
    sandbox["class"] = "temp_copy";
    sandbox["max_bytes"] = Json::Value::UInt64( 1048576 );
    sandbox["require_source_unchanged"] = true;
    doc["sandbox"] = sandbox;

    Json::Value expected( Json::objectValue );
    Json::Value obs( Json::arrayValue );
    Json::Value o1( Json::objectValue );
    o1["id"] = "band_roles";
    o1["relation"] = "changed";
    obs.append( o1 );
    Json::Value o2( Json::objectValue );
    o2["id"] = "index_mean";
    o2["relation"] = "delta_ge";
    o2["value"] = 0.5;
    obs.append( o2 );
    expected["observables"] = obs;
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = "all_negative_index";
    expected["diagnosis"] = diagnosis;
    doc["expected"] = expected;

    Json::Value lo( Json::objectValue );
    lo["id"] = "LO-03";
    lo["statement"] = "Recognize that band metadata, not band position, defines an index.";
    lo["statement_zh"] = "认识到定义指数的是波段元数据而不是波段位置。";
    doc["learning_objective"] = lo;

    Json::Value cleanup( Json::objectValue );
    cleanup["required"] = true;
    cleanup["verify_no_residue"] = true;
    doc["cleanup"] = cleanup;
    return doc;
}

FaultGrid sampleGrid()
{
    FaultGrid grid;
    grid.width = 4;
    grid.height = 3;
    grid.crsId = "EPSG:4326";
    grid.hasNoData = true;
    grid.noDataValue = -9999.0;
    BandSpec red;
    red.role = "red";
    red.samples = { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2 };
    BandSpec nir;
    nir.role = "nir";
    nir.samples = { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };
    grid.bands = { red, nir };
    return grid;
}
} // namespace

// ---------------------------------------------------------------------------
// Slice A — schema gate
// ---------------------------------------------------------------------------

TEST_CASE( "fault lab: scenario loader accepts sicnu.lab.faults/1", "[faultlab]" )
{
    const auto result = loadFaultScenario( validScenarioDoc() );
    REQUIRE( result.ok );
    CHECK( result.value.scenarioId == "fs_band_role_swap_ndvi" );
    CHECK( result.value.fault.familyId == "band_role_swap" );
    CHECK( result.value.fault.seed == 42u );
    CHECK( result.value.fixtureId == "two_band_index_pair" );
    CHECK( result.value.maxBytes == 1048576u );
    CHECK( result.value.expectedDiagnosisSignature == "all_negative_index" );
    CHECK( result.value.learningObjectiveId == "LO-03" );
    REQUIRE( result.value.expectations.size() == 2 );
    CHECK( result.value.expectations[0].relation == ExpectationRelation::Changed );
    CHECK( result.value.expectations[1].relation == ExpectationRelation::DeltaGe );
    CHECK( result.diagnostics.empty() );
}

TEST_CASE( "fault lab: scenario loader rejects foreign schema versions", "[faultlab]" )
{
    Json::Value doc = validScenarioDoc();
    doc["schema_version"] = "sicnu.lab.faults/2";
    const auto result = loadFaultScenario( doc );
    CHECK_FALSE( result.ok );
    REQUIRE( !result.diagnostics.empty() );
    CHECK( result.diagnostics.front().code == "faultlab.schema_version" );
}

TEST_CASE( "fault lab: scenario loader rejects a missing schema version", "[faultlab]" )
{
    Json::Value doc = validScenarioDoc();
    doc.removeMember( "schema_version" );
    const auto result = loadFaultScenario( doc );
    CHECK_FALSE( result.ok );
    REQUIRE( !result.diagnostics.empty() );
    CHECK( result.diagnostics.front().code == "faultlab.schema_version" );
}

TEST_CASE( "fault lab: scenario loader rejects an unknown fault family", "[faultlab]" )
{
    Json::Value doc = validScenarioDoc();
    doc["fault"]["family"] = "cosmic_ray_zap";
    const auto result = loadFaultScenario( doc );
    CHECK_FALSE( result.ok );
    REQUIRE( !result.diagnostics.empty() );
    CHECK( result.diagnostics.front().code == "faultlab.fault_unknown_family" );
}

TEST_CASE( "fault lab: scenario loader rejects malformed required fields", "[faultlab]" )
{
    SECTION( "scenario id outside the closed pattern" )
    {
        Json::Value doc = validScenarioDoc();
        doc["scenario_id"] = "BandRoleSwap";
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "no observable expectations" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"] = Json::arrayValue;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "unknown expectation relation" )
    {
        Json::Value doc = validScenarioDoc();
        doc["expected"]["observables"][0]["relation"] = "roughly";
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_relation" );
    }
    SECTION( "learning objective id outside the closed pattern" )
    {
        Json::Value doc = validScenarioDoc();
        doc["learning_objective"]["id"] = "objective-3";
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_objective" );
    }
    SECTION( "sandbox byte budget above the schema cap" )
    {
        Json::Value doc = validScenarioDoc();
        doc["sandbox"]["max_bytes"] = Json::Value::UInt64( 128ull * 1024 * 1024 );
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
    SECTION( "cleanup may not be waived" )
    {
        Json::Value doc = validScenarioDoc();
        doc["cleanup"]["required"] = false;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
        CHECK( result.diagnostics.front().code == "faultlab.scenario_field" );
    }
}

TEST_CASE( "fault lab: scenario loader accepts every registered fault family", "[faultlab]" )
{
    for ( const auto &family : faultFamilyCatalog() )
    {
        Json::Value doc = validScenarioDoc();
        doc["fault"]["family"] = family.id;
        const auto result = loadFaultScenario( doc );
        INFO( family.id );
        CHECK( result.ok );
    }
}

// ---------------------------------------------------------------------------
// Slice A — sandbox contract
// ---------------------------------------------------------------------------

TEST_CASE( "fault lab: sandbox copies are independent deep copies", "[faultlab]" )
{
    const auto sandbox = FaultSandbox::create();
    REQUIRE( sandbox.ok );
    CHECK( sandbox->active() );
    CHECK_FALSE( sandbox->path().empty() );

    const FaultGrid source = sampleGrid();
    FaultGrid copy = FaultSandbox::copyOf( source );

    // Mutating the copy must never reach the source.
    copy.bands[0].role = "nir";
    copy.bands[1].role = "red";
    copy.bands[0].samples[0] = 999.0;
    copy.extras["marker"] = "faulted";

    CHECK( source.bands[0].role == "red" );
    CHECK( source.bands[1].role == "nir" );
    CHECK( source.bands[0].samples[0] == 0.1 );
    CHECK( source.extras.isObject() );
    CHECK( source.extras.getMemberNames().empty() );
}

TEST_CASE( "fault lab: sandbox cleanup verifies no residue", "[faultlab]" )
{
    std::string path;
    {
        auto sandbox = FaultSandbox::create();
        REQUIRE( sandbox.ok );
        path = sandbox->path();
        CHECK( sandbox->verifyNoResidue() == false ); // still present
        sandbox->cleanup();
        CHECK( sandbox->verifyNoResidue() );
        CHECK_FALSE( sandbox->active() );
    }
    // Destructor of a cleaned sandbox is a no-op and leaves nothing behind.
    CHECK_FALSE( std::filesystem::exists( path ) );
}

TEST_CASE( "fault lab: sandbox destructor cleans up an abandoned sandbox", "[faultlab]" )
{
    std::string path;
    {
        const auto sandbox = FaultSandbox::create();
        REQUIRE( sandbox.ok );
        path = sandbox->path();
        CHECK( std::filesystem::exists( path ) );
    }
    CHECK_FALSE( std::filesystem::exists( path ) );
}

TEST_CASE( "fault lab: sandbox enforces the scenario byte budget", "[faultlab]" )
{
    const auto sandbox = FaultSandbox::create();
    REQUIRE( sandbox.ok );
    const FaultGrid source = sampleGrid();
    // A 4x3 two-band float grid is ~192 bytes of samples: a 1 MiB budget
    // admits it, a 1-byte budget must refuse before any copy is made.
    const auto refused = FaultSandbox::copyWithinBudget( source, 1 );
    CHECK_FALSE( refused.ok );
    REQUIRE( !refused.diagnostics.empty() );
    CHECK( refused.diagnostics.front().code == "faultlab.budget_exceeded" );
    const auto copied = FaultSandbox::copyWithinBudget( source, 1048576 );
    CHECK( copied.ok );
    CHECK( copied.value.bands.size() == 2 );
}

// ---------------------------------------------------------------------------
// Slice A — determinism primitives
// ---------------------------------------------------------------------------

TEST_CASE( "fault lab: deterministic prng replays identically for a seed", "[faultlab]" )
{
    deterministic::Pcg32 a( 42 );
    deterministic::Pcg32 b( 42 );
    deterministic::Pcg32 c( 43 );
    for ( int i = 0; i < 16; ++i )
    {
        const auto va = a.nextU32();
        CHECK( va == b.nextU32() );
        CHECK( va != c.nextU32() );
    }
    for ( int i = 0; i < 16; ++i )
    {
        CHECK( a.nextDouble() >= 0.0 );
        CHECK( a.nextDouble() < 1.0 );
    }
}

TEST_CASE( "fault lab: derived seeds do not perturb each other", "[faultlab]" )
{
    const auto split = deterministic::seedFor( 42, "split" );
    const auto patch = deterministic::seedFor( 42, "patch" );
    const auto splitAgain = deterministic::seedFor( 42, "split" );
    CHECK( split != patch );
    CHECK( split == splitAgain );
}

// ---------------------------------------------------------------------------
// Slice A — canonical json + digest
// ---------------------------------------------------------------------------

TEST_CASE( "fault lab: canonical json is byte-stable across runs", "[faultlab]" )
{
    Json::Value doc( Json::objectValue );
    doc["zeta"] = 1;
    doc["alpha"] = 2.5;
    doc["middle"] = "text";
    Json::Value arr( Json::arrayValue );
    arr.append( 3 );
    arr.append( 4 );
    doc["array"] = arr;

    const std::string first = canonical::toCanonicalString( doc );
    const std::string second = canonical::toCanonicalString( doc );
    CHECK( first == second );
    // Keys sorted, arrays in order, floats pinned to a stable format.
    CHECK( first.find( "\"alpha\":2.5" ) != std::string::npos );
    CHECK( first.find( "\"array\":[3,4]" ) != std::string::npos );
    CHECK( first.find( "\"alpha\"" ) < first.find( "\"middle\"" ) );
    CHECK( first.find( "\"middle\"" ) < first.find( "\"zeta\"" ) );
}

TEST_CASE( "fault lab: sha256 matches the FIPS 180-4 known answers", "[faultlab]" )
{
    CHECK( sha256Hex( "" ) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    CHECK( sha256Hex( "abc" ) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    const std::string longMsg( 1000, 'a' );
    CHECK( sha256Hex( longMsg ) ==
           "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" );
}

TEST_CASE( "fault lab: canonical grid digest is stable and NaN-safe", "[faultlab]" )
{
    FaultGrid grid = sampleGrid();
    grid.bands[0].samples[3] = std::nan( "" );
    const std::string first = canonical::sha256HexOf( grid.toJson() );
    const std::string second = canonical::sha256HexOf( grid.toJson() );
    CHECK( first == second );
    CHECK( first.size() == 64 );
    // A mutated copy must digest differently.
    FaultGrid other = FaultSandbox::copyOf( grid );
    other.bands[1].scale = 2.0;
    CHECK( canonical::sha256HexOf( other.toJson() ) != first );
}
