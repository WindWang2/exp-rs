// test_r25_a_qa_confidence.cpp — R25-A FEAT-2.
//
// rs:qa_mask cloud_confidence: "standard" (default, bit-identical to the
// historical behaviour) masks the whole cloud family; "relaxed" drops the
// AMBIGUOUS members — Sentinel-2 SCL class 8 (medium-probability cloud)
// and the Landsat dilated-cloud bit — for studies that prefer coverage.
// The result also carries an SCL class histogram as a QA quality summary.
//
// Oracle: a 1×4 SCL strip with classes [8, 9, 4, 10] —
//   standard cloud mask → 3 masked (8, 9, 10)
//   relaxed  cloud mask → 2 masked (9, 10)
//   histogram → {4:1, 8:1, 9:1, 10:1}

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal.h>
#include <json/json.h>

#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

struct RunResult
{
    Json::Value json;
    bool ok = false;
    RSOperatorError error = RSOperatorError( ErrorCode::Unknown, "" );
};

RunResult runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    RunResult run;
    if ( op == nullptr )
    {
        run.error = RSOperatorError( ErrorCode::InvalidParameter, "operator not registered" );
        return run;
    }
    RSOperatorContext context;
    try
    {
        run.json = op->run( params, context );
        run.ok = true;
    }
    catch ( const RSOperatorError &e )
    {
        run.error = e;
    }
    return run;
}

/// 1×4 single-band SCL strip with the given classes, role-tagged so source
/// auto-resolution picks sentinel2_scl.
QString writeSclStrip( const QTemporaryDir &dir, const std::vector<float> &classes )
{
    RsSyntheticRasterBuilder b( static_cast<int>( classes.size() ), 1, 1, GDT_Byte );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 10.0, 10.0, -10.0 );
    for ( size_t x = 0; x < classes.size(); ++x )
        b.withPixel( 1, static_cast<int>( x ), 0, classes[x] );
    return b.writeToDisk( dir.filePath( "scl.tif" ) );
}

/// Stamps SICNU_BAND_ROLE=scene_classification on band 1 (in-place).
void stampRole( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( band != nullptr );
    GDALSetMetadataItem( band, "SICNU_BAND_ROLE", "scene_classification", nullptr );
    GDALSetMetadataItem( band, "DESCRIPTION", "SCL", nullptr );
    GDALClose( ds );
}

std::vector<uint8_t> readMask( const QString &path, int width )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<uint8_t> px( static_cast<size_t>( width ) );
    REQUIRE( ds.readBandWindowNative( 1, 0, 0, width, 1, px.data() ) );
    return px;
}

} // namespace

TEST_CASE( "rs:qa_mask cloud_confidence standard vs relaxed (SCL)",
           "[operators][qa][r25a][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString scl = writeSclStrip( dir, { 8.0f, 9.0f, 4.0f, 10.0f } );
    REQUIRE( !scl.isEmpty() );
    stampRole( scl );

    auto runWith = [&]( const char *confidence, const char *mask ) {
        Json::Value params( Json::objectValue );
        params["input"] = scl.toStdString();
        params["output"] = dir.filePath( "mask.tif" ).toStdString();
        params["mask"] = mask;
        if ( confidence )
            params["cloud_confidence"] = confidence;
        return runOperator( "rs:qa_mask", params );
    };

    SECTION( "standard (default) masks the whole cloud family" )
    {
        const RunResult run = runWith( nullptr, "cloud" ); // no param: default
        REQUIRE( run.ok );
        CHECK( run.json["cloudConfidence"].asString() == "standard" );
        CHECK( run.json["maskedPixels"].asUInt64() == 3 ); // 8, 9, 10
        const auto px = readMask( dir.filePath( "mask.tif" ), 4 );
        CHECK( px == std::vector<uint8_t>{ 1, 1, 0, 1 } );
        // Histogram: one readable sample per class present.
        CHECK( run.json["sclClassHistogram"]["4"].asUInt64() == 1 );
        CHECK( run.json["sclClassHistogram"]["8"].asUInt64() == 1 );
        CHECK( run.json["sclClassHistogram"]["9"].asUInt64() == 1 );
        CHECK( run.json["sclClassHistogram"]["10"].asUInt64() == 1 );
    }

    SECTION( "relaxed drops the medium-probability class" )
    {
        const RunResult run = runWith( "relaxed", "cloud" );
        REQUIRE( run.ok );
        CHECK( run.json["cloudConfidence"].asString() == "relaxed" );
        CHECK( run.json["maskedPixels"].asUInt64() == 2 ); // 9, 10 only
        const auto px = readMask( dir.filePath( "mask.tif" ), 4 );
        CHECK( px == std::vector<uint8_t>{ 0, 1, 0, 1 } );
    }

    SECTION( "relaxed keeps non-cloud selections unchanged" )
    {
        const RunResult run = runWith( "relaxed", "water" );
        REQUIRE( run.ok );
        CHECK( run.json["maskedPixels"].asUInt64() == 0 ); // no water classes
    }
}

TEST_CASE( "rs:qa_mask relaxed drops the Landsat dilated-cloud bit",
           "[operators][qa][r25a][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Landsat QA_PIXEL bits: cloud = 1<<3, dilated cloud = 1<<1.
    // Pixels: dilated-only (2), cloud (8), clear (0), dilated+cloud (10).
    RsSyntheticRasterBuilder b( 4, 1, 1, GDT_UInt16 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 10.0, 10.0, -10.0 );
    b.withPixel( 1, 0, 0, 2.0f );
    b.withPixel( 1, 1, 0, 8.0f );
    b.withPixel( 1, 2, 0, 0.0f );
    b.withPixel( 1, 3, 0, 10.0f );
    const QString qa = b.writeToDisk( dir.filePath( "qa_pixel.tif" ) );
    REQUIRE( !qa.isEmpty() );
    // Role-tag as qa so auto-resolution picks landsat_qa_pixel.
    GDALDatasetH ds = GDALOpen( qa.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetMetadataItem( band, "SICNU_BAND_ROLE", "qa", nullptr );
    GDALClose( ds );

    auto runWith = [&]( const char *confidence ) {
        Json::Value params( Json::objectValue );
        params["input"] = qa.toStdString();
        params["output"] = dir.filePath( "mask.tif" ).toStdString();
        params["mask"] = "cloud";
        if ( confidence )
            params["cloud_confidence"] = confidence;
        return runOperator( "rs:qa_mask", params );
    };

    const RunResult standard = runWith( nullptr );
    REQUIRE( standard.ok );
    CHECK( standard.json["source"].asString() == "landsat_qa_pixel" );
    CHECK( standard.json["maskedPixels"].asUInt64() == 3 ); // 2, 8, 10
    CHECK( readMask( dir.filePath( "mask.tif" ), 4 )
           == std::vector<uint8_t>{ 1, 1, 0, 1 } );

    const RunResult relaxed = runWith( "relaxed" );
    REQUIRE( relaxed.ok );
    CHECK( relaxed.json["maskedPixels"].asUInt64() == 2 ); // 8, 10 (dilated dropped)
    CHECK( readMask( dir.filePath( "mask.tif" ), 4 )
           == std::vector<uint8_t>{ 0, 1, 0, 1 } );
}
