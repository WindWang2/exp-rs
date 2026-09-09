// test_grid_operators.cpp — Foundation 6.0, Milestone D: grid & resampling
// foundation (rs:resample / rs:align) known-answer tests.
//
// The resampling math itself is GDAL's (authoritative engine); these tests
// pin the operator POLICY: exact nearest-pick known answers, the categorical
// safety pin, exact reference-grid reproduction by rs:align, and the
// lossless same-grid copy.
#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

std::vector<float> readBand( const QString &path, int band, int width, int height )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<float> values( static_cast<size_t>( width ) * height );
    REQUIRE( ds.readBandWindow( band, 0, 0, width, height, values.data() ) );
    return values;
}

} // namespace

TEST_CASE( "rs:resample nearest known answer on a 2x decimation",
           "[grid][resample][ka]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString inputPath = dir.filePath( "in.tif" );
    const QString outputPath = dir.filePath( "out.tif" );

    // 4x4 unique values, 10 m cells at a projected origin.
    constexpr int kW = 4;
    constexpr int kH = 4;
    RsSyntheticRasterBuilder builder( kW, kH, 1 );
    builder.withCrs( "EPSG:32650" );
    builder.withGeoTransform( 500000.0, 10.0, 4000000.0, -10.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            builder.withPixel( 1, x, y, static_cast<float>( 100 * y + x ) );
    REQUIRE( !builder.writeToDisk( inputPath ).isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:resample" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["resolution"] = 20.0;
    params["resampling"] = "near";
    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    CHECK( result["width"].asInt() == 2 );
    CHECK( result["height"].asInt() == 2 );

    // Nearest picks the source pixel containing each 20 m output center:
    // source column 2i+1 / row 2j+1 for this even origin.
    const std::vector<float> out = readBand( outputPath, 1, 2, 2 );
    const float expected[4] = {
        static_cast<float>( 100 * 1 + 1 ), static_cast<float>( 100 * 1 + 3 ),
        static_cast<float>( 100 * 3 + 1 ), static_cast<float>( 100 * 3 + 3 ),
    };
    for ( int i = 0; i < 4; ++i )
        CHECK( out[static_cast<size_t>( i )] == Catch::Approx( expected[i] ) );

    // The output grid is 2x coarser on the same CRS.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outputPath ) );
    const auto gt = outDs.geoTransform();
    CHECK( gt[1] == Catch::Approx( 20.0 ).margin( 1e-9 ) );
    CHECK( gt[5] == Catch::Approx( -20.0 ).margin( 1e-9 ) );
    CHECK( gt[0] == Catch::Approx( 500000.0 ).margin( 1e-9 ) );
}

TEST_CASE( "rs:resample refuses interpolated kernels on categorical rasters",
           "[grid][resample][adv]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder builder( 4, 4, 1 );
    builder.withCrs( "EPSG:32650" );
    const QString inputPath = builder.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inputPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:resample" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = dir.filePath( "out.tif" ).toStdString();
    params["resolution"] = 20.0;
    params["resampling"] = "bilinear";
    params["categorical"] = true;
    RSOperatorContext context;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}

TEST_CASE( "rs:resample refuses inputs without a declared CRS",
           "[grid][resample][adv]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // The synthetic builder always stamps a CRS, so craft the CRS-less
    // input through GDAL directly: grid contracts are never inferred.
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const QString path = dir.filePath( "nocrs.tif" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> values( 16, 1.0f );
    GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 4, 4, values.data(), 4, 4,
                  GDT_Float32, 0, 0 );
    GDALClose( ds );

    auto op = RSOperatorRegistry::instance().create( "rs:resample" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = path.toStdString();
    params["output"] = dir.filePath( "out.tif" ).toStdString();
    params["resolution"] = 20.0;
    RSOperatorContext context;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}

TEST_CASE( "rs:align reproduces the reference grid exactly",
           "[grid][align][ka]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Reference: 3x3 at 25 m with an offset origin.
    constexpr int kRW = 3;
    constexpr int kRH = 3;
    RsSyntheticRasterBuilder refBuilder( kRW, kRH, 1 );
    refBuilder.withCrs( "EPSG:32650" );
    refBuilder.withGeoTransform( 1000.0, 25.0, 5000.0, -25.0 );
    for ( int y = 0; y < kRH; ++y )
        for ( int x = 0; x < kRW; ++x )
            refBuilder.withPixel( 1, x, y, 1.0f );
    const QString refPath = refBuilder.writeToDisk( dir.filePath( "ref.tif" ) );
    REQUIRE( !refPath.isEmpty() );

    // Input: 9x9 at 10 m, different origin AND resolution, fully covering
    // the reference extent so the known-answer check never samples outside
    // the source data.
    constexpr int kW = 9;
    constexpr int kH = 9;
    constexpr double kInOriginX = 995.0;
    constexpr double kInOriginY = 5015.0;
    RsSyntheticRasterBuilder inBuilder( kW, kH, 1 );
    inBuilder.withCrs( "EPSG:32650" );
    inBuilder.withGeoTransform( kInOriginX, 10.0, kInOriginY, -10.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            inBuilder.withPixel( 1, x, y, static_cast<float>( 10 * y + x ) );
    const QString inputPath = inBuilder.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !inputPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:align" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["reference"] = refPath.toStdString();
    params["output"] = dir.filePath( "aligned.tif" ).toStdString();
    params["resampling"] = "near";
    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    CHECK( result["alreadyAligned"].asBool() == false );
    CHECK( result["width"].asInt() == kRW );
    CHECK( result["height"].asInt() == kRH );

    // The output grid must equal the reference grid EXACTLY.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "aligned.tif" ) ) );
    CHECK( outDs.width() == kRW );
    CHECK( outDs.height() == kRH );
    const auto gt = outDs.geoTransform();
    CHECK( gt[0] == Catch::Approx( 1000.0 ).margin( 1e-9 ) );
    CHECK( gt[3] == Catch::Approx( 5000.0 ).margin( 1e-9 ) );
    CHECK( gt[1] == Catch::Approx( 25.0 ).margin( 1e-9 ) );
    CHECK( gt[5] == Catch::Approx( -25.0 ).margin( 1e-9 ) );

    // Nearest known answer: output cell (i, j) center lands in source
    // column/row of the 10 m input — computed from the grid math, not
    // hardcoded: source col = floor((1000 + 25i + 12.5 - 900) / 10).
    const std::vector<float> out = readBand( dir.filePath( "aligned.tif" ), 1, kRW, kRH );
    for ( int j = 0; j < kRH; ++j )
    {
        for ( int i = 0; i < kRW; ++i )
        {
            const double centerX = 1000.0 + 25.0 * i + 12.5;
            const double centerY = 5000.0 - 25.0 * j - 12.5;
            const int srcCol = static_cast<int>( std::floor( ( centerX - kInOriginX ) / 10.0 ) );
            const int srcRow = static_cast<int>( std::floor( ( kInOriginY - centerY ) / 10.0 ) );
            const float expected = static_cast<float>( 10 * srcRow + srcCol );
            INFO( "cell " << i << "," << j << " src " << srcCol << "," << srcRow );
            CHECK( out[static_cast<size_t>( j ) * kRW + i] == Catch::Approx( expected ) );
        }
    }
}

TEST_CASE( "rs:align publishes a lossless copy when grids already match",
           "[grid][align][ka]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RsSyntheticRasterBuilder builder( 4, 4, 1 );
    builder.withCrs( "EPSG:32650" );
    builder.withGeoTransform( 100.0, 5.0, 200.0, -5.0 );
    for ( int y = 0; y < 4; ++y )
        for ( int x = 0; x < 4; ++x )
            builder.withPixel( 1, x, y, static_cast<float>( 4 * y + x ) );
    const QString refPath = builder.writeToDisk( dir.filePath( "ref.tif" ) );
    const QString inputPath = builder.writeToDisk( dir.filePath( "in.tif" ) );
    REQUIRE( !refPath.isEmpty() );
    REQUIRE( !inputPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:align" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["reference"] = refPath.toStdString();
    params["output"] = dir.filePath( "aligned.tif" ).toStdString();
    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    CHECK( result["alreadyAligned"].asBool() == true );

    const std::vector<float> out = readBand( dir.filePath( "aligned.tif" ), 1, 4, 4 );
    for ( int i = 0; i < 16; ++i )
        CHECK( out[static_cast<size_t>( i )] == Catch::Approx( static_cast<float>( i ) ) );
}
