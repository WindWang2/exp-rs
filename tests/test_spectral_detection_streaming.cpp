// test_spectral_detection_streaming.cpp — Spectral Intelligence 12.0 work
// package G: tile-vs-whole-scene equivalence for the target detectors.
//
// The operators stream 256×256 tiles through the same kernel accumulators a
// whole-raster pass would use (identical pixel order), so a single-tile scene
// must match the in-process reference BIT-EXACTLY, and a multi-tile scene
// matches within the same FP-ulp tolerance the rs:rx_anomaly streaming gate
// documents (stats rounding is amplified by the matrix inversion).
//
// #1183: background accumulation is capped at max(minSamples*16, bands*64)
// samples, stopping after the whole 256×256 tile that crosses the budget —
// a deterministic first-tiles prefix. The reference replicates that documented
// contract (same tile order, same in-tile pixel order) instead of demanding
// full-scene statistics the driver no longer computes; scoring itself still
// streams every tile of the scene against that background.
//
// The NoData case also proves QA honesty: declared-NoData pixels are excluded
// from the background statistics identically on both paths. (Their scores
// stay finite — the kernels NaN only non-finite inputs — but the two paths
// must agree pixel-for-pixel on the excluded statistics.)

#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/spectral_cem.h"
#include "processing/algorithms/spectral_detection.h"
#include "processing/algorithms/spectral_osp.h"
#include "processing/algorithms/spectral_tcimf.h"

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

// In-process whole-scene reference for one detector kind. @a interference
// carries the undesired spectra (bands values each) for the TCIMF/OSP kinds;
// it is ignored by the covariance-based kinds.
std::vector<float> referenceScores( const std::string &kind,
                                    const std::vector<float> &bip, int W, int H, int B,
                                    bool withNoData,
                                    const std::vector<std::vector<float>> &interference = {} )
{
    const size_t count = static_cast<size_t>( W ) * H;
    std::vector<float> noData( static_cast<size_t>( B ), -9999.0f );
    std::vector<uint8_t> hasNoData( static_cast<size_t>( B ), withNoData ? 1 : 0 );
    // #1150: the reference applies the validity predicate ITSELF — the
    // kernels only check isfinite, so without this post-mask a finite
    // sentinel pixel would compare its bogus finite score as "expected" and
    // codify the driver's omission as the oracle.
    const auto maskInvalid = [&]( std::vector<float> &s ) {
        for ( size_t p = 0; p < count; ++p )
        {
            const float *x = bip.data() + p * B;
            for ( int b = 0; b < B; ++b )
            {
                if ( !std::isfinite( x[b] )
                     || ( hasNoData[static_cast<size_t>( b )]
                          && x[b] == noData[static_cast<size_t>( b )] ) )
                {
                    s[p] = std::numeric_limits<float>::quiet_NaN();
                    break;
                }
            }
        }
    };

    std::vector<float> scores( count );
    std::vector<float> target( static_cast<size_t>( B ) );
    for ( int b = 0; b < B; ++b )
        target[static_cast<size_t>( b )] = 30.0f + 10.0f * b;

    // #1183 background prefix: replicate the driver's deterministic first-
    // tiles accumulation — 256×256 tiles in row-major order, stopping after
    // the tile whose cumulative pixel count crosses the budget. In-tile pixel
    // order matches the driver's BIP window order, so the single-tile prefix
    // accumulates bit-exactly. (These cases carry no declared NoData, so the
    // valid-pixel count equals the consumed pixel count.)
    const size_t budget = std::max<size_t>(
        static_cast<size_t>( SpectralCem::minSamplesRequired( B, false ) ) * 16,
        static_cast<size_t>( B ) * 64 );
    std::vector<float> bgBip;
    size_t accumulated = 0;
    for ( int ty = 0; ty < H && accumulated < budget; ty += 256 )
    {
        for ( int tx = 0; tx < W && accumulated < budget; tx += 256 )
        {
            const int tw = std::min( 256, W - tx );
            const int th = std::min( 256, H - ty );
            bgBip.reserve( static_cast<size_t>( tw ) * th * B );
            for ( int y = ty; y < ty + th; ++y )
                for ( int x = tx; x < tx + tw; ++x )
                    for ( int b = 0; b < B; ++b )
                        bgBip.push_back(
                            bip[( static_cast<size_t>( y ) * W + x ) * B + b] );
            accumulated += static_cast<size_t>( tw ) * th;
        }
    }
    const size_t bgCount = bgBip.size() / static_cast<size_t>( B );

    if ( kind == "osp" )
    {
        // OSP consumes no background statistics: filter, then score.
        SpectralOsp::Filter filter;
        REQUIRE( SpectralOsp::buildFilter( target.data(), B, interference, &filter ) );
        std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
        for ( size_t p = 0; p < count; ++p )
            scores[p] = SpectralOsp::ospScore( bip.data() + p * B, filter, B, &scratch );
        maskInvalid( scores );
        return scores;
    }

    if ( kind == "cem" || kind == "tcimf" )
    {
        SpectralCem::CorrelationStats stats;
        SpectralCem::accumulateCorrelation( bgBip.data(), bgCount, B, &stats, true,
                                            noData.data(), hasNoData.data() );
        SpectralCem::finalizeCorrelation( &stats );
        if ( kind == "cem" )
        {
            SpectralCem::Filter filter;
            REQUIRE( SpectralCem::buildFilter( target.data(), B, stats.correlation, 0.0, &filter ) );
            std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
            for ( size_t p = 0; p < count; ++p )
                scores[p] = SpectralCem::cemScore( bip.data() + p * B, filter, B, &scratch );
            maskInvalid( scores );
            return scores;
        }
        SpectralTcimf::Filter filter;
        REQUIRE( SpectralTcimf::buildFilter( target.data(), B, interference, stats.correlation,
                                             0.0, &filter ) );
        std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
        for ( size_t p = 0; p < count; ++p )
            scores[p] = SpectralTcimf::tcimfScore( bip.data() + p * B, filter, B, &scratch );
        maskInvalid( scores );
        return scores;
    }

    {
        SpectralAnomaly::BackgroundStats stats;
        SpectralAnomaly::accumulateMean( bgBip.data(), bgCount, B, &stats, true,
                                         noData.data(), hasNoData.data() );
        SpectralAnomaly::finalizeMean( &stats );
        SpectralAnomaly::accumulateCovariance( bgBip.data(), bgCount, B, &stats, true,
                                               noData.data(), hasNoData.data() );
        SpectralAnomaly::finalizeCovariance( &stats );
        std::vector<double> invCov;
        REQUIRE( SpectralAnomaly::invertCovariance( stats.covariance, B, &invCov ) );
        SpectralDetection::TargetModel model;
        REQUIRE( SpectralDetection::buildTargetModel( target.data(), B, stats.mean, invCov, &model ) );
        std::vector<double> scratch( static_cast<size_t>( B ), 0.0 );
        for ( size_t p = 0; p < count; ++p )
            scores[p] = ( kind == "mf" )
                            ? SpectralDetection::matchedFilterScore( bip.data() + p * B, model,
                                                                     stats.mean, B, &scratch )
                            : SpectralDetection::aceScore( bip.data() + p * B, model, stats.mean,
                                                           invCov, B, &scratch );
        maskInvalid( scores );
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

    auto runCase = []( const char *opName, int W, int H, bool exact, bool withNoData,
                       const Json::Value &interference = Json::Value() ) {
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
        if ( !interference.isNull() )
            params["interference"] = interference;
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
        else if ( kind == "tcimf_detection" )
            kind = "tcimf";
        else if ( kind == "osp_detection" )
            kind = "osp";
        std::vector<std::vector<float>> interferenceSpectra;
        if ( !interference.isNull() )
            for ( const auto &row : interference )
            {
                std::vector<float> s;
                for ( const auto &v : row )
                    s.push_back( static_cast<float>( v.asDouble() ) );
                interferenceSpectra.push_back( std::move( s ) );
            }
        const std::vector<float> ref =
            referenceScores( kind, bip, W, H, kB, withNoData, interferenceSpectra );

        double maxRelErr = 0.0;
        size_t plantedNoData = 0;
        for ( size_t p = 0; p < ref.size(); ++p )
        {
            const bool refNaN = std::isnan( ref[p] );
            REQUIRE( std::isnan( streamed[p] ) == refNaN );
            if ( refNaN )
            {
                if ( withNoData )
                    ++plantedNoData;
                continue;
            }
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
        // #1150: with a declared NoData the scene must actually plant
        // sentinel pixels, and every one of them must come out NaN in the
        // streamed output (the driver's own validity predicate).
        if ( withNoData )
        {
            REQUIRE( plantedNoData > 0 );
            for ( size_t p = 0; p < bip.size(); p = p + kB )
            {
                bool sentinel = false;
                for ( int b = 0; b < kB; ++b )
                    if ( bip[p + static_cast<size_t>( b )] == -9999.0f )
                        sentinel = true;
                if ( sentinel )
                {
                    const size_t pixel = p / static_cast<size_t>( kB );
                    INFO( "planted -9999 pixel " << pixel << " must score NaN, got "
                          << streamed[pixel] );
                    REQUIRE( std::isnan( streamed[pixel] ) );
                }
            }
        }
    };

    // Interference spectrum for the TCIMF/OSP kinds: linearly independent of
    // the planted target (30,40,50,60) and of itself across bands.
    auto makeInterference = []() {
        Json::Value rows( Json::arrayValue );
        Json::Value row( Json::arrayValue );
        row.append( 10.0 );
        row.append( 40.0 );
        row.append( 20.0 );
        row.append( 60.0 );
        rows.append( row );
        return rows;
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

    // Spectral Intelligence 13.0: TCIMF shares the CEM background pass; OSP
    // has none. Both stream like the covariance detectors.
    for ( const char *opName : { "rs:tcimf_detection", "rs:osp_detection" } )
    {
        DYNAMIC_SECTION( "single tile (10x10): " << opName )
        {
            runCase( opName, 10, 10, true, false, makeInterference() );
        }
        DYNAMIC_SECTION( "single tile with declared NoData (10x10): " << opName )
        {
            runCase( opName, 10, 10, true, true, makeInterference() );
        }
        DYNAMIC_SECTION( "multi tile (300x300, tile=256): " << opName )
        {
            runCase( opName, 300, 300, false, false, makeInterference() );
        }
    }
}
