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
