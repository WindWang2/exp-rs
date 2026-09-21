// test_faultlab.cpp — core semantics of the Scientific Fault Injection Lab
// Framework (RS14-13). Slice A: scenario schema gate, sandbox contract,
// deterministic core types. Slice tests are appended per TDD slice; every
// TEST_CASE name is prefixed "fault lab: " so `ctest -R 'fault lab'` selects
// the suite regardless of executable name.
#include "faultlab/deterministic.h"
#include "faultlab/fault_expectations.h"
#include "faultlab/fault_observables.h"
#include "faultlab/fault_registry.h"
#include "faultlab/fault_sandbox.h"
#include "faultlab/fault_transforms.h"
#include "faultlab/fault_types.h"
#include "faultlab/util/canonical_json.h"
#include "faultlab/util/sha256.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
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

// ---------------------------------------------------------------------------
// Slice B — metadata/state faults (band role swap, omit quality mask,
// wrong scale/offset, NoData-as-data) + observables + expectations
// ---------------------------------------------------------------------------

namespace
{

/// Fixture with a QA-masked pair: QA==0 marks cloud pixels; a subset of
/// masked pixels are additionally NaN, the rest are finite-but-masked — so
/// dropping the QA band measurably changes `valid_fraction`.
FaultGrid qualityMaskedGrid()
{
    FaultGrid grid;
    grid.width = 4;
    grid.height = 3;
    grid.crsId = "EPSG:4326";
    grid.hasNoData = true;
    grid.noDataValue = -9999.0;

    BandSpec red;
    red.role = "red";
    red.samples = { 0.10, 0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.32 };
    BandSpec nir;
    nir.role = "nir";
    nir.samples = { 0.40, 0.42, 0.44, 0.46, 0.48, 0.50, 0.52, 0.54, 0.56, 0.58, 0.60, 0.62 };
    BandSpec qa;
    qa.role = "qa";
    qa.samples = { 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1 };
    grid.bands = { red, nir, qa };

    // Two of the four QA-invalid pixels are NaN in the data bands; two stay
    // finite so that omitting the mask moves `valid_fraction`.
    for ( int band = 0; band < 2; ++band )
    {
        grid.bands[band].samples[5] = std::nan( "" );
        grid.bands[band].samples[6] = std::nan( "" );
    }
    return grid;
}

/// Base grid whose extras declare a role-resolved index pair (red/nir) so
/// `index_mean` is measurable. Constant bands keep the expected index mean
/// exactly computable: (0.1-0.9)/(0.1+0.9) = -0.8, and a role swap flips it.
FaultGrid indexPairGrid()
{
    FaultGrid grid = sampleGrid();
    for ( double &sample : grid.bands[0].samples )
    {
        sample = 0.1;
    }
    for ( double &sample : grid.bands[1].samples )
    {
        sample = 0.9;
    }
    Json::Value pair( Json::objectValue );
    pair["numerator"] = "red";
    pair["denominator"] = "nir";
    grid.extras["index_pair"] = pair;
    return grid;
}

FaultSpec swapSpec()
{
    FaultSpec spec;
    spec.familyId = "band_role_swap";
    spec.params["role_a"] = "red";
    spec.params["role_b"] = "nir";
    spec.seed = 42;
    return spec;
}

} // namespace

TEST_CASE( "fault lab: band role swap exchanges exactly the two roles", "[faultlab]" )
{
    FaultGrid grid = indexPairGrid();
    const auto before = measureObservables( grid );
    REQUIRE( before.count( "index_mean" ) == 1 );
    const double cleanMean = before.at( "index_mean" ).number;

    const auto outcome = applyFault( grid, swapSpec() );
    REQUIRE( outcome.ok );
    CHECK( outcome.mutations == 1 );
    CHECK( grid.bands[0].role == "nir" );
    CHECK( grid.bands[1].role == "red" );

    const auto after = measureObservables( grid );
    REQUIRE( after.count( "index_mean" ) == 1 );
    // Swapping numerator/denominator flips the ratio index sign.
    CHECK( after.at( "index_mean" ).number == Catch::Approx( -cleanMean ) );
    CHECK( before.at( "band_roles" ).text != after.at( "band_roles" ).text );
}

TEST_CASE( "fault lab: band role swap refuses unsafe targets and params", "[faultlab]" )
{
    SECTION( "role absent from the fixture" )
    {
        FaultGrid grid = sampleGrid();
        FaultSpec spec = swapSpec();
        spec.params["role_b"] = "swir2";
        const auto outcome = applyFault( grid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsafe_target" );
        // The grid is untouched by a refused transform.
        CHECK( grid.bands[0].role == "red" );
    }
    SECTION( "identical roles" )
    {
        FaultGrid grid = sampleGrid();
        FaultSpec spec = swapSpec();
        spec.params["role_b"] = "red";
        const auto outcome = applyFault( grid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
    SECTION( "unknown param name" )
    {
        FaultGrid grid = sampleGrid();
        FaultSpec spec = swapSpec();
        spec.params["channels"] = Json::arrayValue;
        const auto outcome = applyFault( grid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
}

TEST_CASE( "fault lab: omitting the quality mask changes masking observables", "[faultlab]" )
{
    FaultGrid grid = qualityMaskedGrid();
    const auto before = measureObservables( grid );
    const double cleanValid = before.at( "valid_fraction" ).number;
    CHECK( before.at( "band_count" ).number == 3 );

    FaultSpec spec;
    spec.familyId = "omit_quality_mask";
    spec.seed = 7;
    const auto outcome = applyFault( grid, spec );
    REQUIRE( outcome.ok );
    CHECK( outcome.mutations == 1 );
    CHECK( grid.bands.size() == 2 );
    CHECK( grid.bandIndexByRole( "qa" ) == -1 );

    const auto after = measureObservables( grid );
    CHECK( after.at( "band_count" ).number == 2 );
    CHECK( before.at( "band_roles" ).text != after.at( "band_roles" ).text );
    // Two finite-but-masked pixels per band are now counted as valid.
    CHECK( after.at( "valid_fraction" ).number > cleanValid );
}

TEST_CASE( "fault lab: wrong scale/offset moves band statistics, NaN-safe", "[faultlab]" )
{
    FaultGrid grid = qualityMaskedGrid();
    const auto before = measureObservables( grid );
    const double cleanMean = before.at( "band.mean.red" ).number;

    SECTION( "samples mutated (gain + offset)" )
    {
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        spec.params["offset"] = 1.0;
        spec.seed = 11;
        const auto outcome = applyFault( grid, spec );
        REQUIRE( outcome.ok );
        CHECK( outcome.mutations == 10 ); // 12 samples minus 2 no-data
        CHECK( grid.bands[0].samples[5] != grid.bands[0].samples[5] ); // still NaN
        const auto after = measureObservables( grid );
        CHECK( after.at( "band.mean.red" ).number ==
               Catch::Approx( cleanMean * 2.0 + 1.0 ) );
    }
    SECTION( "declared metadata mutated (data kept)" )
    {
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        spec.params["metadata"] = true;
        spec.seed = 11;
        const auto outcome = applyFault( grid, spec );
        REQUIRE( outcome.ok );
        CHECK( grid.bands[0].samples[0] == 0.10 ); // samples untouched
        CHECK( grid.bands[0].scale == 2.0 );       // declared scale wrong
        const auto after = measureObservables( grid );
        CHECK( after.at( "band.scale.red" ).number != before.at( "band.scale.red" ).number );
        CHECK( after.at( "band.mean.red" ).number == Catch::Approx( cleanMean ) );
    }
    SECTION( "zero gain refused" )
    {
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 0.0;
        const auto outcome = applyFault( grid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
}

TEST_CASE( "fault lab: NoData-as-data fills sentinel pixels and moves stats", "[faultlab]" )
{
    FaultGrid grid = qualityMaskedGrid();
    const auto before = measureObservables( grid );
    CHECK( before.at( "nodata_fraction" ).number > 0.0 );

    SECTION( "default fill = declared sentinel" )
    {
        FaultSpec spec;
        spec.familyId = "nodata_as_data";
        spec.seed = 5;
        const auto outcome = applyFault( grid, spec );
        REQUIRE( outcome.ok );
        CHECK( outcome.mutations == 4 ); // two NaN samples in two data bands
        CHECK( grid.bands[0].samples[5] == grid.noDataValue );
        const auto after = measureObservables( grid );
        CHECK( after.at( "nodata_fraction" ).number == 0.0 );
        CHECK( after.at( "finite_fraction" ).number == 1.0 );
        CHECK( after.at( "band.mean.red" ).number != before.at( "band.mean.red" ).number );
    }
    SECTION( "explicit fill value" )
    {
        FaultSpec spec;
        spec.familyId = "nodata_as_data";
        spec.params["fill_value"] = 0.0;
        const auto outcome = applyFault( grid, spec );
        REQUIRE( outcome.ok );
        CHECK( grid.bands[1].samples[6] == 0.0 );
    }
    SECTION( "non-finite fill refused" )
    {
        FaultGrid nanGrid = qualityMaskedGrid();
        nanGrid.noDataValue = std::nan( "" );
        FaultSpec spec;
        spec.familyId = "nodata_as_data";
        const auto outcome = applyFault( nanGrid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
}

TEST_CASE( "fault lab: expectation relations evaluate observables with evidence", "[faultlab]" )
{
    const FaultGrid clean = indexPairGrid();
    const ObservableSet cleanSet = measureObservables( clean );
    FaultGrid faulted = FaultSandbox::copyOf( clean );
    REQUIRE( applyFault( faulted, swapSpec() ).ok );
    const ObservableSet faultedSet = measureObservables( faulted );

    std::vector<ObservableExpectation> expectations;
    ObservableExpectation changed;
    changed.id = "band_roles";
    changed.relation = ExpectationRelation::Changed;
    expectations.push_back( changed );
    ObservableExpectation deltaGe;
    deltaGe.id = "index_mean";
    deltaGe.relation = ExpectationRelation::DeltaGe;
    deltaGe.value = 0.5;
    expectations.push_back( deltaGe );
    ObservableExpectation truthIs;
    truthIs.id = "provenance.generator_present";
    truthIs.relation = ExpectationRelation::TruthIs;
    truthIs.value = 0.0;
    expectations.push_back( truthIs );
    ObservableExpectation missing;
    missing.id = "channel_order";
    missing.relation = ExpectationRelation::Changed;
    expectations.push_back( missing );

    const auto results = checkExpectations( cleanSet, faultedSet, expectations );
    REQUIRE( results.size() == 4 );
    CHECK( results[0].passed );                                     // band_roles changed
    CHECK( results[1].passed );                                     // index delta >= 0.5
    CHECK( results[1].delta >= 0.5 );                               // evidence carries the delta
    CHECK_FALSE( results[2].passed );                               // truth observable never produced
    CHECK( results[2].note.find( "observable" ) != std::string::npos );
    CHECK_FALSE( results[3].passed );                               // channel_order never produced
    CHECK( results[3].note.find( "observable" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Slice C — geometry/temporal faults (grid shift, CRS mismatch, temporal
// shuffle, temporal gap)
// ---------------------------------------------------------------------------

namespace
{

/// Four dated epochs (32x32, one value per epoch) for temporal faults.
FaultGrid temporalGrid()
{
    FaultGrid grid;
    grid.width = 4;
    grid.height = 4;
    grid.crsId = "EPSG:4326";
    const char *dates[4] = { "2020-03-01", "2020-04-01", "2020-05-01", "2020-06-01" };
    const char *roles[4] = { "epoch1", "epoch2", "epoch3", "epoch4" };
    for ( int band = 0; band < 4; ++band )
    {
        BandSpec spec;
        spec.role = roles[band];
        spec.acquisitionDate = dates[band];
        spec.samples.assign( 16, 0.1 * ( band + 1 ) );
        grid.bands.push_back( spec );
    }
    return grid;
}

std::string datesOf( const FaultGrid &grid )
{
    std::string joined;
    for ( const auto &band : grid.bands )
    {
        if ( !joined.empty() )
        {
            joined += ",";
        }
        joined += band.acquisitionDate;
    }
    return joined;
}

} // namespace

TEST_CASE( "fault lab: grid shift moves only the grid origin", "[faultlab]" )
{
    FaultGrid grid = temporalGrid();
    const auto before = measureObservables( grid );

    FaultSpec spec;
    spec.familyId = "grid_shift";
    spec.params["dx"] = 0.5;
    spec.params["dy"] = -0.5;
    spec.seed = 3;
    const auto outcome = applyFault( grid, spec );
    REQUIRE( outcome.ok );
    CHECK( outcome.mutations == 1 );

    const auto after = measureObservables( grid );
    CHECK( after.at( "geo_transform.origin_x" ).number ==
           Catch::Approx( before.at( "geo_transform.origin_x" ).number + 0.5 ) );
    // dx/dy are image-space pixel offsets: a north-up grid has pixelY < 0,
    // so dy = -0.5 moves the origin NORTH by half a pixel.
    CHECK( after.at( "geo_transform.origin_y" ).number ==
           Catch::Approx( before.at( "geo_transform.origin_y" ).number + 0.5 ) );
    // Pixel size and everything else must stay untouched.
    CHECK( grid.geoTransform[1] == 1.0 );
    CHECK( grid.geoTransform[5] == -1.0 );
    CHECK( after.at( "band.mean.epoch1" ).number ==
           before.at( "band.mean.epoch1" ).number );
}

TEST_CASE( "fault lab: CRS mismatch rewrites the CRS only", "[faultlab]" )
{
    FaultGrid grid = temporalGrid();
    const auto before = measureObservables( grid );
    CHECK( before.at( "crs" ).text == "EPSG:4326" );

    FaultSpec spec;
    spec.familyId = "crs_mismatch";
    spec.params["crs"] = "EPSG:3857";
    spec.seed = 3;
    const auto outcome = applyFault( grid, spec );
    REQUIRE( outcome.ok );
    CHECK( grid.crsId == "EPSG:3857" );
    CHECK( measureObservables( grid ).at( "crs" ).text == "EPSG:3857" );

    SECTION( "unknown CRS refused" )
    {
        FaultSpec bad = spec;
        bad.params["crs"] = "EPSG:9999";
        FaultGrid local = temporalGrid();
        const auto refused = applyFault( local, bad );
        CHECK_FALSE( refused.ok );
        CHECK( refused.diagnostics.front().code == "faultlab.fault_unsupported_params" );
        CHECK( local.crsId == "EPSG:4326" );
    }
    SECTION( "declaring the fixture's own CRS is not a fault" )
    {
        FaultSpec same = spec;
        same.params["crs"] = "EPSG:4326";
        FaultGrid local = temporalGrid();
        const auto refused = applyFault( local, same );
        CHECK_FALSE( refused.ok );
        CHECK( refused.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
}

TEST_CASE( "fault lab: temporal shuffle permutes dated epochs deterministically", "[faultlab]" )
{
    const std::string original = "2020-03-01,2020-04-01,2020-05-01,2020-06-01";

    SECTION( "same seed replays the same permutation" )
    {
        FaultGrid a = temporalGrid();
        FaultGrid b = temporalGrid();
        FaultSpec spec;
        spec.familyId = "temporal_shuffle";
        spec.seed = 42;
        REQUIRE( applyFault( a, spec ).ok );
        REQUIRE( applyFault( b, spec ).ok );
        CHECK( datesOf( a ) == datesOf( b ) );
        CHECK( datesOf( a ) != original );
    }
    SECTION( "the permutation always moves the epoch order" )
    {
        for ( std::uint32_t seed = 0; seed < 16; ++seed )
        {
            FaultGrid grid = temporalGrid();
            FaultSpec spec;
            spec.familyId = "temporal_shuffle";
            spec.seed = seed;
            const auto outcome = applyFault( grid, spec );
            INFO( seed );
            REQUIRE( outcome.ok );
            CHECK( outcome.mutations == 1 );
            CHECK( datesOf( grid ) != original );
            // Same multiset of dates: a shuffle, not a rewrite.
            CHECK( datesOf( grid ).size() == original.size() );
        }
    }
    SECTION( "a grid without dated epochs refuses" )
    {
        FaultGrid grid = sampleGrid();
        FaultSpec spec;
        spec.familyId = "temporal_shuffle";
        spec.seed = 1;
        const auto outcome = applyFault( grid, spec );
        CHECK_FALSE( outcome.ok );
        CHECK( outcome.diagnostics.front().code == "faultlab.fault_unsafe_target" );
    }
}

TEST_CASE( "fault lab: temporal gap drops one dated epoch", "[faultlab]" )
{
    FaultGrid grid = temporalGrid();
    const auto before = measureObservables( grid );
    CHECK( before.at( "acquisition_dates" ).text ==
           "2020-03-01,2020-04-01,2020-05-01,2020-06-01" );

    FaultSpec spec;
    spec.familyId = "temporal_gap";
    spec.params["epoch_index"] = 1;
    spec.seed = 9;
    const auto outcome = applyFault( grid, spec );
    REQUIRE( outcome.ok );
    CHECK( outcome.mutations == 1 );
    CHECK( grid.bands.size() == 3 );
    CHECK( measureObservables( grid ).at( "acquisition_dates" ).text ==
           "2020-03-01,2020-05-01,2020-06-01" );

    SECTION( "out-of-range index refused" )
    {
        FaultGrid local = temporalGrid();
        FaultSpec bad;
        bad.familyId = "temporal_gap";
        bad.params["epoch_index"] = 7;
        const auto refused = applyFault( local, bad );
        CHECK_FALSE( refused.ok );
        CHECK( refused.diagnostics.front().code == "faultlab.fault_unsupported_params" );
        CHECK( local.bands.size() == 4 );
    }
    SECTION( "a non-integer index refused" )
    {
        FaultGrid local = temporalGrid();
        FaultSpec bad;
        bad.familyId = "temporal_gap";
        bad.params["epoch_index"] = 1.5;
        const auto refused = applyFault( local, bad );
        CHECK_FALSE( refused.ok );
        CHECK( refused.diagnostics.front().code == "faultlab.fault_unsupported_params" );
    }
}
