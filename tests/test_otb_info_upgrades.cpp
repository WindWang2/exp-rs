// test_otb_info_upgrades.cpp — Processing Toolbox Phase 1 Task 5
//
// Metadata and buildArgs tests for upgraded OTB info wrappers and stereo rectification.
#include <catch2/catch_test_macros.hpp>

#include "processing/providers/otb_tools/algorithms/otb_compute_images_statistics.h"
#include "processing/providers/otb_tools/algorithms/otb_read_image_info.h"
#include "processing/providers/otb_tools/algorithms/otb_pixel_info.h"
#include "processing/providers/otb_tools/algorithms/otb_stereo_rectification.h"

#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

namespace
{
class TestableOtbComputeImagesStatistics : public OtbComputeImagesStatisticsAlgorithm
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return buildArgs( parameters, context, nullptr );
    }
};

class TestableOtbReadImageInfo : public OtbReadImageInfoAlgorithm
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return buildArgs( parameters, context, nullptr );
    }
};

class TestableOtbPixelInfo : public OtbPixelInfoAlgorithm
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return buildArgs( parameters, context, nullptr );
    }
};

class TestableOtbStereoRectification : public OtbStereoRectificationAlgorithm
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return buildArgs( parameters, context, nullptr );
    }
};
} // namespace

TEST_CASE( "OTB ComputeImagesStatistics: metadata and schema", "[otb][processing]" )
{
    OtbComputeImagesStatisticsAlgorithm algo;
    algo.initAlgorithm();

    REQUIRE( !algo.shortHelpString().isEmpty() );

    const QVariantMap schema = algo.toJsonSchema();
    REQUIRE( schema.contains( "properties" ) );
    REQUIRE( schema.value( "properties" ).toMap().contains( "INPUT" ) );
}

TEST_CASE( "OTB ReadImageInfo: metadata and schema", "[otb][processing]" )
{
    OtbReadImageInfoAlgorithm algo;
    algo.initAlgorithm();

    REQUIRE( !algo.shortHelpString().isEmpty() );

    const QVariantMap schema = algo.toJsonSchema();
    REQUIRE( schema.value( "properties" ).toMap().contains( "INPUT" ) );
}

TEST_CASE( "OTB PixelInfo: metadata and schema", "[otb][processing]" )
{
    OtbPixelInfoAlgorithm algo;
    algo.initAlgorithm();

    REQUIRE( !algo.shortHelpString().isEmpty() );

    const QVariantMap schema = algo.toJsonSchema();
    const QVariantMap properties = schema.value( "properties" ).toMap();
    REQUIRE( properties.contains( "INPUT" ) );
    REQUIRE( properties.contains( "X" ) );
    REQUIRE( properties.contains( "Y" ) );
}

TEST_CASE( "OTB PixelInfo: buildArgs", "[otb][processing]" )
{
    TestableOtbPixelInfo algo;

    QVariantMap params;
    params["INPUT"] = "/data/image.tif";
    params["X"] = 42;
    params["Y"] = 17;

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in" ) + 1] == "/data/image.tif" );
    REQUIRE( args.indexOf( "-coord" ) >= 0 );
    REQUIRE( args[args.indexOf( "-coord" ) + 1] == "42" );
    REQUIRE( args[args.indexOf( "-coord" ) + 2] == "17" );
}

TEST_CASE( "OTB StereoRectification: metadata", "[otb][processing]" )
{
    OtbStereoRectificationAlgorithm algo;

    REQUIRE( algo.name() == "otb_stereo_rectification" );
    REQUIRE( algo.displayName() == "Stereo Rectification" );
    REQUIRE( algo.applicationName() == "StereoRectificationGridGenerator" );
    REQUIRE( !algo.shortHelpString().isEmpty() );
}

TEST_CASE( "OTB StereoRectification: parameter definitions", "[otb][processing]" )
{
    OtbStereoRectificationAlgorithm algo;
    algo.initAlgorithm();

    QStringList names;
    for ( auto *p : algo.parameterDefinitions() )
        names << p->name();

    REQUIRE( names.contains( "LEFT" ) );
    REQUIRE( names.contains( "RIGHT" ) );
    REQUIRE( names.contains( "OUTPUT_LEFT" ) );
    REQUIRE( names.contains( "OUTPUT_RIGHT" ) );
    REQUIRE( names.contains( "ELEVATION" ) );
    REQUIRE( names.contains( "GRID_STEP" ) );
}

TEST_CASE( "OTB StereoRectification: buildArgs", "[otb][processing]" )
{
    TestableOtbStereoRectification algo;

    QVariantMap params;
    params["LEFT"] = "/data/left.tif";
    params["RIGHT"] = "/data/right.tif";
    params["OUTPUT_LEFT"] = "/tmp/left_grid.tif";
    params["OUTPUT_RIGHT"] = "/tmp/right_grid.tif";
    params["ELEVATION"] = 250.0;
    params["GRID_STEP"] = 4;

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-io.inleft" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.inleft" ) + 1] == "/data/left.tif" );
    REQUIRE( args.indexOf( "-io.inright" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.inright" ) + 1] == "/data/right.tif" );
    REQUIRE( args.indexOf( "-io.outleft" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.outleft" ) + 1] == "/tmp/left_grid.tif" );
    REQUIRE( args.indexOf( "-io.outright" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.outright" ) + 1] == "/tmp/right_grid.tif" );
    REQUIRE( args.indexOf( "-epi.elevation.default" ) >= 0 );
    REQUIRE( args[args.indexOf( "-epi.elevation.default" ) + 1] == "250" );
    REQUIRE( args.indexOf( "-epi.step" ) >= 0 );
    REQUIRE( args[args.indexOf( "-epi.step" ) + 1] == "4" );
}