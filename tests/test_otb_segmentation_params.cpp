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

#include "processing/providers/otb_tools/algorithms/otb_extract_roi.h"
#include "processing/providers/otb_tools/algorithms/otb_concatenate_images.h"
#include "processing/providers/otb_tools/algorithms/otb_compute_images_statistics.h"
#include "processing/providers/otb_tools/algorithms/otb_haralick_texture.h"
#include "processing/providers/otb_tools/algorithms/otb_binary_morphological.h"
#include "processing/providers/otb_tools/algorithms/otb_gray_scale_morphological.h"
#include "processing/providers/otb_tools/algorithms/otb_multi_resolution_pyramid.h"
#include "processing/providers/otb_tools/algorithms/otb_bundle_to_perfect_sensor.h"
#include "processing/providers/otb_tools/algorithms/otb_radiometric_indices.h"
#include "processing/providers/otb_tools/algorithms/otb_lsms.h"
#include "processing/providers/otb_tools/algorithms/otb_pixel_info.h"
#include "processing/providers/otb_tools/algorithms/otb_feature_extraction.h"
#include "processing/providers/otb_tools/algorithms/otb_gray_level_cooccurrence_matrix.h"

namespace
{
template <typename T>
class TestableOtbWrapper : public T
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return this->buildArgs( parameters, context, nullptr );
    }
};

using TestableOtbSegmentation = TestableOtbWrapper<OtbSegmentationAlgorithm>;
using TestableOtbExtractRoi = TestableOtbWrapper<OtbExtractRoiAlgorithm>;
using TestableOtbConcatenateImages = TestableOtbWrapper<OtbConcatenateImagesAlgorithm>;
using TestableOtbComputeImagesStatistics = TestableOtbWrapper<OtbComputeImagesStatisticsAlgorithm>;
using TestableOtbHaralickTexture = TestableOtbWrapper<OtbHaralickTextureAlgorithm>;
using TestableOtbBinaryMorphological = TestableOtbWrapper<OtbBinaryMorphologicalAlgorithm>;
using TestableOtbGrayScaleMorphological = TestableOtbWrapper<OtbGrayScaleMorphologicalAlgorithm>;
using TestableOtbMultiResolutionPyramid = TestableOtbWrapper<OtbMultiResolutionPyramidAlgorithm>;
using TestableOtbBundleToPerfectSensor = TestableOtbWrapper<OtbBundleToPerfectSensorAlgorithm>;
using TestableOtbRadiometricIndices = TestableOtbWrapper<OtbRadiometricIndicesAlgorithm>;
using TestableOtbLsms = TestableOtbWrapper<OtbLsmsAlgorithm>;
using TestableOtbPixelInfo = TestableOtbWrapper<OtbPixelInfoAlgorithm>;
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
    REQUIRE( paramNames.contains("CC_EXPR") );
    REQUIRE( paramNames.contains("MPROFILES_SIZE") );
    REQUIRE( paramNames.contains("MPROFILES_START") );
    REQUIRE( paramNames.contains("MPROFILES_STEP") );
    REQUIRE( paramNames.contains("MPROFILES_SIGMA") );
    REQUIRE( paramNames.contains("OUTPUT_RASTER") );

    // 377 fix: INPUT + MODE + 5 meanshift/watershed + 1 cc + 4 mprofiles + 2 outputs = 14
    REQUIRE( params.size() == 14 );
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
    // Watershed threshold default aligned with the operator and OTB (#398 S2)
    REQUIRE( defaults["THRESHOLD"].toDouble() == Catch::Approx(0.01) );
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
    REQUIRE( args.indexOf( "-filter" ) >= 0 );
    REQUIRE( args[args.indexOf( "-filter" ) + 1] == "meanshift" );
    REQUIRE( args.indexOf( "-filter.meanshift.spatialr" ) >= 0 );
    REQUIRE( args[args.indexOf( "-filter.meanshift.spatialr" ) + 1] == "7" );
    REQUIRE( args.indexOf( "-filter.meanshift.ranger" ) >= 0 );
    REQUIRE( args[args.indexOf( "-filter.meanshift.ranger" ) + 1] == "20.00" );
    REQUIRE( args.indexOf( "-filter.meanshift.minsize" ) >= 0 );
    REQUIRE( args[args.indexOf( "-filter.meanshift.minsize" ) + 1] == "50" );
    REQUIRE( args.indexOf( "-filter.meanshift.maxiter" ) >= 0 );
    REQUIRE( args[args.indexOf( "-filter.meanshift.maxiter" ) + 1] == "200" );
    REQUIRE( args.indexOf( "-mode" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode" ) + 1] == "vector" );
    REQUIRE( args.indexOf( "-mode.vector.out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.vector.out" ) + 1] == "/tmp/segments.shp" );
    REQUIRE( args.indexOf( "-mode.raster.out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.raster.out" ) + 1] == "/tmp/labels.tif" );
    REQUIRE( !args.contains( "-filter.meanshift.threshold" ) );
}

TEST_CASE( "OTB ExtractROI: extent coordinate ordering and unit parameter (#296)", "[otb][extract_roi]" )
{
    TestableOtbExtractRoi algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    // QGIS extent string: xmin, xmax, ymin, ymax
    params["EXTENT"] = "100.0,200.0,300.0,400.0";
    params["OUTPUT"] = "/tmp/roi.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in" ) + 1] == "/data/input.tif" );
    REQUIRE( args.indexOf( "-mode" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode" ) + 1] == "extent" );
    REQUIRE( args.indexOf( "-mode.extent.unit" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.extent.unit" ) + 1] == "phy" );
    REQUIRE( args.indexOf( "-mode.extent.ulx" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.extent.ulx" ) + 1] == "100.000000" );
    REQUIRE( args.indexOf( "-mode.extent.uly" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.extent.uly" ) + 1] == "400.000000" );
    REQUIRE( args.indexOf( "-mode.extent.lrx" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.extent.lrx" ) + 1] == "200.000000" );
    REQUIRE( args.indexOf( "-mode.extent.lry" ) >= 0 );
    REQUIRE( args[args.indexOf( "-mode.extent.lry" ) + 1] == "300.000000" );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/roi.tif" );
}

TEST_CASE( "OTB ConcatenateImages: -il parameter key (#296)", "[otb][concatenate]" )
{
    TestableOtbConcatenateImages algo;

    QVariantMap params;
    params["INPUT"] = "/data/b1.tif;/data/b2.tif";
    params["OUTPUT"] = "/tmp/stack.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-il" ) >= 0 );
    REQUIRE( args.contains( "/data/b1.tif" ) );
    REQUIRE( args.contains( "/data/b2.tif" ) );
    REQUIRE( !args.contains( "-in" ) );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/stack.tif" );
}

TEST_CASE( "OTB ComputeImagesStatistics: -out.xml parameter key (#296)", "[otb][statistics]" )
{
    TestableOtbComputeImagesStatistics algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["STATS"] = "/tmp/stats.xml";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-il" ) >= 0 );
    REQUIRE( args.indexOf( "-out.xml" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out.xml" ) + 1] == "/tmp/stats.xml" );
    REQUIRE( !args.contains( "-out" ) );
}

TEST_CASE( "OTB HaralickTexture: -parameters.xrad/yrad and -channel (#296)", "[otb][haralick]" )
{
    TestableOtbHaralickTexture algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["RADIUS"] = 5;
    params["CHANNEL"] = 2;
    params["OUTPUT"] = "/tmp/texture.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-parameters.xrad" ) >= 0 );
    REQUIRE( args[args.indexOf( "-parameters.xrad" ) + 1] == "5" );
    REQUIRE( args.indexOf( "-parameters.yrad" ) >= 0 );
    REQUIRE( args[args.indexOf( "-parameters.yrad" ) + 1] == "5" );
    REQUIRE( args.indexOf( "-channel" ) >= 0 );
    REQUIRE( args[args.indexOf( "-channel" ) + 1] == "2" );
    REQUIRE( !args.contains( "-parameters.radius" ) );
}

TEST_CASE( "OTB MorphologicalOperations: -structype ball and -channel (#296)", "[otb][morphology]" )
{
    TestableOtbBinaryMorphological binAlgo;
    QVariantMap binParams;
    binParams["INPUT"] = "/data/bin.tif";
    binParams["RADIUS"] = 3;
    binParams["OPERATOR"] = 1; // erode
    binParams["OUTPUT"] = "/tmp/eroded.tif";

    const QStringList binArgs = binAlgo.testBuildArgs( binParams );
    REQUIRE( binArgs.indexOf( "-structype" ) >= 0 );
    REQUIRE( binArgs[binArgs.indexOf( "-structype" ) + 1] == "ball" );
    REQUIRE( binArgs.indexOf( "-structype.ball.xradius" ) >= 0 );
    REQUIRE( binArgs[binArgs.indexOf( "-structype.ball.xradius" ) + 1] == "3" );
    REQUIRE( binArgs.indexOf( "-channel" ) >= 0 );
    REQUIRE( binArgs[binArgs.indexOf( "-channel" ) + 1] == "1" );
    REQUIRE( binArgs.indexOf( "-filter" ) >= 0 );
    REQUIRE( binArgs[binArgs.indexOf( "-filter" ) + 1] == "erode" );

    TestableOtbGrayScaleMorphological grayAlgo;
    QVariantMap grayParams;
    grayParams["INPUT"] = "/data/gray.tif";
    grayParams["RADIUS"] = 4;
    grayParams["OPERATOR"] = 0; // dilate
    grayParams["OUTPUT"] = "/tmp/dilated.tif";

    const QStringList grayArgs = grayAlgo.testBuildArgs( grayParams );
    REQUIRE( grayArgs.indexOf( "-structype" ) >= 0 );
    REQUIRE( grayArgs[grayArgs.indexOf( "-structype" ) + 1] == "ball" );
    REQUIRE( grayArgs.indexOf( "-structype.ball.xradius" ) >= 0 );
    REQUIRE( grayArgs[grayArgs.indexOf( "-structype.ball.xradius" ) + 1] == "4" );
    REQUIRE( grayArgs.indexOf( "-channel" ) >= 0 );
    REQUIRE( grayArgs.indexOf( "-filter" ) >= 0 );
    REQUIRE( grayArgs[grayArgs.indexOf( "-filter" ) + 1] == "dilate" );
}

TEST_CASE( "OTB MultiResolutionPyramid: -level and -sfactor (#296)", "[otb][pyramid]" )
{
    TestableOtbMultiResolutionPyramid algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["LEVELS"] = 4;
    params["SFACTOR"] = 3;
    params["OUTPUT"] = "/tmp/pyr.tif";

    const QStringList args = algo.testBuildArgs( params );
    REQUIRE( args.indexOf( "-level" ) >= 0 );
    REQUIRE( args[args.indexOf( "-level" ) + 1] == "4" );
    REQUIRE( args.indexOf( "-sfactor" ) >= 0 );
    REQUIRE( args[args.indexOf( "-sfactor" ) + 1] == "3" );
    REQUIRE( !args.contains( "-levels" ) );
    REQUIRE( !args.contains( "-method" ) );
}

TEST_CASE( "OTB BundleToPerfectSensor: -inxs and -inp (#296)", "[otb][pansharpening]" )
{
    TestableOtbBundleToPerfectSensor algo;

    QVariantMap params;
    params["INPUT"] = "/data/ms.tif";
    params["PANCHROMATIC"] = "/data/pan.tif";
    params["OUTPUT"] = "/tmp/fused.tif";

    const QStringList args = algo.testBuildArgs( params );
    REQUIRE( args.indexOf( "-inxs" ) >= 0 );
    REQUIRE( args[args.indexOf( "-inxs" ) + 1] == "/data/ms.tif" );
    REQUIRE( args.indexOf( "-inp" ) >= 0 );
    REQUIRE( args[args.indexOf( "-inp" ) + 1] == "/data/pan.tif" );
    REQUIRE( !args.contains( "-in" ) );
    REQUIRE( !args.contains( "-pan" ) );
}

TEST_CASE( "OTB RadiometricIndices: prefix and channels (#296)", "[otb][indices]" )
{
    TestableOtbRadiometricIndices algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["LIST"] = "NDVI";
    params["RED_CHANNEL"] = 3;
    params["NIR_CHANNEL"] = 4;
    params["OUTPUT"] = "/tmp/ndvi.tif";

    const QStringList args = algo.testBuildArgs( params );
    REQUIRE( args.indexOf( "-list" ) >= 0 );
    REQUIRE( args[args.indexOf( "-list" ) + 1] == "Vegetation:NDVI" );
    REQUIRE( args.indexOf( "-channels.red" ) >= 0 );
    REQUIRE( args[args.indexOf( "-channels.red" ) + 1] == "3" );
    REQUIRE( args.indexOf( "-channels.nir" ) >= 0 );
    REQUIRE( args[args.indexOf( "-channels.nir" ) + 1] == "4" );
}

TEST_CASE( "OTB Application Names validity (#296)", "[otb][apps]" )
{
    OtbLsmsAlgorithm lsms;
    REQUIRE( lsms.applicationName() == "LSMSSegmentation" );

    OtbFeatureExtractionAlgorithm feat;
    REQUIRE( feat.applicationName() == "HaralickTextureExtraction" );

    OtbGrayLevelCooccurrenceMatrixAlgorithm glcm;
    REQUIRE( glcm.applicationName() == "HaralickTextureExtraction" );

    OtbPixelInfoAlgorithm pinfo;
    REQUIRE( pinfo.applicationName() == "PixelValue" );
}
