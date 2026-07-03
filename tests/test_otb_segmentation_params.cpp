// test_otb_segmentation_params.cpp — Phase 10B Task 10B.2
//
// Tests for the enhanced OTB Segmentation wrapper parameter setup.
// Since OTB CLI binaries may not be available, we test parameter
// definitions and buildArgs logic only.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include "processing/providers/otb_tools/algorithms/otb_segmentation.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

namespace
{
class TestableOtbSegmentation : public OtbSegmentationAlgorithm
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return buildArgs( parameters, context, nullptr );
    }
};
} // namespace

TEST_CASE( "OTB Segmentation: algorithm metadata", "[otb][segmentation]" )
{
    OtbSegmentationAlgorithm algo;

    REQUIRE( algo.name() == "otb_segmentation" );
    REQUIRE( algo.displayName() == "Image Segmentation" );
    REQUIRE( algo.group() == "Segmentation" );
    REQUIRE( algo.groupId() == "segmentation" );
    REQUIRE( algo.applicationName() == "Segmentation" );
    REQUIRE( !algo.tags().isEmpty() );
}

TEST_CASE( "OTB Segmentation: parameter definitions", "[otb][segmentation]" )
{
    OtbSegmentationAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QStringList paramNames;
    for (auto *p : params)
        paramNames << p->name();

    // Core parameters
    REQUIRE( paramNames.contains("INPUT") );
    REQUIRE( paramNames.contains("MODE") );
    REQUIRE( paramNames.contains("OUTPUT") );

    // Phase 10B.2 — enhanced MeanShift parameters
    REQUIRE( paramNames.contains("SPATIAL_RADIUS") );
    REQUIRE( paramNames.contains("RANGE_RADIUS") );
    REQUIRE( paramNames.contains("MIN_REGION_SIZE") );
    REQUIRE( paramNames.contains("MAX_ITERATION") );
    REQUIRE( paramNames.contains("THRESHOLD") );
    REQUIRE( paramNames.contains("OUTPUT_RASTER") );

    // Verify total parameter count (INPUT + MODE + 5 params + OUTPUT + OUTPUT_RASTER = 9)
    REQUIRE( params.size() == 9 );
}

TEST_CASE( "OTB Segmentation: default values", "[otb][segmentation]" )
{
    OtbSegmentationAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QMap<QString, QVariant> defaults;
    for (auto *p : params)
        defaults[p->name()] = p->defaultValue();

    // MeanShift defaults
    REQUIRE( defaults["SPATIAL_RADIUS"].toInt() == 5 );
    REQUIRE( defaults["RANGE_RADIUS"].toDouble() == Catch::Approx(15.0) );
    REQUIRE( defaults["MIN_REGION_SIZE"].toInt() == 100 );
    REQUIRE( defaults["MAX_ITERATION"].toInt() == 100 );
    REQUIRE( defaults["THRESHOLD"].toDouble() == Catch::Approx(0.1) );
}

TEST_CASE( "OTB Segmentation: buildArgs MeanShift with label raster", "[otb][segmentation]" )
{
    TestableOtbSegmentation algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["MODE"] = 0;
    params["SPATIAL_RADIUS"] = 7;
    params["RANGE_RADIUS"] = 20.0;
    params["MIN_REGION_SIZE"] = 50;
    params["MAX_ITERATION"] = 200;
    params["OUTPUT"] = "/tmp/segments.shp";
    params["OUTPUT_RASTER"] = "/tmp/labels.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in" ) + 1] == "/data/input.tif" );
    REQUIRE( args.indexOf( "-mode" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode" ) + 1] == "meanshift" );
    REQUIRE( args.indexOf( "-mode.meanshift.spatialr" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.meanshift.spatialr" ) + 1] == "7" );
    REQUIRE( args.indexOf( "-mode.meanshift.ranger" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.meanshift.ranger" ) + 1] == "20.00" );
    REQUIRE( args.indexOf( "-mode.meanshift.minsize" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.meanshift.minsize" ) + 1] == "50" );
    REQUIRE( args.indexOf( "-mode.meanshift.maxiter" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.meanshift.maxiter" ) + 1] == "200" );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/segments.shp" );
    REQUIRE( args[args.indexOf( "-out" ) + 2] == "/tmp/labels.tif" );
    REQUIRE( args[args.indexOf( "-out" ) + 3] == "uint32" );
    REQUIRE( !args.contains( "-mode.meanshift.threshold" ) );
}
