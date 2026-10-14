/***************************************************************************
  tests/test_faultlab_params_r8.cpp

  R8 (issue #1464) regressions for the faultlab closed param vocabulary:
   * a declared optional parameter must be CONSUMED by its transform —
     accepted-but-inert parameters mislead scenario authors and violate the
     domain's no-silent-fallback doctrine (cell_size on
     train_test_spatial_leakage was accepted and never read);
   * the cross-check: every param declared in the registry catalog must be
     literally referenced by the transform implementation.
 ***************************************************************************/

#include "faultlab/fault_registry.h"
#include "faultlab/fault_transforms.h"
#include "faultlab/fault_types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace sicnu::faultlab;

namespace
{

/// 16x16 grid with train points clustered in two cells and test points in
/// disjoint cells (default cell size 4) — same shape as the leakage fixture
/// in test_faultlab.cpp.
FaultGrid leakageGrid()
{
    FaultGrid grid;
    grid.width = 16;
    grid.height = 16;
    grid.extras["leakage_cell_size"] = 4;

    Json::Value samples( Json::arrayValue );
    auto addSample = [&samples]( const char *role, double x, double y ) {
        Json::Value sample( Json::objectValue );
        sample["role"] = role;
        sample["x"] = x;
        sample["y"] = y;
        samples.append( sample );
    };
    addSample( "train", 1.0, 1.0 );
    addSample( "train", 2.0, 2.5 );
    addSample( "test", 9.0, 1.0 );
    addSample( "test", 10.0, 2.0 );
    grid.extras["samples"] = samples;
    return grid;
}

std::string readFileOrEmpty( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// leakageGrid minus every sample in @p droppedRole — the fixture shape that
/// makes each mode's target check refuse.
FaultGrid leakageGridWithout( const char *droppedRole )
{
    FaultGrid grid = leakageGrid();
    Json::Value filtered( Json::arrayValue );
    for ( const auto &sample : grid.extras["samples"] )
    {
        if ( sample.isObject() && sample.isMember( "role" )
             && sample["role"].asString() == droppedRole )
            continue;
        filtered.append( sample );
    }
    grid.extras["samples"] = filtered;
    return grid;
}

} // namespace

TEST_CASE( "train_test_spatial_leakage consumes cell_size, refusing invalid values",
           "[faultlab][params][r8]" )
{
    // The declared optional cell_size feeds the leakage observable's binning
    // (fault_observables.cpp reads extras["leakage_cell_size"]): passing it
    // must change that bin size on the faulted grid.
    FaultGrid grid = leakageGrid();
    FaultSpec spec;
    spec.familyId = "train_test_spatial_leakage";
    spec.params["mode"] = "duplicate";
    spec.params["cell_size"] = 8.0;
    const FaultOutcome applied = applyFault( grid, spec );
    REQUIRE( applied.ok );
    REQUIRE( grid.extras.isMember( "leakage_cell_size" ) );
    CHECK( grid.extras["leakage_cell_size"].asDouble() == 8.0 );

    // Same shape as applyNoDataAsData's fill_value gate: a declared param
    // with an unusable value is a typed refusal, never a silent default.
    const Json::Value badValues = [] {
        Json::Value values( Json::arrayValue );
        values.append( 0.0 );
        values.append( -4.0 );
        values.append( "big" );
        values.append( true );
        return values;
    }();
    for ( const Json::Value &bad : badValues )
    {
        FaultGrid victim = leakageGrid();
        FaultSpec refusing;
        refusing.familyId = "train_test_spatial_leakage";
        refusing.params["mode"] = "duplicate";
        refusing.params["cell_size"] = bad;
        const FaultOutcome refused = applyFault( victim, refusing );
        INFO( "cell_size = " << bad.toStyledString() );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
    }

    // Absent cell_size keeps the fixture's own bin size (no behavior change
    // for scenarios that never declared it).
    FaultGrid untouched = leakageGrid();
    FaultSpec withoutCell;
    withoutCell.familyId = "train_test_spatial_leakage";
    withoutCell.params["mode"] = "duplicate";
    const FaultOutcome plain = applyFault( untouched, withoutCell );
    REQUIRE( plain.ok );
    CHECK( untouched.extras["leakage_cell_size"].asDouble() == 4.0 );
}

TEST_CASE( "train_test_spatial_leakage refusals leave the grid unmutated",
           "[faultlab][params][r9]" )
{
    // Fail-before-mutate (#1474 review): a transform rejected by a
    // mode-specific target check must leave the fixture untouched. The
    // declared cell_size used to be published before those checks ran, so a
    // refused scenario still rewrote the observable's binning anchor.
    SECTION( "duplicate: no train samples to clone" )
    {
        FaultGrid grid = leakageGridWithout( "train" );
        const Json::Value pristineSamples = grid.extras["samples"];
        FaultSpec spec;
        spec.familyId = "train_test_spatial_leakage";
        spec.params["mode"] = "duplicate";
        spec.params["cell_size"] = 8.0;
        const FaultOutcome refused = applyFault( grid, spec );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsafe_target" );
        CHECK( grid.extras["leakage_cell_size"].asDouble() == 4.0 );
        CHECK( grid.extras["samples"] == pristineSamples );
    }

    SECTION( "relocate: no test sample to move" )
    {
        FaultGrid grid = leakageGridWithout( "test" );
        const Json::Value pristineSamples = grid.extras["samples"];
        FaultSpec spec;
        spec.familyId = "train_test_spatial_leakage";
        spec.params["mode"] = "relocate";
        spec.params["cell_size"] = 8.0;
        const FaultOutcome refused = applyFault( grid, spec );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsafe_target" );
        CHECK( grid.extras["leakage_cell_size"].asDouble() == 4.0 );
        CHECK( grid.extras["samples"] == pristineSamples );
    }
}

TEST_CASE( "wrong_scale_offset refuses mistyped optional params instead of coercing",
           "[faultlab][params][r12]" )
{
    // Present-but-unusable optional params used to be silently coerced:
    // offset "abc" became 0.0, metadata 1 became false — a refused-looking
    // scenario silently applied the identity offset. Same gate shape as
    // cell_size (#1464): a declared param with an unusable value is a typed
    // refusal, never a silent default.
    SECTION( "string offset refused" )
    {
        FaultGrid grid = leakageGrid();
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        spec.params["offset"] = "abc";
        const FaultOutcome refused = applyFault( grid, spec );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
    }
    SECTION( "array offset refused" )
    {
        FaultGrid grid = leakageGrid();
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        Json::Value offset( Json::arrayValue );
        offset.append( 1 );
        spec.params["offset"] = offset;
        const FaultOutcome refused = applyFault( grid, spec );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
    }
    SECTION( "numeric metadata refused" )
    {
        FaultGrid grid = leakageGrid();
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        spec.params["metadata"] = 1;
        const FaultOutcome refused = applyFault( grid, spec );
        CHECK_FALSE( refused.ok );
        const FaultDiagnostic *error = refused.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
    }
    SECTION( "valid offset and metadata still apply" )
    {
        FaultGrid grid = leakageGrid();
        BandSpec red;
        red.role = "red";
        red.samples.assign( 4, 1.0 );
        grid.bands.push_back( red );
        FaultSpec spec;
        spec.familyId = "wrong_scale_offset";
        spec.params["role"] = "red";
        spec.params["gain"] = 2.0;
        spec.params["offset"] = 0.5;
        spec.params["metadata"] = true;
        const FaultOutcome applied = applyFault( grid, spec );
        REQUIRE( applied.ok );
        CHECK( grid.bands.back().scale == Catch::Approx( 2.0 ) );
        CHECK( grid.bands.back().offset == Catch::Approx( 0.5 ) );
        CHECK( grid.bands.back().samples[0] == Catch::Approx( 1.0 ) ); // data kept
    }
}

TEST_CASE( "integral params beyond int64 refuse typed instead of throwing",
           "[faultlab][params][r12]" )
{
    // Round-2 review (P1): isIntegral() is true for a UInt64 above INT64_MAX,
    // but asInt64() THROWS Json::LogicError on one — escaping the
    // never-throws contract (fault_types.h) through applyFault /
    // runFaultScenario, which have no try/catch. Same defect class as the
    // R1 P1 closed in scientific_state: gate with isInt64() and refuse the
    // out-of-range value with the existing typed vocabulary.
    const Json::Value beyondInt64( Json::UInt64( 18446744073709551615ull ) );

    SECTION( "temporal_gap epoch_index" )
    {
        auto datedGrid = [] {
            FaultGrid grid;
            for ( int i = 0; i < 2; ++i )
            {
                BandSpec band;
                band.role = "epoch" + std::to_string( i + 1 );
                band.acquisitionDate = "2020-0" + std::to_string( i + 1 ) + "-01";
                band.samples.assign( 4, 0.1 );
                grid.bands.push_back( band );
            }
            return grid;
        };

        FaultGrid grid = datedGrid();
        FaultSpec refused;
        refused.familyId = "temporal_gap";
        refused.params["epoch_index"] = beyondInt64;
        FaultOutcome outcome;
        REQUIRE_NOTHROW( outcome = applyFault( grid, refused ) );
        CHECK_FALSE( outcome.ok );
        const FaultDiagnostic *error = outcome.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
        CHECK( grid.bands.size() == 2 );

        // In-range guard: an ordinary index still applies the gap.
        FaultGrid victim = datedGrid();
        FaultSpec valid;
        valid.familyId = "temporal_gap";
        valid.params["epoch_index"] = 1;
        const FaultOutcome applied = applyFault( victim, valid );
        REQUIRE( applied.ok );
        CHECK( victim.bands.size() == 1 );
    }

    SECTION( "model_channel_mismatch permutation entry" )
    {
        auto modelGrid = [] {
            FaultGrid grid;
            Json::Value order( Json::arrayValue );
            order.append( "red" );
            order.append( "nir" );
            Json::Value model( Json::objectValue );
            model["channel_order"] = order;
            grid.extras["model"] = model;
            return grid;
        };

        FaultGrid grid = modelGrid();
        FaultSpec refused;
        refused.familyId = "model_channel_mismatch";
        Json::Value permutation( Json::arrayValue );
        permutation.append( 1 );
        permutation.append( beyondInt64 );
        refused.params["permutation"] = permutation;
        FaultOutcome outcome;
        REQUIRE_NOTHROW( outcome = applyFault( grid, refused ) );
        CHECK_FALSE( outcome.ok );
        const FaultDiagnostic *error = outcome.firstError();
        REQUIRE( error != nullptr );
        CHECK( error->code == "faultlab.fault_unsupported_params" );
        // Refusal leaves the declared channel order unmutated.
        REQUIRE( grid.extras["model"].isMember( "channel_order" ) );
        CHECK( grid.extras["model"]["channel_order"][0].asString() == "red" );

        // In-range guard: a real permutation still applies.
        FaultGrid victim = modelGrid();
        FaultSpec valid;
        valid.familyId = "model_channel_mismatch";
        Json::Value swap( Json::arrayValue );
        swap.append( 1 );
        swap.append( 0 );
        valid.params["permutation"] = swap;
        const FaultOutcome applied = applyFault( victim, valid );
        REQUIRE( applied.ok );
        CHECK( victim.extras["model"]["channel_order"][0].asString() == "nir" );
    }
}

TEST_CASE( "every param declared by the fault registry is read by its transform",
           "[faultlab][params][r8]" )
{
    // The no-silent-fallback doctrine: a declared param the transform never
    // reads is accepted-and-inert (the #1464 cell_size shape). The catalog is
    // data; the transforms are code — this source scan pins that every
    // declared name is literally dereferenced somewhere in the implementation
    // file, so a future catalog row cannot silently regress.
    const std::string transformsSource =
        readFileOrEmpty( std::string( CMAKE_SOURCE_DIR ) + "/src/faultlab/fault_transforms.cpp" );
    REQUIRE_FALSE( transformsSource.empty() );
    for ( const FaultFamilyInfo &family : faultFamilyCatalog() )
    {
        for ( const std::string &param : family.requiredParams )
        {
            INFO( family.id << " required param '" << param << "'" );
            CHECK( transformsSource.find( "\"" + param + "\"" ) != std::string::npos );
        }
        for ( const std::string &param : family.optionalParams )
        {
            INFO( family.id << " optional param '" << param << "'" );
            CHECK( transformsSource.find( "\"" + param + "\"" ) != std::string::npos );
        }
    }
}
