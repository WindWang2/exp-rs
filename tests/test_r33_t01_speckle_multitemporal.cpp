// tests/test_r33_t01_speckle_multitemporal.cpp — multitemporal speckle
// scene/band/grid contract (R33-T01, issue #2129): every companion scene's
// OWN declared band sentinel decides its missing samples (not the
// reference's), scenes must share the reference grid (CRS/transform, not
// just size) and carry the filtered band, and a pixel with no valid
// observation anywhere is NaN under the NaN-declared output — never the
// reference's finite sentinel written back as signal.
//
// Hand oracle: uniform 10/20/30 W cm⁻² scenes with kernelSize=3 and
// deviationK=10 — every local window is uniform after each scene's own
// sentinel taps drop out (variance 0, gate always passes), so each pixel's
// output is the mean of its valid observations exactly.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t01_speckle_mt";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

/// Writes a Float32 GTiff with an optional declared band sentinel and
/// optional geotransform (defaults: a plain EPSG-free grid is fine for the
/// kernel; the mismatch case needs distinct transforms).
bool writeScene( const QString &path, const std::vector<float> &values, int width,
                 int height, bool declareSentinel, float sentinel,
                 const double *geoTransform = nullptr, int bands = 1 )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, bands,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    if ( geoTransform )
        GDALSetGeoTransform( ds, const_cast<double *>( geoTransform ) );
    for ( int b = 1; b <= bands; ++b )
    {
        if ( declareSentinel && b == 1 )
            GDALSetRasterNoDataValue( GDALGetRasterBand( ds, b ),
                                      static_cast<double>( sentinel ) );
        const bool ok = GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width,
                                      height, const_cast<float *>( values.data() ), width,
                                      height, GDT_Float32, 0, 0 ) == CE_None;
        if ( !ok )
        {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

Json::Value runSpeckle( const QString &input, const std::vector<QString> &companions,
                        const QString &output, int band = 1 )
{
    auto op = RSOperatorRegistry::instance().create( "rs:sar_speckle" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["band"] = band;
    params["method"] = "multitemporal";
    params["kernelSize"] = 3;
    params["deviationK"] = 10.0;
    Json::Value scenes( Json::arrayValue );
    for ( const QString &c : companions )
        scenes.append( c.toStdString() );
    params["companionScenes"] = scenes;
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

} // anonymous namespace

TEST_CASE( "multitemporal speckle: per-scene sentinels, NaN on no observation (#2129)",
           "[r33t01][sar][speckle]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    constexpr int kN = 3;

    // Reference (sentinel -9999): uniform 10 with the CENTER invalid.
    std::vector<float> ref( kN * kN, 10.0f );
    ref[4] = -9999.0f;
    const QString refPath = tmp.filePath( "ref.tif" );
    REQUIRE( writeScene( refPath, ref, kN, kN, true, -9999.0f ) );

    // Companion 1 (sentinel -8888, DIFFERENT from ref's): uniform 20,
    // center invalid under its own declaration.
    std::vector<float> c1( kN * kN, 20.0f );
    c1[4] = -8888.0f;
    const QString c1Path = tmp.filePath( "c1.tif" );
    REQUIRE( writeScene( c1Path, c1, kN, kN, true, -8888.0f ) );

    // Companion 2 (sentinel -7777): uniform 30, center invalid.
    std::vector<float> c2( kN * kN, 30.0f );
    c2[4] = -7777.0f;
    const QString c2Path = tmp.filePath( "c2.tif" );
    REQUIRE( writeScene( c2Path, c2, kN, kN, true, -7777.0f ) );

    const QString outPath = tmp.filePath( "filtered.tif" );
    const Json::Value result =
        runSpeckle( refPath, { c1Path, c2Path }, outPath );
    REQUIRE( result["output"].asString() == outPath.toStdString() );

    const std::vector<float> out = readBand( outPath );
    REQUIRE( out.size() == static_cast<size_t>( kN ) * kN );
    for ( int i = 0; i < kN * kN; ++i )
    {
        INFO( "pixel " << i );
        if ( i == 4 )
        {
            // No valid observation in ANY scene → NaN (the pre-fix kernel
            // wrote the reference's finite -9999 back as a "valid" sample).
            REQUIRE( std::isnan( out[4] ) );
        }
        else
        {
            // Hand-computed temporal mean of three valid observations.
            REQUIRE( out[i] == Approx( 20.0f ).margin( 1e-5f ) );
        }
    }

    // Sentinel DIRECTION pin: a companion pixel equal to the REFERENCE's
    // sentinel is valid data for the companion when the companion declares
    // no sentinel of its own — only each scene's own declaration drops.
    std::vector<float> c0( kN * kN, 20.0f );
    c0[4] = -9999.0f; // equals ref's sentinel; c0 declares NOTHING
    const QString c0Path = tmp.filePath( "c0.tif" );
    REQUIRE( writeScene( c0Path, c0, kN, kN, false, 0.0f ) );

    const QString out2Path = tmp.filePath( "filtered2.tif" );
    runSpeckle( refPath, { c0Path }, out2Path );
    const std::vector<float> out2 = readBand( out2Path );
    // Center: ref invalid, companion -9999 VALID → mean = -9999/1 exactly.
    REQUIRE( out2[4] == Approx( -9999.0f ).margin( 1e-4f ) );
    // Others: (10 + 20)/2.
    REQUIRE( out2[0] == Approx( 15.0f ).margin( 1e-5f ) );
}

TEST_CASE( "multitemporal speckle refuses mismatched grids and band shortages (#2129)",
           "[r33t01][sar][speckle]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    constexpr int kN = 3;

    const std::vector<float> ref( kN * kN, 10.0f );
    const QString refPath = tmp.filePath( "ref.tif" );
    const double gtRef[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
    REQUIRE( writeScene( refPath, ref, kN, kN, false, 0.0f, gtRef ) );

    // Same SIZE, shifted transform → the lockstep tile walk samples
    // different ground; the kernel must refuse (pre-fix: only WxH compared).
    const QString shiftedPath = tmp.filePath( "shifted.tif" );
    const double gtShifted[6] = { 500100.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
    REQUIRE( writeScene( shiftedPath, ref, kN, kN, false, 0.0f, gtShifted ) );
    REQUIRE_THROWS_AS( runSpeckle( refPath, { shiftedPath },
                                   tmp.filePath( "out_shift.tif" ) ),
                       RSOperatorError );

    // Band shortage: reference carries band 2, the companion does not.
    const QString twoBandPath = tmp.filePath( "ref2band.tif" );
    REQUIRE( writeScene( twoBandPath, ref, kN, kN, false, 0.0f, gtRef, 2 ) );
    REQUIRE( writeScene( refPath, ref, kN, kN, false, 0.0f, gtRef ) );
    REQUIRE_THROWS_AS( runSpeckle( twoBandPath, { refPath },
                                   tmp.filePath( "out_band.tif" ), 2 ),
                       RSOperatorError );
}
