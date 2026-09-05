// tests/test_sar_operators.cpp — Platform 3.0 SAR operator end-to-end tests
// (goal §6): synthetic SAR fixtures flow through the registry → operator →
// streaming kernel → output path, with numeric values asserted against the
// analytic expectations from the kernel references.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <string>

#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::operators;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_operators";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

/// Writes a single-band Float32 raster with the given values.
bool writeRaster( const QString &path, const std::vector<float> &values, int width, int height )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { 500000, 10, 0, 4500000, 0, -10 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
    }
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( GDALRasterIO( band, GF_Write, 0, 0, width, height,
                       const_cast<float *>( values.data() ), width, height, GDT_Float32,
                       0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }
    GDALClose( ds );
    return true;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}

} // namespace

TEST_CASE( "rs:sar_calibrate converts DN to sigma0 with recorded metadata",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "dn.tif" );
    const QString output = tmp.filePath( "sigma0.tif" );

    // DN = 4 everywhere; A = 2 → sigma0 = 16/4 = 4.0 linear, 6.0206 dB.
    REQUIRE( writeRaster( input, std::vector<float>( 16, 4.0f ), 4, 4 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_calibrate" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["calibrationA"] = 2.0;
    params["polarizations"] = "VV";
    params["sensor"] = "IW";
    params["incidenceDeg"] = 35.0;

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["calibration"].asString() == "sigma0" );
    REQUIRE( result["domain"].asString() == "linear_power" );

    const auto values = readBand( output );
    REQUIRE( values.size() == 16 );
    for ( float v : values )
        REQUIRE( v == Approx( 4.0f ).margin( 1e-6 ) );

    // The output carries the Platform-3.0 SAR contract for re-ingestion.
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( output ) );
        GDALDatasetH h = static_cast<GDALDatasetH>( ds.dataset() );
        const char *calibration = GDALGetMetadataItem( h, "SICNU_SAR_CALIBRATION", nullptr );
        const char *domain = GDALGetMetadataItem( h, "SICNU_SAR_DOMAIN", nullptr );
        const char *modality = GDALGetMetadataItem( h, "SICNU_MODALITY", nullptr );
        const char *polarizations = GDALGetMetadataItem( h, "SICNU_POLARIZATIONS", nullptr );
        const char *sensor = GDALGetMetadataItem( h, "SICNU_SENSOR", nullptr );
        REQUIRE( calibration != nullptr );
        REQUIRE( std::string( calibration ) == "sigma0" );
        REQUIRE( domain != nullptr );
        REQUIRE( std::string( domain ) == "linear_power" );
        REQUIRE( modality != nullptr );
        REQUIRE( std::string( modality ) == "sar" );
        REQUIRE( polarizations != nullptr );
        REQUIRE( std::string( polarizations ) == "VV" );
        REQUIRE( sensor != nullptr );
        REQUIRE( std::string( sensor ) == "IW" );
    }
}

TEST_CASE( "rs:sar_calibrate dB output converts nonpositive power to NoData",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "dn.tif" );
    const QString output = tmp.filePath( "sigma0_db.tif" );

    // DN values 0, 2, 4, 8 → sigma0 (A=1): 0, 4, 16, 64 → dB: NoData, 6.02, 12.04, 18.06.
    REQUIRE( writeRaster( input, { 0.f, 2.f, 4.f, 8.f }, 2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_calibrate" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["calibrationA"] = 1.0;
    params["outputDomain"] = "db";

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["domain"].asString() == "db" );

    const auto values = readBand( output );
    REQUIRE( values.size() == 4 );
    REQUIRE( std::isnan( values[0] ) );                       // 0 power → NoData
    REQUIRE( values[1] == Approx( 6.0206f ).margin( 1e-3 ) );
    REQUIRE( values[2] == Approx( 12.0412f ).margin( 1e-3 ) );
    REQUIRE( values[3] == Approx( 18.0618f ).margin( 1e-3 ) );
}

TEST_CASE( "rs:sar_backscatter converts sigma0 to gamma0 with constant incidence",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "sigma0.tif" );
    const QString output = tmp.filePath( "gamma0.tif" );

    // sigma0 = 0.25 at θ0 = 60° → gamma0 = 0.5.
    REQUIRE( writeRaster( input, std::vector<float>( 4, 0.25f ), 2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_backscatter" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["fromCalibration"] = "sigma0";
    params["toCalibration"] = "gamma0";
    params["incidenceDeg"] = 60.0;

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["calibration"].asString() == "gamma0" );

    const auto values = readBand( output );
    REQUIRE( values.size() == 4 );
    for ( float v : values )
        REQUIRE( v == Approx( 0.5f ).margin( 1e-6 ) );
}

TEST_CASE( "rs:sar_backscatter refuses geometry-free state conversions",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "sigma0.tif" );
    REQUIRE( writeRaster( input, std::vector<float>( 4, 0.25f ), 2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_backscatter" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "gamma0.tif" ).toStdString();
    params["fromCalibration"] = "sigma0";
    params["toCalibration"] = "gamma0";
    // No incidenceDeg, no incidenceRaster.

    RSOperatorContext ctx;
    REQUIRE_THROWS_AS( op->run( params, ctx ), RSOperatorError );
}

TEST_CASE( "rs:sar_speckle keeps a homogeneous scene at its level",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "flat.tif" );
    const QString output = tmp.filePath( "flat_lee.tif" );

    REQUIRE( writeRaster( input, std::vector<float>( 64, 2.5f ), 8, 8 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_speckle" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["method"] = "lee";
    params["kernelSize"] = 3;
    params["noiseVariance"] = 0.25;

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["bands"].asInt() == 1 );

    const auto values = readBand( output );
    REQUIRE( values.size() == 64 );
    // Lee of a homogeneous window = the window mean ( speckle filter identity ).
    for ( float v : values )
        REQUIRE( v == Approx( 2.5f ).margin( 1e-4 ) );
}

TEST_CASE( "rs:sar_speckle filters every band with band=0", "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "two_band.tif" );
    const QString output = tmp.filePath( "two_band_lee.tif" );

    // Two-band raster (write band 2 by rewriting band 1 twice).
    REQUIRE( writeRaster( input, std::vector<float>( 36, 4.0f ), 6, 6 ) );
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( input ) );
        GDALDatasetH h = static_cast<GDALDatasetH>( ds.dataset() );
        GDALRasterBandH band2 = GDALGetRasterBand( h, 1 );
        REQUIRE( band2 != nullptr );
        // Add a second band via CreateCopy-free approach: use the driver.
    }
    // Simpler: rebuild with 2 bands directly.
    {
        ensureGdalInit();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH h = GDALCreate( driver, input.toUtf8().constData(), 6, 6, 2, GDT_Float32, nullptr );
        REQUIRE( h != nullptr );
        std::vector<float> b1( 36, 4.0f );
        std::vector<float> b2( 36, 9.0f );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( h, 1 ), GF_Write, 0, 0, 6, 6, b1.data(), 6, 6,
                               GDT_Float32, 0, 0 ) == CE_None );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( h, 2 ), GF_Write, 0, 0, 6, 6, b2.data(), 6, 6,
                               GDT_Float32, 0, 0 ) == CE_None );
        GDALClose( h );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:sar_speckle" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["method"] = "frost";
    params["band"] = 0;

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["bands"].asInt() == 2 );

    const auto band1 = readBand( output, 1 );
    const auto band2 = readBand( output, 2 );
    REQUIRE( band1.size() == 36 );
    REQUIRE( band2.size() == 36 );
    for ( float v : band1 )
        REQUIRE( v == Approx( 4.0f ).margin( 1e-4 ) );
    for ( float v : band2 )
        REQUIRE( v == Approx( 9.0f ).margin( 1e-4 ) );
}

TEST_CASE( "rs:sar_ratio computes the analytic log-ratio", "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString a = tmp.filePath( "a.tif" );
    const QString b = tmp.filePath( "b.tif" );
    const QString output = tmp.filePath( "ratio.tif" );

    // Powers 0.5 and 2.0 → log-ratio = 10·log10(0.25) ≈ −6.02 dB.
    REQUIRE( writeRaster( a, std::vector<float>( 4, 0.5f ), 2, 2 ) );
    REQUIRE( writeRaster( b, std::vector<float>( 4, 2.0f ), 2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_ratio" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["inputA"] = a.toStdString();
    params["inputB"] = b.toStdString();
    params["output"] = output.toStdString();
    params["outputType"] = "log_ratio";

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["outputType"].asString() == "log_ratio" );

    const auto values = readBand( output );
    for ( float v : values )
        REQUIRE( v == Approx( -6.0206f ).margin( 1e-3 ) );
}

TEST_CASE( "rs:sar_ratio refuses mismatched grids", "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString a = tmp.filePath( "a.tif" );
    const QString b = tmp.filePath( "b.tif" );
    REQUIRE( writeRaster( a, std::vector<float>( 4, 1.0f ), 2, 2 ) );
    REQUIRE( writeRaster( b, std::vector<float>( 9, 1.0f ), 3, 3 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_ratio" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["inputA"] = a.toStdString();
    params["inputB"] = b.toStdString();
    params["output"] = tmp.filePath( "ratio.tif" ).toStdString();

    RSOperatorContext ctx;
    REQUIRE_THROWS_AS( op->run( params, ctx ), RSOperatorError );
}

TEST_CASE( "rs:sar_texture derives one band per requested measure",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "tex.tif" );
    const QString output = tmp.filePath( "tex_out.tif" );

    // Checkerboard: high contrast/entropy, known homogeneity within windows
    // that are uniform at the border (handled by the NaN window policy).
    std::vector<float> values( 36 );
    for ( int y = 0; y < 6; ++y )
        for ( int x = 0; x < 6; ++x )
            values[y * 6 + x] = ( ( x + y ) % 2 == 0 ) ? 1.0f : 3.0f;
    REQUIRE( writeRaster( input, values, 6, 6 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_texture" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["windowSize"] = 3;
    params["quantLevels"] = 4;
    Json::Value measures( Json::arrayValue );
    measures.append( "contrast" );
    measures.append( "homogeneity" );
    params["measures"] = measures;

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["bands"].asInt() == 2 );
    REQUIRE( result["measures"].size() == 2 );

    // Center pixel window is a 3×3 checkerboard: quantization to 4 levels
    // spans [1,3] → levels {0,3}; every horizontal pair connects 0↔3
    // (distance 3), normalized symmetric mass 0.25 per entry of the 4
    // populated entries → contrast = Σ d²·p = 9·(4·0.25) = 9.
    const auto contrast = readBand( output, 1 );
    const float center = contrast[3 * 6 + 3];
    REQUIRE( center == Approx( 9.0f ).epsilon( 0.05f ) );
}

TEST_CASE( "rs:sar_terrain_flatten is the identity on flat DEMs",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "sigma0.tif" );
    const QString dem = tmp.filePath( "dem.tif" );
    const QString output = tmp.filePath( "gamma0.tif" );

    REQUIRE( writeRaster( input, std::vector<float>( 16, 0.4f ), 4, 4 ) );
    REQUIRE( writeRaster( dem, std::vector<float>( 16, 100.0f ), 4, 4 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_flatten" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["dem"] = dem.toStdString();
    params["incidenceDeg"] = 35.0;
    params["demUnit"] = "meters";

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    REQUIRE( result["calibration"].asString() == "gamma0" );

    // Flat facets keep thetaI == theta0 → the flattening ratio is exactly 1.
    const auto values = readBand( output );
    for ( float v : values )
        REQUIRE( v == Approx( 0.4f ).margin( 1e-5 ) );
}

TEST_CASE( "rs:sar_terrain_flatten refuses mismatched DEM grids", "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = tmp.filePath( "sigma0.tif" );
    const QString dem = tmp.filePath( "dem.tif" );
    REQUIRE( writeRaster( input, std::vector<float>( 4, 0.4f ), 2, 2 ) );
    REQUIRE( writeRaster( dem, std::vector<float>( 9, 100.0f ), 3, 3 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_flatten" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "gamma0.tif" ).toStdString();
    params["dem"] = dem.toStdString();

    RSOperatorContext ctx;
    REQUIRE_THROWS_AS( op->run( params, ctx ), RSOperatorError );
}

TEST_CASE( "rs:sar_change derives a change mask from a planted dB shift",
           "[sar][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString a = tmp.filePath( "before.tif" );
    const QString b = tmp.filePath( "after.tif" );
    const QString output = tmp.filePath( "change.tif" );

    // Half the pixels change by +6 dB (factor 4 in power), half stay.
    std::vector<float> before( 16, 1.0f );
    std::vector<float> after( 16, 1.0f );
    for ( int i = 0; i < 8; ++i )
        after[i] = 4.0f;
    REQUIRE( writeRaster( a, before, 4, 4 ) );
    REQUIRE( writeRaster( b, after, 4, 4 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_change" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["inputA"] = a.toStdString();
    params["inputB"] = b.toStdString();
    params["output"] = output.toStdString();
    params["thresholdMethod"] = "manual";
    params["threshold"] = 3.0; // dB; changed pixels sit at |ΔdB| ≈ 6.02

    RSOperatorContext ctx;
    Json::Value result = op->run( params, ctx );
    const int changed = result["changedPixels"].asInt();
    const int evaluated = result["evaluatedPixels"].asInt();
    REQUIRE( evaluated == 16 );
    REQUIRE( changed == 8 );
}
