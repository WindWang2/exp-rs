// test_spectral_background_raster.cpp — Spectral Intelligence 13.0 work
// package B: independent background raster for the covariance detectors.
//
// The detectors need only SPECTRAL statistics from the background, so the
// contract is: same band count, wavelength grid reconcilable onto the scene
// grid (resampled with the shared kernels), and NO spatial co-registration —
// a differently sized / differently located background raster is legal.
//
// Oracles (independent of the implementation):
//   - constructed equivalence: a background raster holding exactly the scene's
//     pixels gives bit-identical scores (same pixels, same order); the same
//     pixels in a different layout give the same statistics within FP tolerance;
//   - hand-computed linear reconciliation: scene grid [400,600], background
//     grid [400,500,600,700,800] — resampling keeps bands 1 and 3 exactly, so
//     the correlation, the CEM filter and the scores are closed-form;
//   - hand-computed Gaussian reconciliation: a background spectrum that is a
//     linear ramp in wavelength resamples to the ramp value at the target
//     center (symmetric SRF), so the statistics stay closed-form;
//   - typed refusals: band mismatch, missing file, disjoint coverage, scene
//     without a wavelength grid, all-NoData background.

#include "processing/algorithms/spectral_cem.h"
#include "processing/algorithms/spectral_detection.h"
#include "processing/algorithms/spectral_resampling.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal.h>

#include <json/json.h>

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void stampWavelengths( const QString &path, const std::vector<double> &centers,
                       const std::vector<double> &fwhm = {} )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    for ( size_t b = 0; b < centers.size(); ++b )
    {
        GDALSetMetadataItem( GDALGetRasterBand( ds, static_cast<int>( b ) + 1 ), "WAVELENGTH",
                             std::to_string( centers[b] ).c_str(), nullptr );
        if ( b < fwhm.size() )
            GDALSetMetadataItem( GDALGetRasterBand( ds, static_cast<int>( b ) + 1 ), "FWHM",
                                 std::to_string( fwhm[b] ).c_str(), nullptr );
    }
    GDALClose( ds );
}

void declareNoData( const QString &path, int bands, double value )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    for ( int b = 1; b <= bands; ++b )
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, b ), value );
    GDALClose( ds );
}

std::vector<float> readBand( const QString &path )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    const int w = ds.width();
    const int h = ds.height();
    std::vector<float> out( static_cast<size_t>( w ) * h, 0.0f );
    REQUIRE( ds.readBandData( 1, out.data(), w, h ) );
    return out;
}

Json::Value targetParam( const std::vector<float> &t )
{
    Json::Value v( Json::arrayValue );
    for ( float x : t )
        v.append( static_cast<double>( x ) );
    return v;
}

// Runs one detector with an optional background raster and returns the
// single-band score plane plus the result JSON.
std::vector<float> runDetector( const char *opName, const QString &input, const QString &output,
                                const Json::Value &target,
                                const std::string &background, Json::Value *result )
{
    auto op = RSOperatorRegistry::instance().create( opName );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["target"] = target;
    if ( !background.empty() )
        params["background"] = background;
    RSOperatorContext ctx;
    Json::Value runResult = op->run( params, ctx );
    if ( result )
        *result = runResult;
    return readBand( output );
}

ErrorCode codeOf( const std::function<void()> &fn )
{
    try
    {
        fn();
    }
    catch ( const RSOperatorError &e )
    {
        return e.code();
    }
    catch ( ... )
    {
        return ErrorCode::Unknown;
    }
    return ErrorCode::Success;
}

// A deterministic 3-band 8×8 scene with a planted target pixel.
QString writeScene( QTemporaryDir &dir, const char *name, int W, int H,
                    std::vector<float> *targetOut, int *targetIndexOut )
{
    constexpr int kB = 3;
    RsSyntheticRasterBuilder builder( W, H, kB );
    builder.withCrs( "EPSG:32650" );
    builder.withGeoTransform( 0.0, 1.0, static_cast<double>( H ), -1.0 );
    for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
            for ( int b = 0; b < kB; ++b )
                builder.withPixel( b + 1, x, y,
                                   10.0f + 2.0f * b + static_cast<float>( ( x * 5 + y * 3 ) % 7 ) );
    const std::vector<float> target{ 30.0f, 40.0f, 50.0f };
    const int tx = 2, ty = 3;
    for ( int b = 0; b < kB; ++b )
        builder.withPixel( b + 1, tx, ty, target[static_cast<size_t>( b )] );
    const QString path = builder.writeToDisk( dir.filePath( QString::fromLatin1( name ) ) );
    REQUIRE( !path.isEmpty() );
    *targetOut = target;
    *targetIndexOut = ty * W + tx;
    return path;
}
} // namespace

TEST_CASE( "background raster equal to the scene gives bit-identical scores",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene.tif", 8, 8, &target, &targetIndex );

    for ( const char *opName : { "rs:matched_filter", "rs:ace", "rs:cem_detection",
                                 "rs:tcimf_detection" } )
    {
        DYNAMIC_SECTION( opName )
        {
            Json::Value paramsExtra( Json::objectValue );
            paramsExtra["target"] = targetParam( target );
            if ( std::string( opName ) == "rs:tcimf_detection" )
            {
                Json::Value rows( Json::arrayValue );
                Json::Value row( Json::arrayValue );
                for ( float x : { 10.0f, 40.0f, 20.0f } )
                    row.append( static_cast<double>( x ) );
                rows.append( row );
                paramsExtra["interference"] = rows;
            }

            Json::Value sceneResult;
            const std::vector<float> sceneScores =
                runDetector( opName, scene, dir.filePath( "scene_out.tif" ),
                             paramsExtra["target"], std::string(), &sceneResult );
            Json::Value bgResult;
            const std::vector<float> bgScores =
                runDetector( opName, scene, dir.filePath( "bg_out.tif" ),
                             paramsExtra["target"], scene.toStdString(), &bgResult );

            REQUIRE( sceneResult["backgroundSource"].asString() == "scene" );
            REQUIRE( bgResult["backgroundSource"].asString() == scene.toStdString() );
            REQUIRE( bgResult["backgroundSamples"].asUInt64() ==
                     sceneResult["backgroundSamples"].asUInt64() );
            // Same pixels, same order, same accumulators → bit-identical.
            for ( size_t p = 0; p < sceneScores.size(); ++p )
                CHECK( bgScores[p] == sceneScores[p] );
        }
    }
}

TEST_CASE( "background raster with the same pixels in a different layout gives the "
           "same statistics",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene2.tif", 8, 8, &target, &targetIndex );

    // The transposed scene as a differently sized background raster: the pixel
    // multiset is identical, the raster order (hence FP accumulation order)
    // differs. Spatial co-registration is NOT required by the contract.
    constexpr int kB = 3;
    RsSyntheticRasterBuilder bg( 8, 8, kB );
    bg.withCrs( "EPSG:32650" );
    bg.withGeoTransform( 0.0, 1.0, 8.0, -1.0 );
    GdalDatasetWrapper sceneDs;
    REQUIRE( sceneDs.open( scene ) );
    for ( int b = 0; b < kB; ++b )
    {
        std::vector<float> band( 64, 0.0f );
        REQUIRE( sceneDs.readBandData( b + 1, band.data(), 8, 8 ) );
        for ( int y = 0; y < 8; ++y )
            for ( int x = 0; x < 8; ++x )
                bg.withPixel( b + 1, x, y, band[static_cast<size_t>( x ) * 8 + y] );
    }
    const QString bgPath = bg.writeToDisk( dir.filePath( "bg_transposed.tif" ) );
    REQUIRE( !bgPath.isEmpty() );

    for ( const char *opName : { "rs:cem_detection", "rs:tcimf_detection" } )
    {
        DYNAMIC_SECTION( opName )
        {
            Json::Value extra( Json::objectValue );
            extra["target"] = targetParam( target );
            if ( std::string( opName ) == "rs:tcimf_detection" )
            {
                Json::Value rows( Json::arrayValue );
                Json::Value row( Json::arrayValue );
                for ( float x : { 10.0f, 40.0f, 20.0f } )
                    row.append( static_cast<double>( x ) );
                rows.append( row );
                extra["interference"] = rows;
            }

            Json::Value sceneResult;
            const std::vector<float> sceneScores =
                runDetector( opName, scene, dir.filePath( "s_out.tif" ), extra["target"],
                             std::string(), &sceneResult );
            Json::Value bgResult;
            const std::vector<float> bgScores = runDetector( opName, scene, dir.filePath( "t_out.tif" ),
                                                             extra["target"], bgPath.toStdString(),
                                                             &bgResult );

            REQUIRE( bgResult["backgroundSamples"].asUInt64() ==
                     sceneResult["backgroundSamples"].asUInt64() );
            for ( size_t p = 0; p < sceneScores.size(); ++p )
                if ( !std::isnan( sceneScores[p] ) )
                    CHECK( bgScores[p] == Catch::Approx( sceneScores[p] ).epsilon( 1e-9 ) );
        }
    }
}

TEST_CASE( "background raster really drives the filter (different statistics, "
           "different scores)",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene3.tif", 8, 8, &target, &targetIndex );

    // A background raster with a clearly different mean: 100x the scene scale.
    constexpr int kB = 3;
    RsSyntheticRasterBuilder bg( 6, 6, kB );
    bg.withCrs( "EPSG:32650" );
    for ( int y = 0; y < 6; ++y )
        for ( int x = 0; x < 6; ++x )
            for ( int b = 0; b < kB; ++b )
                bg.withPixel( b + 1, x, y,
                              500.0f + 30.0f * b + static_cast<float>( ( x * 3 + y * 5 ) % 5 ) );
    const QString bgPath = bg.writeToDisk( dir.filePath( "bg_scaled.tif" ) );
    REQUIRE( !bgPath.isEmpty() );

    const std::vector<float> sceneScores =
        runDetector( "rs:cem_detection", scene, dir.filePath( "cs_out.tif" ),
                     targetParam( target ), std::string(), nullptr );
    const std::vector<float> bgScores =
        runDetector( "rs:cem_detection", scene, dir.filePath( "cb_out.tif" ),
                     targetParam( target ), bgPath.toStdString(), nullptr );

    double maxDiff = 0.0;
    for ( size_t p = 0; p < sceneScores.size(); ++p )
        if ( !std::isnan( sceneScores[p] ) )
            maxDiff = std::max( maxDiff,
                                std::fabs( static_cast<double>( sceneScores[p] - bgScores[p] ) ) );
    REQUIRE( maxDiff > 1.0 ); // the background is genuinely consumed
    // The planted target still scores exactly 1 (the constraint is background-free).
    CHECK( bgScores[static_cast<size_t>( targetIndex )] == Catch::Approx( 1.0 ).margin( 1e-5 ) );
}

TEST_CASE( "background raster with a shifted grid is reconciled linearly "
           "(hand-computed)",
           "[spectral][detection][background][resample]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Scene: 2 bands at 400/600 nm. Background: 5 bands at 400/500/600/700/800.
    // Linear resampling onto [400,600] keeps bands 1 and 3 exactly, so a
    // background pixel (a,b,c,d,e) contributes (a,c).
    constexpr int kW = 4, kH = 4;
    RsSyntheticRasterBuilder scene( kW, kH, 2 );
    scene.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            for ( int b = 0; b < 2; ++b )
                scene.withPixel( b + 1, x, y, 5.0f + b ); // filler scores
    scene.withPixel( 1, 0, 0, 1.0f );
    scene.withPixel( 2, 0, 0, 2.0f ); // planted target
    scene.withPixel( 1, 1, 0, 10.0f );
    scene.withPixel( 2, 1, 0, 20.0f ); // background type A
    scene.withPixel( 1, 2, 0, 20.0f );
    scene.withPixel( 2, 2, 0, 10.0f ); // background type B
    const QString scenePath = scene.writeToDisk( dir.filePath( "lin_scene.tif" ) );
    REQUIRE( !scenePath.isEmpty() );
    stampWavelengths( scenePath, { 400.0, 600.0 } );

    RsSyntheticRasterBuilder bg( kW, kH, 5 );
    bg.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            // Half the pixels are type A (10,*,20,*,*), half type B (20,*,10,*,*);
            // the filler bands (99) must never reach the statistics.
            const bool typeA = ( ( x + y ) % 2 ) == 0;
            bg.withPixel( 1, x, y, typeA ? 10.0f : 20.0f );
            bg.withPixel( 2, x, y, 99.0f );
            bg.withPixel( 3, x, y, typeA ? 20.0f : 10.0f );
            bg.withPixel( 4, x, y, 99.0f );
            bg.withPixel( 5, x, y, 99.0f );
        }
    const QString bgPath = bg.writeToDisk( dir.filePath( "lin_bg.tif" ) );
    REQUIRE( !bgPath.isEmpty() );
    stampWavelengths( bgPath, { 400.0, 500.0, 600.0, 700.0, 800.0 } );

    Json::Value result;
    const std::vector<float> scores =
        runDetector( "rs:cem_detection", scenePath, dir.filePath( "lin_out.tif" ),
                     targetParam( { 1.0f, 2.0f } ), bgPath.toStdString(), &result );

    // Hand-computed: R = mean of outer products of (10,20) and (20,10)
    //   = [[250, 200], [200, 250]];  t = (1,2) ⇒ w = (−1/3, 2/3).
    REQUIRE( result["backgroundResampled"].asBool() );
    REQUIRE( result["backgroundCoverage"].asString() == "full" );
    REQUIRE( result["backgroundSamples"].asUInt64() == 16 );
    CHECK( scores[0] == Catch::Approx( 1.0 ).margin( 1e-6 ) );    // target scores 1
    CHECK( scores[1] == Catch::Approx( 10.0 ).margin( 1e-6 ) );   // wᵀ(10,20) = 10
    CHECK( scores[2] == Catch::Approx( 0.0 ).margin( 1e-6 ) );    // wᵀ(20,10) = 0
}

TEST_CASE( "background raster with a shifted grid is reconciled with the Gaussian "
           "SRF (linear-ramp invariance)",
           "[spectral][detection][background][resample]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Scene: 2 bands at 400/600 nm, FWHM 100. Background: 9 bands at
    // 100..900 nm, FWHM 100. A background spectrum that is a linear ramp in
    // wavelength resamples to the ramp value at the target center exactly
    // (the ±3σ window is symmetric and fully inside the source grid), so the
    // statistics stay closed-form.
    constexpr int kW = 4, kH = 4;
    RsSyntheticRasterBuilder scene( kW, kH, 2 );
    scene.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            for ( int b = 0; b < 2; ++b )
                scene.withPixel( b + 1, x, y, 5.0f + b );
    scene.withPixel( 1, 0, 0, 1.0f );
    scene.withPixel( 2, 0, 0, 2.0f ); // planted target
    scene.withPixel( 1, 1, 0, 0.0f );
    scene.withPixel( 2, 1, 0, 2.0f ); // resampled type A
    scene.withPixel( 1, 2, 0, -1.0f );
    scene.withPixel( 2, 2, 0, 1.0f ); // resampled type B
    const QString scenePath = scene.writeToDisk( dir.filePath( "g_scene.tif" ) );
    REQUIRE( !scenePath.isEmpty() );
    stampWavelengths( scenePath, { 400.0, 600.0 }, { 100.0, 100.0 } );

    RsSyntheticRasterBuilder bg( kW, kH, 9 );
    bg.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const bool typeA = ( ( x + y ) % 2 ) == 0;
            for ( int b = 0; b < 9; ++b )
            {
                const double lambda = 100.0 + 100.0 * b;
                const double v = typeA ? ( lambda - 400.0 ) / 100.0
                                       : ( lambda - 500.0 ) / 100.0;
                bg.withPixel( b + 1, x, y, static_cast<float>( v ) );
            }
        }
    const QString bgPath = bg.writeToDisk( dir.filePath( "g_bg.tif" ) );
    REQUIRE( !bgPath.isEmpty() );
    stampWavelengths( bgPath, { 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0 },
                      { 100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0 } );

    Json::Value result;
    const std::vector<float> scores =
        runDetector( "rs:cem_detection", scenePath, dir.filePath( "g_out.tif" ),
                     targetParam( { 1.0f, 2.0f } ), bgPath.toStdString(), &result );

    // Hand-computed: resampled pixels are A = (0,2) and B = (−1,1), half each
    //   R = [[0.5, −0.5], [−0.5, 2.5]],  t = (1,2) ⇒ w = (7/13, 3/13).
    REQUIRE( result["backgroundResampled"].asBool() );
    REQUIRE( result["backgroundCoverage"].asString() == "full" );
    CHECK( scores[0] == Catch::Approx( 1.0 ).margin( 1e-5 ) );   // target scores 1
    CHECK( scores[1] == Catch::Approx( 6.0 / 13.0 ).margin( 1e-5 ) );
    CHECK( scores[2] == Catch::Approx( -4.0 / 13.0 ).margin( 1e-5 ) );
}

TEST_CASE( "background raster refuses mismatched bands, missing files, disjoint "
           "coverage and all-NoData",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene4.tif", 8, 8, &target, &targetIndex );

    // Band mismatch: a 2-band background against a 3-band scene.
    RsSyntheticRasterBuilder wrongBands( 4, 4, 2 );
    wrongBands.withCrs( "EPSG:32650" );
    wrongBands.withConstantValue( 1, 1.0f );
    wrongBands.withConstantValue( 2, 2.0f );
    const QString wrongPath = wrongBands.writeToDisk( dir.filePath( "wrong_bands.tif" ) );
    REQUIRE( !wrongPath.isEmpty() );
    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", scene, dir.filePath( "wb.tif" ), targetParam( target ),
                     wrongPath.toStdString(), nullptr );
    } ) == ErrorCode::InvalidInputData );

    // Missing file.
    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", scene, dir.filePath( "mf.tif" ), targetParam( target ),
                     dir.filePath( "does_not_exist.tif" ).toStdString(), nullptr );
    } ) == ErrorCode::FileNotFound );

    // Disjoint wavelength coverage: scene at 400/600, background at 1000/2000.
    const QString bgFarPath = [&] {
        RsSyntheticRasterBuilder b( 4, 4, 3 );
        b.withCrs( "EPSG:32650" );
        for ( int band = 1; band <= 3; ++band )
            b.withConstantValue( band, 1.0f * band );
        const QString p = b.writeToDisk( dir.filePath( "bg_far.tif" ) );
        stampWavelengths( p, { 1000.0, 1500.0, 2000.0 } );
        return p;
    }();
    REQUIRE( !bgFarPath.isEmpty() );
    const QString sceneGridPath = [&] {
        RsSyntheticRasterBuilder b( 8, 8, 3 );
        b.withCrs( "EPSG:32650" );
        for ( int band = 1; band <= 3; ++band )
            b.withConstantValue( band, 1.0f * band );
        const QString p = b.writeToDisk( dir.filePath( "scene_grid.tif" ) );
        stampWavelengths( p, { 400.0, 500.0, 600.0 } );
        return p;
    }();
    REQUIRE( !sceneGridPath.isEmpty() );
    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", sceneGridPath, dir.filePath( "dc.tif" ),
                     targetParam( { 1.0f, 2.0f, 3.0f } ), bgFarPath.toStdString(), nullptr );
    } ) == ErrorCode::InvalidInputData );

    // All-NoData background: no valid samples at all.
    const QString bgNoDataPath = [&] {
        RsSyntheticRasterBuilder b( 4, 4, 3 );
        b.withCrs( "EPSG:32650" );
        for ( int band = 1; band <= 3; ++band )
            b.withConstantValue( band, -9999.0f );
        const QString p = b.writeToDisk( dir.filePath( "bg_nodata.tif" ) );
        declareNoData( p, 3, -9999.0 );
        return p;
    }();
    REQUIRE( !bgNoDataPath.isEmpty() );
    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", scene, dir.filePath( "nd.tif" ), targetParam( target ),
                     bgNoDataPath.toStdString(), nullptr );
    } ) == ErrorCode::InvalidInputData );
}

TEST_CASE( "background NoData pixels are excluded from the statistics",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene5.tif", 8, 8, &target, &targetIndex );

    constexpr int kB = 3;
    RsSyntheticRasterBuilder bg( 8, 8, kB );
    bg.withCrs( "EPSG:32650" );
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
            for ( int b = 0; b < kB; ++b )
            {
                const bool invalid = ( x < 4 ); // half the background is NoData
                bg.withPixel( b + 1, x, y, invalid ? -9999.0f
                                                   : 20.0f + 3.0f * b +
                                                         static_cast<float>( ( x + y ) % 5 ) );
            }
    const QString bgPath = bg.writeToDisk( dir.filePath( "bg_nd.tif" ) );
    REQUIRE( !bgPath.isEmpty() );
    declareNoData( bgPath, kB, -9999.0 );

    Json::Value result;
    runDetector( "rs:cem_detection", scene, dir.filePath( "nd_out.tif" ), targetParam( target ),
                 bgPath.toStdString(), &result );
    REQUIRE( result["backgroundSamples"].asUInt64() == 32 ); // half of 64

    // And the same background without declared NoData uses all 64 samples
    // (proving the exclusion comes from the predicate, not from the values).
    const QString bgPlainPath = dir.filePath( "bg_plain.tif" );
    {
        RsSyntheticRasterBuilder plain( 8, 8, kB );
        plain.withCrs( "EPSG:32650" );
        for ( int y = 0; y < 8; ++y )
            for ( int x = 0; x < 8; ++x )
                for ( int b = 0; b < kB; ++b )
                    plain.withPixel( b + 1, x, y,
                                     ( x < 4 ) ? -9999.0f
                                               : 20.0f + 3.0f * b +
                                                     static_cast<float>( ( x + y ) % 5 ) );
        REQUIRE( !plain.writeToDisk( bgPlainPath ).isEmpty() );
    }
    Json::Value plainResult;
    runDetector( "rs:cem_detection", scene, dir.filePath( "plain_out.tif" ), targetParam( target ),
                 bgPlainPath.toStdString(), &plainResult );
    REQUIRE( plainResult["backgroundSamples"].asUInt64() == 64 );
}

TEST_CASE( "OSP refuses a background raster (it consumes no background statistics)",
           "[spectral][detection][background][osp]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> target;
    int targetIndex = 0;
    const QString scene = writeScene( dir, "scene6.tif", 8, 8, &target, &targetIndex );

    Json::Value paramsExtra( Json::objectValue );
    paramsExtra["target"] = targetParam( target );
    Json::Value rows( Json::arrayValue );
    Json::Value row( Json::arrayValue );
    for ( float x : { 10.0f, 40.0f, 20.0f } )
        row.append( static_cast<double>( x ) );
    rows.append( row );
    paramsExtra["interference"] = rows;
    paramsExtra["background"] = scene.toStdString();

    auto op = RSOperatorRegistry::instance().create( "rs:osp_detection" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = scene.toStdString();
    params["output"] = dir.filePath( "osp_bg.tif" ).toStdString();
    params["target"] = paramsExtra["target"];
    params["interference"] = paramsExtra["interference"];
    params["background"] = paramsExtra["background"];
    RSOperatorContext ctx;
    REQUIRE_THROWS_AS( op->run( params, ctx ), RSOperatorError );
}

TEST_CASE( "background raster refuses when the scene has no wavelength grid and "
           "malformed background metadata",
           "[spectral][detection][background]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A 3-band background WITH a stamped grid, and a 3-band scene WITHOUT one:
    // the background carries WAVELENGTH metadata the scene cannot reconcile
    // against → typed refusal (band-order interpretation is not assumed when
    // one side declares a grid).
    RsSyntheticRasterBuilder bg( 4, 4, 3 );
    bg.withCrs( "EPSG:32650" );
    for ( int band = 1; band <= 3; ++band )
        bg.withConstantValue( band, 0.1f * band );
    const QString bgGridded = bg.writeToDisk( dir.filePath( "bg_gridded.tif" ) );
    REQUIRE( !bgGridded.isEmpty() );
    stampWavelengths( bgGridded, { 400.0, 500.0, 600.0 } );

    RsSyntheticRasterBuilder scene( 6, 6, 3 );
    scene.withCrs( "EPSG:32650" );
    for ( int band = 1; band <= 3; ++band )
        scene.withConstantValue( band, 0.2f * band );
    const QString sceneBare = scene.writeToDisk( dir.filePath( "scene_bare.tif" ) );
    REQUIRE( !sceneBare.isEmpty() );

    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", sceneBare, dir.filePath( "nogrid.tif" ),
                     targetParam( { 0.2f, 0.4f, 0.6f } ), bgGridded.toStdString(), nullptr );
    } ) == ErrorCode::InvalidInputData );

    // The same scene with a grid is accepted (band-order/equal-grid path) —
    // the refusal above is the grid asymmetry, not the raster itself.
    const QString sceneGridded = dir.filePath( "scene_gridded.tif" );
    {
        RsSyntheticRasterBuilder b( 6, 6, 3 );
        b.withCrs( "EPSG:32650" );
        for ( int band = 1; band <= 3; ++band )
            b.withConstantValue( band, 0.2f * band );
        REQUIRE( !b.writeToDisk( sceneGridded ).isEmpty() );
    }
    stampWavelengths( sceneGridded, { 400.0, 500.0, 600.0 } );
    Json::Value okResult;
    REQUIRE_NOTHROW( runDetector( "rs:cem_detection", sceneGridded,
                                  dir.filePath( "grid_ok.tif" ),
                                  targetParam( { 0.2f, 0.4f, 0.6f } ),
                                  bgGridded.toStdString(), &okResult ) );
    REQUIRE( okResult["backgroundSource"].asString() == bgGridded.toStdString() );
    // Identical grids → no resampling, no coverage key.
    REQUIRE( !okResult.isMember( "backgroundResampled" ) );

    // Malformed background WAVELENGTH metadata is a typed refusal.
    const QString bgBadMeta = dir.filePath( "bg_badmeta.tif" );
    {
        RsSyntheticRasterBuilder b( 4, 4, 3 );
        b.withCrs( "EPSG:32650" );
        for ( int band = 1; band <= 3; ++band )
            b.withConstantValue( band, 0.1f * band );
        REQUIRE( !b.writeToDisk( bgBadMeta ).isEmpty() );
    }
    {
        GDALDatasetH ds = GDALOpen( bgBadMeta.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        GDALSetMetadataItem( GDALGetRasterBand( ds, 1 ), "WAVELENGTH", "not-a-number",
                             nullptr );
        GDALClose( ds );
    }
    REQUIRE( codeOf( [&] {
        runDetector( "rs:cem_detection", sceneGridded, dir.filePath( "badmeta.tif" ),
                     targetParam( { 0.2f, 0.4f, 0.6f } ), bgBadMeta.toStdString(), nullptr );
    } ) == ErrorCode::InvalidInputData );
}

TEST_CASE( "background raster reports partial SRF coverage without refusing",
           "[spectral][detection][background][resample]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Scene bands at 900/950 nm with a wide FWHM (400): the Gaussian response
    // reaches past the background's 1000 nm edge, so coverage is Partial
    // (in-range but edge-truncated) — reported, not refused.
    constexpr int kW = 4;
    constexpr int kH = 4;
    RsSyntheticRasterBuilder scene( kW, kH, 2 );
    scene.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            for ( int b = 0; b < 2; ++b )
                scene.withPixel( b + 1, x, y, 5.0f + b );
    scene.withPixel( 1, 0, 0, 1.0f );
    scene.withPixel( 2, 0, 0, 2.0f ); // planted target
    const QString scenePath = scene.writeToDisk( dir.filePath( "p_scene.tif" ) );
    REQUIRE( !scenePath.isEmpty() );
    stampWavelengths( scenePath, { 900.0, 950.0 }, { 400.0, 400.0 } );

    RsSyntheticRasterBuilder bg( kW, kH, 2 );
    bg.withCrs( "EPSG:32650" );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const bool typeA = ( ( x + y ) % 2 ) == 0;
            bg.withPixel( 1, x, y, typeA ? 0.5f : 0.2f );
            bg.withPixel( 2, x, y, typeA ? 0.2f : 0.5f );
        }
    const QString bgPath = bg.writeToDisk( dir.filePath( "p_bg.tif" ) );
    REQUIRE( !bgPath.isEmpty() );
    stampWavelengths( bgPath, { 400.0, 1000.0 }, { 50.0, 50.0 } );

    Json::Value result;
    const std::vector<float> scores =
        runDetector( "rs:cem_detection", scenePath, dir.filePath( "p_out.tif" ),
                     targetParam( { 1.0f, 2.0f } ), bgPath.toStdString(), &result );

    REQUIRE( result["backgroundResampled"].asBool() );
    REQUIRE( result["backgroundCoverage"].asString() == "partial" );
    REQUIRE( result["backgroundSamples"].asUInt64() == 16 );

    // In-process plumbing oracle: resample the same background pixels with the
    // shared kernel, accumulate, build the CEM filter and score the planted
    // target — the operator must reproduce it (the kernel itself is
    // unit-tested elsewhere).
    std::vector<double> correlation( 4, 0.0 );
    size_t samples = 0;
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const bool typeA = ( ( x + y ) % 2 ) == 0;
            const float pixel[2] = { typeA ? 0.5f : 0.2f, typeA ? 0.2f : 0.5f };
            const float srcWl[2] = { 400.0f, 1000.0f };
            const float srcFwhm[2] = { 50.0f, 50.0f };
            const float dstWl[2] = { 900.0f, 950.0f };
            const float dstFwhm[2] = { 400.0f, 400.0f };
            float resampled[2] = { 0.0f, 0.0f };
            REQUIRE( SpectralResampling::resampleSpectrumGaussian( pixel, srcWl, 2, dstWl,
                                                                   dstFwhm, 2, resampled ) );
            for ( int i = 0; i < 2; ++i )
                for ( int j = 0; j < 2; ++j )
                    correlation[static_cast<size_t>( i ) * 2 + j] +=
                        static_cast<double>( resampled[i] ) * resampled[j];
            ++samples;
        }
    REQUIRE( samples == 16 );
    // finalizeCorrelation() divides the accumulated raw sum by count.
    SpectralCem::CorrelationStats stats;
    stats.bands = 2;
    stats.count = samples;
    stats.correlation = correlation;
    SpectralCem::finalizeCorrelation( &stats );
    const float target[2] = { 1.0f, 2.0f };
    SpectralCem::Filter filter;
    REQUIRE( SpectralCem::buildFilter( target, 2, stats.correlation, 0.0, &filter ) );
    std::vector<double> scratch( 2, 0.0 );
    const float expectedTarget = SpectralCem::cemScore( target, filter, 2, &scratch );
    REQUIRE( scores[0] == Catch::Approx( expectedTarget ).margin( 1e-5 ) );
    REQUIRE( scores[0] == Catch::Approx( 1.0 ).margin( 1e-5 ) ); // distortionless
}
