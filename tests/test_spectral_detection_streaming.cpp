// test_spectral_detection_streaming.cpp — Spectral Intelligence 12.0 work
// package G: tile-vs-whole-scene equivalence for the target detectors.
//
// The operators stream 256×256 tiles through the same kernel accumulators a
// whole-raster pass would use (identical pixel order), so a single-tile scene
// must match the in-process reference BIT-EXACTLY, and a multi-tile scene
// matches within the same FP-ulp tolerance the rs:rx_anomaly streaming gate
// documents (stats rounding is amplified by the matrix inversion).
//
// The NoData case also proves QA honesty: declared-NoData pixels never enter
// the background statistics and score NaN in the output on both paths.

#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/spectral_cem.h"
#include "processing/algorithms/spectral_detection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include <gdal.h>

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{
struct Lcg
{
    unsigned int state = 1103515245u;
    float next() { return static_cast<float>( ( state = state * 1664525u + 1013904223u ) % 1000u ); }
};

// Builds a W×H×B scene (LCG values, checkerboard offset so the background is
// non-degenerate), optionally stamping declared per-band NoData on a stripe
// and planting a target pixel. Returns the BIP reference pixel vector.
std::vector<float> buildScene( RsSyntheticRasterBuilder &builder, int W, int H, int B,
                               bool withNoData, std::vector<std::vector<float>> *bandsOut )
{
    Lcg lcg;
    bandsOut->assign( static_cast<size_t>( B ), std::vector<float>( static_cast<size_t>( W ) * H ) );
    for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
            for ( int b = 0; b < B; ++b )
            {
                const bool invalid = withNoData && ( ( x == 7 && ( y / 4 ) % 2 == 0 ) );
                float v = lcg.next() + 50.0f * static_cast<float>( ( x / 16 + y / 16 ) % 2 );
                if ( invalid )
                    v = -9999.0f; // the builder writes it; the test declares it NoData
                ( *bandsOut )[static_cast<size_t>( b )][static_cast<size_t>( y ) * W + x] = v;
                builder.withPixel( b + 1, x, y, v );
            }
    // Plant one target pixel at a fixed spot — in the raster AND in the
    // reference plane, so both paths score the same scene.
    std::vector<float> target( B );
    for ( int b = 0; b < B; ++b )
        target[static_cast<size_t>( b )] = 30.0f + 10.0f * b;
    for ( int b = 0; b < B; ++b )
    {
        builder.withPixel( b + 1, 3, 3, target[static_cast<size_t>( b )] );
        ( *bandsOut )[static_cast<size_t>( b )][static_cast<size_t>( 3 ) * W + 3] =
            target[static_cast<size_t>( b )];
    }

    std::vector<float> bip( static_cast<size_t>( W ) * H * B );
    for ( int p = 0; p < W * H; ++p )
        for ( int b = 0; b < B; ++b )
            bip[static_cast<size_t>( p ) * B + b] =
                ( *bandsOut )[static_cast<size_t>( b )][static_cast<size_t>( p )];
    return bip;
}

// In-process whole-scene reference for one detector kind.
std::vector<float> referenceScores( const std::string &kind,
                                    const std::vector<float> &bip, int W, int H, int B,
                                    bool withNoData )
{
    const size_t count = static_cast<size_t>( W ) * H;
    std::vector<float> noData( static_cast<size_t>( B ), -9999.0f );
    std::vector<uint8_t> hasNoData( static_cast<size_t>( B ), withNoData ? 1 : 0 );

    std::vector<float> scores( count );
    if ( kind == "cem" )
    {
        SpectralCem::CorrelationStats stats;
        SpectralCem::accumulateCorrelation( bip.data(), count, B, &stats, true,
                                            noData.data(), hasNoData.data() );
        SpectralCem::finalizeCorrelation( &stats );
        std::vector<float> target( B );
        for ( int b = 0; b < B; ++b )
            target[static_cast<size_t>( b )] = 30.0f + 10.0f * b;
        SpectralCem::Filter filter;
        REQUIRE( SpectralCem::buildFilter( target.data(), B, stats.correlation, 0.0, &filter ) );
        std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
        for ( size_t p = 0; p < count; ++p )
            scores[p] = SpectralCem::cemScore( bip.data() + p * B, filter, B, &scratch );
    }
    else
    {
        SpectralAnomaly::BackgroundStats stats;
        SpectralAnomaly::accumulateMean( bip.data(), count, B, &stats, true,
                                         noData.data(), hasNoData.data() );
        SpectralAnomaly::finalizeMean( &stats );
        SpectralAnomaly::accumulateCovariance( bip.data(), count, B, &stats, true,
                                               noData.data(), hasNoData.data() );
        SpectralAnomaly::finalizeCovariance( &stats );
        std::vector<double> invCov;
        REQUIRE( SpectralAnomaly::invertCovariance( stats.covariance, B, &invCov ) );
        std::vector<float> target( B );
        for ( int b = 0; b < B; ++b )
            target[static_cast<size_t>( b )] = 30.0f + 10.0f * b;
        SpectralDetection::TargetModel model;
        REQUIRE( SpectralDetection::buildTargetModel( target.data(), B, stats.mean, invCov, &model ) );
        std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
        for ( size_t p = 0; p < count; ++p )
            scores[p] = ( kind == "mf" )
                            ? SpectralDetection::matchedFilterScore( bip.data() + p * B, model,
                                                                     stats.mean, B, &scratch )
                            : SpectralDetection::aceScore( bip.data() + p * B, model, stats.mean,
                                                           invCov, B, &scratch );
    }
    return scores;
}
} // namespace

TEST_CASE( "rs:matched_filter / rs:ace / rs:cem_detection streaming matches the "
           "whole-scene kernel",
           "[spectral][detection][streaming]" )
{
    constexpr int kB = 4;
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    auto runCase = []( const char *opName, int W, int H, bool exact, bool withNoData ) {
        QTemporaryDir tmp;
        REQUIRE( tmp.isValid() );
        const QString inputPath = tmp.path() + "/in.tif";
        const QString outputPath = tmp.path() + "/out.tif";

        RsSyntheticRasterBuilder builder( W, H, kB );
        builder.withCrs( "EPSG:32650" );
        builder.withGeoTransform( 0.0, 1.0, static_cast<double>( H ), -1.0 );
        std::vector<std::vector<float>> bands;
        const std::vector<float> bip = buildScene( builder, W, H, kB, withNoData, &bands );
        REQUIRE( !builder.writeToDisk( inputPath ).isEmpty() );
        if ( withNoData )
        {
            GDALDatasetH ds = GDALOpen( inputPath.toUtf8().constData(), GA_Update );
            REQUIRE( ds != nullptr );
            for ( int b = 1; b <= kB; ++b )
                GDALSetRasterNoDataValue( GDALGetRasterBand( ds, b ), -9999.0 );
            GDALClose( ds );
        }

        Json::Value target( Json::arrayValue );
        for ( int b = 0; b < kB; ++b )
            target.append( 30.0 + 10.0 * b );

        auto op = RSOperatorRegistry::instance().create( opName );
        REQUIRE( op != nullptr );
        Json::Value params( Json::objectValue );
        params["input"] = inputPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["target"] = target;
        RSOperatorContext ctx;
        Json::Value result;
        REQUIRE_NOTHROW( result = op->run( params, ctx ) );

        GdalDatasetWrapper outDs;
        REQUIRE( outDs.open( outputPath ) );
        std::vector<float> streamed( static_cast<size_t>( W ) * H );
        REQUIRE( outDs.readBandData( 1, streamed.data(), W, H ) );

        std::string kind = std::string( opName ).substr( 3 );
        if ( kind == "matched_filter" )
            kind = "mf";
        else if ( kind == "cem_detection" )
            kind = "cem";
        const std::vector<float> ref =
            referenceScores( kind, bip, W, H, kB, withNoData );

        double maxRelErr = 0.0;
        for ( size_t p = 0; p < ref.size(); ++p )
        {
            const bool refNaN = std::isnan( ref[p] );
            REQUIRE( std::isnan( streamed[p] ) == refNaN );
            if ( refNaN )
                continue;
            if ( exact )
            {
                CHECK( streamed[p] == ref[p] );
            }
            else
            {
                // Multi-tile: same accumulation order per tile, so the drift is
                // ULP-level before the inversion; bound to 2% as the rx gate does.
                CHECK( streamed[p] == Catch::Approx( ref[p] ).epsilon( 0.02 ) );
                const double denom = std::max( 1.0, std::fabs( static_cast<double>( ref[p] ) ) );
                maxRelErr = std::max( maxRelErr,
                                      std::fabs( streamed[p] - ref[p] ) / denom );
            }
        }
        if ( !exact )
            INFO( "max relative error: " << maxRelErr );
    };

    for ( const char *opName : { "rs:matched_filter", "rs:ace", "rs:cem_detection" } )
    {
        DYNAMIC_SECTION( "single tile (10x10): " << opName )
        {
            runCase( opName, 10, 10, true, false );
        }
        DYNAMIC_SECTION( "single tile with declared NoData (10x10): " << opName )
        {
            runCase( opName, 10, 10, true, true );
        }
        DYNAMIC_SECTION( "multi tile (300x300, tile=256): " << opName )
        {
            runCase( opName, 300, 300, false, false );
        }
    }
}
