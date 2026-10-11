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
