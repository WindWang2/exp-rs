// tests/test_swarm_fix_processing.cpp — regression anchors for the
// src/processing/** fixes made on branch fix/swarm-review-2026-11-04
// (SWARM_REVIEW.md, 2026-11-04). One TEST_CASE per finding; each names the
// finding id it pins.
//
// Oracles are derived independently here (closed-form geometry, hand-computed
// constants) — never by calling the code under test.
//
// Registration (tests/CMakeLists.txt is outside this change's ownership; the
// mainline must add the block below):
//
//   add_executable(test_swarm_fix_processing test_swarm_fix_processing.cpp)
//   target_link_libraries(test_swarm_fix_processing PRIVATE
//     Catch2::Catch2WithMain
//     Qt6::Core
//     qgis_core
//     sicnu_processing
//     GDAL::GDAL
//   )
//   target_include_directories(test_swarm_fix_processing PRIVATE
//     ${CMAKE_SOURCE_DIR}/src
//     ${CMAKE_SOURCE_DIR}/tests
//     ${CMAKE_BINARY_DIR}
//   )
//   sicnu_discover_tests(test_swarm_fix_processing)

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/change_detector.h"
#include "processing/algorithms/classifier_engine.h"
#include "processing/algorithms/gcp_manager.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/algorithms/image_fusion.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/terrain_hydrology.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QFile>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

using Catch::Approx;

namespace
{

constexpr float kNo = -9999.0f;

constexpr double kTwoPowMinus20 = 9.5367431640625e-07; // 2^-20

/// One ulp of a float32 at magnitude @p z (binade bottom convention).
float ulpAt( float z )
{
    return std::ldexp( 1.0f, std::ilogb( z ) - 23 );
}

} // namespace

// ===========================================================================
// PROC-A2-4 — slope / aspect / hillshade must not fabricate a flat cell when
// the 3x3 window carries no gradient evidence on an axis.
// ===========================================================================

TEST_CASE( "PROC-A2-4: a valid centre pixel whose 3x3 neighbours are all NoData "
           "yields nodata, not a fabricated flat",
           "[swarm][proc-a2-4][terrain]" )
{
    constexpr int W = 3, H = 3;
    std::vector<float> dem( W * H, kNo );
    dem[1 * W + 1] = 100.0f; // the only valid pixel

    std::vector<float> slopeOut( W * H, 0.0f );
    std::vector<float> aspectOut( W * H, 0.0f );
    std::vector<float> hsOut( W * H, 0.0f );

    REQUIRE( TerrainAnalysis::slope( dem.data(), slopeOut.data(), W, H, 1.0f, kNo ) );
    REQUIRE( TerrainAnalysis::aspect( dem.data(), aspectOut.data(), W, H, 1.0f, kNo ) );
    REQUIRE( TerrainAnalysis::hillshade( dem.data(), hsOut.data(), W, H, 1.0f, kNo, 315.0f, 45.0f ) );

    // Pre-fix: dzdx = dzdy = 0 → slope 0 deg, aspect -1, hillshade fully lit.
    // GDAL/ArcGIS emit NoData; so do we now.
    REQUIRE( slopeOut[1 * W + 1] == kNo );
    REQUIRE( aspectOut[1 * W + 1] == kNo );
    REQUIRE( hsOut[1 * W + 1] == kNo );
    // The void neighbours stay void.
    for ( int i = 0; i < W * H; ++i )
        if ( i != 1 * W + 1 )
        {
            REQUIRE( slopeOut[i] == kNo );
            REQUIRE( aspectOut[i] == kNo );
            REQUIRE( hsOut[i] == kNo );
        }
}

TEST_CASE( "PROC-A2-4: a partially-surrounded pixel keeps its one-sided gradient",
           "[swarm][proc-a2-4][terrain]" )
{
    // East (+10 over 1 cell) and south (+5 over 1 cell) are valid; west and
    // north are NoData. Both axes still have evidence, so the raster-border
    // one-sided policy must survive — only the all-missing axis is undefined.
    constexpr int W = 3, H = 3;
    std::vector<float> dem( W * H, kNo );
    dem[1 * W + 1] = 100.0f;
    dem[1 * W + 2] = 110.0f;
    dem[2 * W + 1] = 105.0f;

    std::vector<float> slopeOut( W * H, 0.0f );
    std::vector<float> aspectOut( W * H, 0.0f );
    REQUIRE( TerrainAnalysis::slope( dem.data(), slopeOut.data(), W, H, 1.0f, kNo ) );
    REQUIRE( TerrainAnalysis::aspect( dem.data(), aspectOut.data(), W, H, 1.0f, kNo ) );

    const float expected = std::atan( std::sqrt( 125.0f ) ) * 180.0f / static_cast<float>( M_PI );
    REQUIRE( slopeOut[1 * W + 1] == Approx( expected ).margin( 0.1f ) );
    // dzdx = +10 (rising east), dzdy = +5 (rising south) ->
    // aspect = atan2(-dzdx, dzdy) = atan2(-10, 5) = -63.43 deg -> 296.57 deg.
    REQUIRE( aspectOut[1 * W + 1] == Approx( 296.565f ).margin( 0.5f ) );
}

// ===========================================================================
// PROC-A2-1 — resolveFlats' epsilon must survive float32 addition at the DEM's
// ABSOLUTE elevation, not just at its relief.
// ===========================================================================

TEST_CASE( "PROC-A2-1: a high-altitude low-relief plateau actually drains "
           "(the old relief-scaled epsilon was absorbed by float32)",
           "[swarm][proc-a2-1][terrain][hydrology]" )
{
    // 4000 m plateau, 60 m relief to the single low rim cell. ulp(4000) =
    // 2.44e-4, so the OLD epsilon (relief * 2^-20 = 5.7e-5) rounded back to
    // 4000 on every raise: the flat stayed strictly flat and every interior
    // cell remained a D8 sink.
    constexpr int W = 11, H = 11;
    std::vector<float> dem( W * H, 4000.0f );
    dem[0] = 3940.0f; // the drain

    const float relief = 4000.0f - 3940.0f;
    const double oldEpsilon = std::max( relief * kTwoPowMinus20, 1e-6 );
    INFO( "old epsilon " << oldEpsilon << " vs ulp(4000) " << ulpAt( 4000.0f ) );
    REQUIRE( oldEpsilon < static_cast<double>( ulpAt( 4000.0f ) ) ); // the bug's precondition

    std::vector<float> filled( W * H, 0.0f );
    TerrainHydrology::FlatResolutionReport report;
    REQUIRE( TerrainHydrology::resolveFlats( dem.data(), filled.data(), W, H, kNo, &report ) );

    // The magnitude-aware epsilon must clear the float32 ulp at zmax.
    REQUIRE( report.epsilon > static_cast<double>( ulpAt( 4000.0f ) ) );
    REQUIRE( report.raisedCells > 0 );

    // At least one cell must have ACTUALLY moved in float32.
    bool anyRaised = false;
    for ( int i = 0; i < W * H; ++i )
        if ( filled[i] > dem[i] )
            anyRaised = true;
    REQUIRE( anyRaised );

    // The real contract: on the resolved surface no INTERIOR cell may stay a
    // D8 sink (code 0). Pre-fix every interior cell was one.
    std::vector<float> dir( W * H, 0.0f );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kNo ) );
    int interiorSinks = 0;
    for ( int y = 1; y < H - 1; ++y )
        for ( int x = 1; x < W - 1; ++x )
            if ( dir[static_cast<size_t>( y ) * W + x] == 0.0f )
                ++interiorSinks;
    INFO( "interior sinks after resolution: " << interiorSinks );
    REQUIRE( interiorSinks == 0 );

    // Monotone non-decreasing vs the input; the drain keeps its elevation.
    for ( int i = 0; i < W * H; ++i )
        REQUIRE( filled[i] >= dem[i] );
    REQUIRE( filled[0] == 3940.0f );
}

TEST_CASE( "PROC-A2-1: raisedCells counts only cells that really moved",
           "[swarm][proc-a2-1][terrain][hydrology]" )
{
    // A tiny DEM (relief 0, zmax 50): epsilon = max(50, 50) * 2^-20 = 4.8e-5,
    // far above ulp(50) = 3.8e-6, so every branch taken is a real raise and
    // the report must agree with the buffer.
    constexpr int W = 7, H = 7;
    std::vector<float> dem( W * H, 50.0f );
    dem[0] = 40.0f;

    std::vector<float> filled( W * H, 0.0f );
    TerrainHydrology::FlatResolutionReport report;
    REQUIRE( TerrainHydrology::resolveFlats( dem.data(), filled.data(), W, H, kNo, &report ) );

    long long actuallyRaised = 0;
    for ( int i = 0; i < W * H; ++i )
        if ( filled[i] > dem[i] )
            ++actuallyRaised;
    REQUIRE( report.raisedCells == actuallyRaised );
    REQUIRE( actuallyRaised > 0 );
}

// ===========================================================================
// PROC-A1-1 — computeLogRatio must not fold NoData into "no change".
// ===========================================================================

TEST_CASE( "PROC-A1-1: computeLogRatio emits NaN for non-finite inputs, "
           "not the neutral 0",
           "[swarm][proc-a1-1][change]" )
{
    using rs::processing::ChangeDetector;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const std::vector<float> t1 = { 10.0f, nan, 5.0f, inf, -3.0f };
    const std::vector<float> t2 = { 13.0f, 4.0f, inf, 2.0f, -3.0f };
    const auto lr = ChangeDetector::computeLogRatio( t1, t2 );
    REQUIRE( lr.size() == t1.size() );

    // Finite positive pair: the hand-expanded constant.
    REQUIRE( lr[0] == Approx( 0.26236195723f ).margin( 1e-6f ) );
    // Non-finite on either side: NaN, never 0 (0 == "no change").
    REQUIRE( std::isnan( lr[1] ) );
    REQUIRE( std::isnan( lr[2] ) );
    REQUIRE( std::isnan( lr[3] ) );
    // Non-positive shifted radiance is outside the operator's domain too.
    REQUIRE( std::isnan( lr[4] ) );

    // Identical finite inputs still give exactly 0 (the documented anchor).
    const std::vector<float> same = { 10.0f, 0.25f, 1000.0f };
    const auto zero = ChangeDetector::computeLogRatio( same, same );
    for ( float v : zero )
        REQUIRE( v == 0.0f );
}

// ===========================================================================
// PROC-A1-3 — computePcaDifference must void only the offending pixel.
// ===========================================================================

TEST_CASE( "PROC-A1-3: computePcaDifference voids one pixel, not the whole image",
           "[swarm][proc-a1-3][change]" )
{
    using rs::processing::ChangeDetector;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // 5 pixels, 2 bands (BSQ: band plane b at offset b*pixels).
    // Pixel 2's band-1 sample is NaN; every other sample is finite.
    const size_t pixels = 5;
    const float d0[5] = { 3.0f, -3.0f, 0.0f, 0.0f, 0.0f };
    const float d1[5] = { 0.0f, 0.0f, 1.0f, 1.0f, -1.0f };
    std::vector<float> t1( 2 * pixels, 0.0f );
    std::vector<float> t2( 2 * pixels, 0.0f );
    for ( size_t p = 0; p < pixels; ++p )
    {
        t2[p] = d0[p];
        t2[pixels + p] = d1[p];
    }
    t2[pixels + 2] = nan; // the single void sample

    const auto scores = ChangeDetector::computePcaDifference(
        t1.data(), t2.data(), static_cast<int>( pixels ), 1, 2 );
    REQUIRE( scores.size() == pixels );

    // The void pixel is NaN; the rest are finite.
    REQUIRE( std::isnan( scores[2] ) );
    for ( size_t p = 0; p < pixels; ++p )
        if ( p != 2 )
            REQUIRE( std::isfinite( scores[p] ) );

    // Oracle over the FOUR finite pixels: differences (3,0), (-3,0), (0,1),
    // (0,-1) -> mean (0,0), covariance diag(4.5, 0.5) with an exactly zero
    // off-diagonal, so the minor principal component is the y axis and the
    // score is |dy| = 0, 0, 1, 1. Including the void pixel in the statistics
    // would instead give mean (0, 0.25) and scores 0.25, 0.25, 0.75, 1.25 —
    // so this pins "statistics over the finite pixels only", not just the
    // NaN mask.
    const float expected[5] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    for ( size_t p = 0; p < pixels; ++p )
    {
        if ( p == 2 )
            continue;
        INFO( "pixel " << p << " score " << scores[p] );
        REQUIRE( scores[p] == Approx( expected[p] ).margin( 1e-5f ) );
    }

    // All-void input voids the whole product rather than dividing by zero.
    std::vector<float> allNan( 2 * pixels, nan );
    const auto none = ChangeDetector::computePcaDifference(
        allNan.data(), allNan.data(), static_cast<int>( pixels ), 1, 2 );
    REQUIRE( none.size() == pixels );
    for ( float v : none )
        REQUIRE( std::isnan( v ) );
}

// ===========================================================================
// PROC-A1-2 — ISODATA's merge step must compare THIS pass's Lloyd means.
// ===========================================================================

TEST_CASE( "PROC-A1-2: ISODATA merges on the current Lloyd means, not the "
           "previous generation's centers",
           "[swarm][proc-a1-2][classifier]" )
{
    using rs::processing::ClassifierAlgorithm;
    using rs::processing::ClassifierEngine;
    using rs::processing::ClassifierHyperparameters;

    // Two perfectly separated 1-D modes (ten samples at 0, ten at 100).
    // Starting from ONE cluster the canonical split rule fires first and
    // leaves the centers at mean +/- sigma/2 = {25, 75}. The NEXT pass's
    // Lloyd means are {0, 100} — 100 apart — while those post-split centers
    // are only 50 apart.
    //
    // With theta_C = 75 the merge must NOT fire (the modes are 100 apart),
    // and the run converges on two clusters at 0 and 100. Comparing the
    // stale post-split centers instead makes the merge fire on a pair of
    // centers that own no samples, collapsing the two modes into one.
    std::vector<float> x;
    for ( int i = 0; i < 10; ++i )
        x.push_back( 0.0f );
    for ( int i = 0; i < 10; ++i )
        x.push_back( 100.0f );

    ClassifierHyperparameters p;
    p.kClusters = 1;                 // force the split-then-merge sequence
    p.maxIterations = 100;
    p.convergenceEpsilon = 1e-6;
    p.isodataMinClusterSize = 1;
    p.isodataMaxStdDev = 40.0;       // theta_S: global sigma 50 > 40 -> split
    p.isodataMinClusterDist = 75.0;  // theta_C: 50 < 75 (stale) but 100 >= 75 (means)

    auto iso = ClassifierEngine::create( ClassifierAlgorithm::Isodata, p );
    REQUIRE( iso != nullptr );
    iso->fit( x, {}, x.size(), 1 );
    REQUIRE( iso->isTrained() );

    const auto centroids = iso->clusterCentroids();
    REQUIRE( centroids.size() == 2 );
    REQUIRE( centroids[0].size() == 1 );
    REQUIRE( centroids[1].size() == 1 );
    const double lo = std::min( centroids[0][0], centroids[1][0] );
    const double hi = std::max( centroids[0][0], centroids[1][0] );
    REQUIRE( lo == Approx( 0.0f ).margin( 1e-4f ) );
    REQUIRE( hi == Approx( 100.0f ).margin( 1e-4f ) );

    // And the partition is the true one: the first ten samples share a label,
    // the last ten share the other, and the labels differ.
    std::vector<int> first, second;
    for ( int i = 0; i < 10; ++i )
        first.push_back( iso->predictOne( std::span<const float>( x ).subspan( i, 1 ) ) );
    for ( int i = 10; i < 20; ++i )
        second.push_back( iso->predictOne( std::span<const float>( x ).subspan( i, 1 ) ) );
    REQUIRE( std::adjacent_find( first.begin(), first.end(),
                                 std::not_equal_to<int>() ) == first.end() );
    REQUIRE( std::adjacent_find( second.begin(), second.end(),
                                 std::not_equal_to<int>() ) == second.end() );
    REQUIRE( first.front() != second.front() );
}

// ===========================================================================
// PROC-A1-4 — the SVM's training-time Gram matrix is released; the decision
// path must keep working bit-for-bit.
// ===========================================================================

TEST_CASE( "PROC-A1-4: SVM still classifies after its Gram matrix is released",
           "[swarm][proc-a1-4][classifier]" )
{
    using rs::processing::ClassifierAlgorithm;
    using rs::processing::ClassifierEngine;
    using rs::processing::ClassifierHyperparameters;

    // Three well-separated 2-D modes; a one-vs-one SVM must separate them.
    std::vector<float> x;
    std::vector<int> y;
    const double means[3][2] = { { -6.0, -6.0 }, { 0.0, 6.0 }, { 6.0, -6.0 } };
    for ( int c = 0; c < 3; ++c )
        for ( int i = 0; i < 40; ++i )
        {
            // Deterministic jitter (no RNG dependency across platforms).
            const double jx = ( ( i % 7 ) - 3 ) * 0.11;
            const double jy = ( ( i % 5 ) - 2 ) * 0.13;
            x.push_back( static_cast<float>( means[c][0] + jx ) );
            x.push_back( static_cast<float>( means[c][1] + jy ) );
            y.push_back( c );
        }

    ClassifierHyperparameters p;
    p.svmC = 1.0;
    p.svmGamma = 0.05;
    auto svm = ClassifierEngine::create( ClassifierAlgorithm::SupportVectorMachine, p );
    REQUIRE( svm != nullptr );
    svm->fit( x, y, y.size(), 2 );
    REQUIRE( svm->isTrained() );

    // Self-consistency: every training sample is recovered, and the argmax of
    // predictProbabilities agrees with predictOne (the decision path is the
    // only thing left after the Gram release — if the release had broken it,
    // these would collapse).
    size_t hits = 0;
    for ( size_t i = 0; i < y.size(); ++i )
    {
        const auto feat = std::span<const float>( x ).subspan( 2 * i, 2 );
        if ( svm->predictOne( feat ) == y[i] )
            ++hits;
        const auto probs = svm->predictProbabilities( feat );
        REQUIRE( probs.size() == 3 );
        const int hot = static_cast<int>( std::distance(
            probs.begin(), std::max_element( probs.begin(), probs.end() ) ) );
        REQUIRE( svm->predictOne( feat ) == hot );
    }
    REQUIRE( static_cast<double>( hits ) / static_cast<double>( y.size() ) >= 0.95 );
}

// ===========================================================================
// PROC-A1-5 — fusion methods must refuse an all-NoData scene instead of
// publishing an all-void product as a success.
// ===========================================================================

namespace
{

/// Writes a single-band float GeoTIFF whose every sample is @p value.
QString writeConstantRaster( const QString &path, int w, int h, float value )
{
    const std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, w, h, 1, GDT_Float32, gt, QString() );
    if ( !ds )
        return {};
    std::vector<float> buf( static_cast<size_t>( w ) * h, value );
    if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, w, h, buf.data(),
                       w, h, GDT_Float32, 0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return {};
    }
    GDALClose( ds );
    return path;
}

/// Writes a multi-band float GeoTIFF whose every sample is @p value.
QString writeConstantRaster( const QString &path, int w, int h, int bands, float value )
{
    const std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, w, h, bands, GDT_Float32, gt, QString() );
    if ( !ds )
        return {};
    std::vector<float> buf( static_cast<size_t>( w ) * h, value );
    for ( int b = 1; b <= bands; ++b )
    {
        if ( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, w, h, buf.data(),
                           w, h, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            return {};
        }
    }
    GDALClose( ds );
    return path;
}

} // namespace

TEST_CASE( "PROC-A1-5: every fusion method refuses an all-NoData scene",
           "[swarm][proc-a1-5][fusion]" )
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = writeConstantRaster( dir.filePath( QStringLiteral( "pan.tif" ) ),
                                                 8, 8,
                                                 std::numeric_limits<float>::quiet_NaN() );
    const QString msPath = writeConstantRaster( dir.filePath( QStringLiteral( "ms.tif" ) ),
                                                8, 8, 3,
                                                std::numeric_limits<float>::quiet_NaN() );
    REQUIRE( !panPath.isEmpty() );
    REQUIRE( !msPath.isEmpty() );

    for ( const QString &method : { QStringLiteral( "linear" ), QStringLiteral( "brovey" ),
                                    QStringLiteral( "ihs" ), QStringLiteral( "gram_schmidt" ),
                                    QStringLiteral( "pca" ) } )
    {
        INFO( "method: " << method.toStdString() );
        const QString outPath = dir.filePath( QStringLiteral( "fused_%1.tif" ).arg( method ) );
        ImageFusion::NativeFusionParams params;
        params.method = method;
        QString error;
        const bool ok = ImageFusion::processNativeFusion( panPath, msPath, outPath, params, &error );
        // A refusal must be loud and typed, never a silent success.
        CHECK_FALSE( ok );
        CHECK_FALSE( error.isEmpty() );
        // ... and it must not leave a plausible-looking product behind.
        CHECK_FALSE( QFile::exists( outPath ) );
    }
}

TEST_CASE( "PROC-A1-5: a scene with valid pixels still fuses (no false refusal)",
           "[swarm][proc-a1-5][fusion]" )
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = writeConstantRaster( dir.filePath( QStringLiteral( "pan.tif" ) ),
                                                 8, 8, 200.0f );
    const QString msPath = writeConstantRaster( dir.filePath( QStringLiteral( "ms.tif" ) ),
                                                8, 8, 3, 100.0f );
    REQUIRE( !panPath.isEmpty() );
    REQUIRE( !msPath.isEmpty() );

    for ( const QString &method : { QStringLiteral( "linear" ), QStringLiteral( "brovey" ),
                                    QStringLiteral( "ihs" ), QStringLiteral( "gram_schmidt" ),
                                    QStringLiteral( "pca" ) } )
    {
        INFO( "method: " << method.toStdString() );
        const QString outPath = dir.filePath( QStringLiteral( "ok_%1.tif" ).arg( method ) );
        ImageFusion::NativeFusionParams params;
        params.method = method;
        QString error;
        REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outPath, params, &error ) );
        REQUIRE( error.isEmpty() );
        REQUIRE( QFile::exists( outPath ) );
    }
}

// ===========================================================================
// PROC-A1-6 — Frost's neighbour loop must skip ±Inf, not only NaN.
// ===========================================================================

TEST_CASE( "PROC-A1-6: Frost (full frame) ignores a -Inf neighbour",
           "[swarm][proc-a1-6][speckle]" )
{
    // localStats derives the window mean from an integral image that does not
    // filter non-finite samples, so a -Inf window gives mean = -Inf and
    // cvSq = 0 (mean > 0 is false) — the weights are all 1 and the output is
    // the plain average of the FINITE neighbours. Pre-fix the -Inf entered
    // sumWeighted and the output was -Inf.
    constexpr int W = 5, H = 5;
    std::vector<float> in( W * H, 100.0f );
    in[2 * W + 2] = -std::numeric_limits<float>::infinity();

    std::vector<float> out( W * H, 0.0f );
    ImageEnhancement::frostFilter( in.data(), out.data(), W, H, 5, 2.0f );

    for ( int i = 0; i < W * H; ++i )
    {
        INFO( "pixel " << i );
        REQUIRE( std::isfinite( out[i] ) );
    }
    // Interior pixels: 24 finite neighbours at 100 -> exactly 100.
    REQUIRE( out[2 * W + 2] == Approx( 100.0f ).margin( 1e-3f ) );
    REQUIRE( out[1 * W + 1] == Approx( 100.0f ).margin( 1e-3f ) );
}

TEST_CASE( "PROC-A1-6: Frost (streaming tile) ignores +Inf and -Inf neighbours",
           "[swarm][proc-a1-6][speckle]" )
{
    using ImageEnhancementStreaming::speckleTileFrost;

    constexpr int kDim = 5;
    constexpr int kHalo = 2;
    constexpr int kKernel = 5;
    constexpr int kBufW = kDim + 2 * kHalo;
    constexpr int kBufH = kDim + 2 * kHalo;

    const auto run = []( float voidValue ) {
        std::vector<float> raster( kDim * kDim, 100.0f );
        raster[2 * kDim + 2] = voidValue;
        // Halo buffer with edge-replicated fill outside the raster.
        std::vector<float> buf( kBufW * kBufH, 0.0f );
        for ( int by = 0; by < kBufH; ++by )
            for ( int bx = 0; bx < kBufW; ++bx )
            {
                const int rx = std::clamp( bx - kHalo, 0, kDim - 1 );
                const int ry = std::clamp( by - kHalo, 0, kDim - 1 );
                buf[static_cast<size_t>( by ) * kBufW + bx] =
                    raster[static_cast<size_t>( ry ) * kDim + rx];
            }
        GdalBlockStream::Tile tile;
        tile.xOffset = 0;
        tile.yOffset = 0;
        tile.width = kDim;
        tile.height = kDim;
        tile.halo = kHalo;
        tile.bufferWidth = kBufW;
        tile.bufferHeight = kBufH;
        tile.index = 0;
        tile.totalTiles = 1;
        tile.rasterWidth = kDim;
        tile.rasterHeight = kDim;

        std::vector<float> core( kDim * kDim, 0.0f );
        speckleTileFrost( tile, buf.data(), core.data(), kKernel, 2.0f );
        return core;
    };

    const float inf = std::numeric_limits<float>::infinity();
    for ( const float voidValue : { inf, -inf } )
    {
        INFO( "void value " << voidValue );
        const auto core = run( voidValue );
        for ( int i = 0; i < kDim * kDim; ++i )
        {
            INFO( "pixel " << i );
            REQUIRE( std::isfinite( core[i] ) );
        }
        REQUIRE( core[2 * kDim + 2] == Approx( 100.0f ).margin( 1e-3f ) );
    }
}

// ===========================================================================
// PROC-A1-7 — a GCP id containing commas must round-trip through CSV.
// ===========================================================================

TEST_CASE( "PROC-A1-7: GCP CSV round-trips ids containing commas and quotes",
           "[swarm][proc-a1-7][gcp]" )
{
    using rs::core::GcpManager;
    using rs::core::GcpPoint;

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "gcps.csv" ) );

    GcpManager mgr;
    const QStringList ids = {
        QStringLiteral( "plain" ),
        QStringLiteral( "road, north site" ),
        QStringLiteral( "he said \"hi\"" ),
        QStringLiteral( "a,b,\"c\"" ),
    };
    for ( int i = 0; i < ids.size(); ++i )
    {
        GcpPoint pt;
        pt.id = ids[i];
        pt.sourceX = 10.0 + i;
        pt.sourceY = 20.0 + i;
        pt.targetX = 30.0 + i;
        pt.targetY = 40.0 + i;
        REQUIRE( mgr.addPoint( pt ) );
    }
    REQUIRE( mgr.saveToCsv( path ) );

    GcpManager loaded;
    REQUIRE( loaded.loadFromCsv( path ) );
    // Pre-fix every comma-bearing id produced >9 fields and was silently
    // dropped, so only "plain" survived.
    REQUIRE( loaded.size() == ids.size() );

    const auto original = mgr.allPoints();
    const auto restored = loaded.allPoints();
    for ( size_t i = 0; i < original.size(); ++i )
    {
        INFO( "point " << i );
        REQUIRE( restored[i].id == original[i].id );
        REQUIRE( restored[i].sourceX == Approx( original[i].sourceX ).margin( 1e-12 ) );
        REQUIRE( restored[i].sourceY == Approx( original[i].sourceY ).margin( 1e-12 ) );
        REQUIRE( restored[i].targetX == Approx( original[i].targetX ).margin( 1e-12 ) );
        REQUIRE( restored[i].targetY == Approx( original[i].targetY ).margin( 1e-12 ) );
    }
}

// ===========================================================================
// PROC-A2-2 — exp_spectral's scale flag must not share a name with the
// main-namespace flag it means the opposite of.
// ===========================================================================

TEST_CASE( "PROC-A2-2: exp_spectral additive-constant indices honour "
           "isUnitReflectance",
           "[swarm][proc-a2-2][spectral]" )
{
    using exp_spectral::SpectralIndices;
    constexpr float kNoData = -9999.0f;

    // DN-scaled reflectance (x10^4) and its unit-reflectance equivalent.
    const std::vector<float> nirDn = { 5000.0f, 8000.0f };
    const std::vector<float> redDn = { 1000.0f, 2000.0f };
    const std::vector<float> blueDn = { 500.0f, 1000.0f };
    const std::vector<float> nirUnit = { 0.5f, 0.8f };
    const std::vector<float> redUnit = { 0.1f, 0.2f };
    const std::vector<float> blueUnit = { 0.05f, 0.1f };

    std::vector<float> a( 2, 0.0f );
    std::vector<float> b( 2, 0.0f );

    // EVI: DN input with isUnitReflectance == false must equal the unit input
    // with the flag true (the /10^4 guard is what makes the +1 constant
    // physically meaningful).
    REQUIRE( SpectralIndices::evi( nirDn.data(), redDn.data(), blueDn.data(),
                                   a.data(), 2, false, kNoData ) );
    REQUIRE( SpectralIndices::evi( nirUnit.data(), redUnit.data(), blueUnit.data(),
                                   b.data(), 2, true, kNoData ) );
    for ( int i = 0; i < 2; ++i )
        REQUIRE( a[i] == Approx( b[i] ).margin( 1e-6f ) );

    // SAVI, same guard.
    REQUIRE( SpectralIndices::savi( nirDn.data(), redDn.data(), a.data(), 2, 0.5f,
                                    false, kNoData ) );
    REQUIRE( SpectralIndices::savi( nirUnit.data(), redUnit.data(), b.data(), 2, 0.5f,
                                    true, kNoData ) );
    for ( int i = 0; i < 2; ++i )
        REQUIRE( a[i] == Approx( b[i] ).margin( 1e-6f ) );

    // EVI2, same guard.
    REQUIRE( SpectralIndices::evi2( nirDn.data(), redDn.data(), a.data(), 2, false, kNoData ) );
    REQUIRE( SpectralIndices::evi2( nirUnit.data(), redUnit.data(), b.data(), 2, true, kNoData ) );
    for ( int i = 0; i < 2; ++i )
        REQUIRE( a[i] == Approx( b[i] ).margin( 1e-6f ) );

    // Closed form for the unit path: EVI = 2.5*(0.8-0.2)/(0.8+6*0.2-7.5*0.1+1)
    //                             = 1.5 / (0.8+1.2-0.75+1) = 1.5/2.25 = 0.6667.
    REQUIRE( b[1] == Approx( 2.0f / 3.0f ).margin( 1e-6f ) );

    // NoData passthrough is unchanged by the rename.
    const std::vector<float> voidBand = { kNoData, 0.5f };
    REQUIRE( SpectralIndices::evi( voidBand.data(), redUnit.data(), blueUnit.data(),
                                   a.data(), 2, true, kNoData ) );
    REQUIRE( a[0] == kNoData );
    REQUIRE( std::isfinite( a[1] ) );
}

// ===========================================================================
// PROC-F-1 — the toolbox hillshade must convert a geographic DEM's degrees to
// metres before handing them to the Horn kernel (#612 family).
// ===========================================================================

TEST_CASE( "PROC-F-1: the geographic degree->metre arc the toolbox applies is "
           "the single-owner MathUtils one",
           "[swarm][proc-f-1][terrain]" )
{
    // Snyder's expansion to the 4th/3rd harmonic, as implemented in
    // MathUtils::wgs84ArcAtLatitudeDeg (the sole owner every geographic
    // surface consumes). The toolbox hillshade multiplies its pixel size by
    // exactly these factors.
    const auto arc = MathUtils::wgs84ArcAtLatitudeDeg( 40.0 );
    REQUIRE( arc.perDegLat == Approx( 111034.604 ).margin( 0.01 ) );
    REQUIRE( arc.perDegLon == Approx( 85393.937 ).margin( 0.01 ) );

    // A ~30 m SRTM-ish cell at 40 N in degrees:
    const double degPerPixel = 30.0 / arc.perDegLon;
    const double cellMetres = degPerPixel * arc.perDegLon;
    REQUIRE( cellMetres == Approx( 30.0 ).margin( 1e-9 ) );

    // And the consequence for the kernel: a DEM that rises 30 m over one
    // 30 m cell is a 45 degree slope. Feeding the kernel the raw degree size
    // (the pre-fix behaviour) instead saturates the gradient.
    constexpr int W = 5, H = 5;
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[static_cast<size_t>( r ) * W + c] = static_cast<float>( c );

    std::vector<float> metreOut( W * H, 0.0f );
    std::vector<float> degreeOut( W * H, 0.0f );
    REQUIRE( TerrainAnalysis::hillshade( dem.data(), metreOut.data(), W, H,
                                         30.0f, 30.0f, kNo, 315.0f, 45.0f ) );
    REQUIRE( TerrainAnalysis::hillshade( dem.data(), degreeOut.data(), W, H,
                                         static_cast<float>( degPerPixel ),
                                         static_cast<float>( degPerPixel ),
                                         kNo, 315.0f, 45.0f ) );

    // The degree-fed gradient is ~1e5 times too steep: the two products are
    // nowhere near each other, which is exactly the silent corruption the
    // fix removes from the toolbox surface.
    double maxDiff = 0.0;
    for ( int i = 0; i < W * H; ++i )
        maxDiff = std::max( maxDiff, static_cast<double>(
                                          std::fabs( metreOut[i] - degreeOut[i] ) ) );
    REQUIRE( maxDiff > 0.1 );
    // The metre-fed product is the physically meaningful one: an exact 45 deg
    // plane lit from azimuth 315 / elevation 45 has a computable shade.
    REQUIRE( std::isfinite( metreOut[2 * W + 2] ) );
    REQUIRE( metreOut[2 * W + 2] >= 0.0f );
    REQUIRE( metreOut[2 * W + 2] <= 1.0f );
}
