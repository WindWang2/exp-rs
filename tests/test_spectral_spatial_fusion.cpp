// test_spectral_spatial_fusion.cpp — Spectral Intelligence 12.0 work package B:
// spatial consistency fusion of spectral score planes. Closed-form fused
// values, NoData leak-proofness, tile-halo equivalence, config refusals.

#include "processing/algorithms/spectral_spatial_fusion.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace SpectralSpatialFusion;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "Fusion closed forms: constant plane, beta extremes, hand-computed mean",
           "[spectral][fusion]" )
{
    // Constant 4×4 plane: fused == the constant for any beta/radius; window
    // member counts follow the clamped windows (4/6/9).
    {
        std::vector<float> scores( 16, 3.0f );
        Result result;
        Config config; // r=1, beta=0.5
        REQUIRE( fuseScores( scores.data(), nullptr, 4, 4, config, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == Catch::Approx( 3.0f ).margin( 1e-6 ) );
        REQUIRE( result.neighborCount[0] == 4 );  // corner
        REQUIRE( result.neighborCount[1] == 6 );  // edge
        REQUIRE( result.neighborCount[static_cast<size_t>( 1 ) * 4 + 1] == 9 ); // interior
        REQUIRE( result.covered[0] == 1 );
    }

    // 3×3 ascending plane, hand-computed means.
    {
        std::vector<float> scores{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        Result result;
        Config config;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, config, &result ) );
        // Center: mean == 5 → 0.5·5 + 0.5·5 = 5.
        REQUIRE( result.fused[4] == Catch::Approx( 5.0f ).margin( 1e-6 ) );
        // Corner (0,0): mean(1,2,4,5) = 3 → 0.5·1 + 0.5·3 = 2.
        REQUIRE( result.fused[0] == Catch::Approx( 2.0f ).margin( 1e-6 ) );
        // Edge (0,1): mean(1,2,3,4,5,6) = 3.5 → 0.5·2 + 0.5·3.5 = 2.75.
        REQUIRE( result.fused[1] == Catch::Approx( 2.75f ).margin( 1e-6 ) );
    }

    // beta = 0 → exact pass-through; beta = 1 → exact local mean; radius 0 →
    // pass-through regardless of beta.
    {
        std::vector<float> scores{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        Result result;
        Config beta0;
        beta0.beta = 0.0;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, beta0, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == scores[p] );

        Config radius0;
        radius0.radius = 0;
        radius0.beta = 0.9;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, radius0, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == scores[p] );

        Config beta1;
        beta1.beta = 1.0;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, beta1, &result ) );
        // Left edge (1,0): window members 1,2,4,5,7,8 → mean 4.5.
        REQUIRE( result.fused[static_cast<size_t>( 1 ) * 3 + 0] ==
                 Catch::Approx( 4.5f ).margin( 1e-6 ) );
    }
}

TEST_CASE( "Fusion NoData contract: invalid pixels never leak into neighbors",
           "[spectral][fusion][nodata]" )
{
    // 1×3 row [NaN, 5, 7], beta = 1: pixel 1 averages only (5,7) → 6 — if the
    // NaN leaked in, the mean would be NaN. Pixel 2 averages (5,7) → 6.
    {
        std::vector<float> scores{ kNaN, 5.0f, 7.0f };
        Result result;
        Config config;
        config.beta = 1.0;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 1, config, &result ) );
        REQUIRE( std::isnan( result.fused[0] ) );
        REQUIRE( result.covered[0] == 0 );
        REQUIRE( result.neighborCount[0] == 0 );
        REQUIRE( result.fused[1] == Catch::Approx( 6.0f ).margin( 1e-6 ) );
        REQUIRE( result.neighborCount[1] == 2 );
        REQUIRE( result.fused[2] == Catch::Approx( 6.0f ).margin( 1e-6 ) );
    }

    // An explicit validity mask can invalidate even FINITE pixels, and those
    // pixels are equally excluded from every neighbor's mean.
    {
        std::vector<float> scores{ 100.0f, 5.0f, 7.0f };
        std::vector<uint8_t> valid{ 0, 1, 1 };
        Result result;
        Config config;
        config.beta = 1.0;
        REQUIRE( fuseScores( scores.data(), valid.data(), 3, 1, config, &result ) );
        REQUIRE( std::isnan( result.fused[0] ) );
        REQUIRE( result.fused[1] == Catch::Approx( 6.0f ).margin( 1e-6 ) );
        REQUIRE( result.neighborCount[1] == 2 );
    }

    // A valid pixel surrounded by NoData keeps its own score (self is a
    // window member; no zero-fill bias).
    {
        std::vector<float> scores{ kNaN, kNaN, kNaN, kNaN, 42.0f, kNaN, kNaN, kNaN, kNaN };
        Result result;
        Config config; // 3×3, r=1, beta=0.5
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, config, &result ) );
        REQUIRE( result.neighborCount[4] == 1 );
        REQUIRE( result.fused[4] == Catch::Approx( 42.0f ).margin( 1e-6 ) );
    }
}

TEST_CASE( "Fusion tile-halo equivalence: interior pixels match the whole-plane result",
           "[spectral][fusion][tile]" )
{
    constexpr int kW = 8;
    constexpr int kH = 8;
    std::vector<float> scores( static_cast<size_t>( kW ) * kH );
    for ( size_t p = 0; p < scores.size(); ++p )
        scores[p] = static_cast<float>( ( p * 37 ) % 101 ) / 10.0f;
    // Punch some NoData holes.
    scores[9] = kNaN;
    scores[static_cast<size_t>( 7 ) * 8 + 3] = kNaN;

    Config config; // r=1, beta=0.5
    Result whole;
    REQUIRE( fuseScores( scores.data(), nullptr, kW, kH, config, &whole ) );

    // 4×4 sub-tile at offset (2,3) plus a 1-pixel halo from the full plane.
    constexpr int kTW = 4;
    constexpr int kTH = 4;
    constexpr int kOx = 2;
    constexpr int kOy = 3;
    constexpr int kR = 1;
    std::vector<float> haloed( static_cast<size_t>( kTW + 2 * kR ) *
                               ( kTH + 2 * kR ) );
    for ( int y = 0; y < kTH + 2 * kR; ++y )
        for ( int x = 0; x < kTW + 2 * kR; ++x )
        {
            const int sy = kOy - kR + y;
            const int sx = kOx - kR + x;
            haloed[static_cast<size_t>( y ) * ( kTW + 2 * kR ) + x] =
                scores[static_cast<size_t>( sy ) * kW + sx];
        }
    Result tiled;
    REQUIRE( fuseScores( haloed.data(), nullptr, kTW + 2 * kR, kTH + 2 * kR, config, &tiled ) );

    for ( int y = 0; y < kTH; ++y )
        for ( int x = 0; x < kTW; ++x )
        {
            const size_t wholeIdx = static_cast<size_t>( kOy + y ) * kW + ( kOx + x );
            const size_t tileIdx =
                static_cast<size_t>( y + kR ) * ( kTW + 2 * kR ) + ( x + kR );
            INFO( "interior pixel (" << x << "," << y << ")" );
            if ( std::isnan( whole.fused[wholeIdx] ) )
                REQUIRE( std::isnan( tiled.fused[tileIdx] ) );
            else
                REQUIRE( tiled.fused[tileIdx] == whole.fused[wholeIdx] );
            REQUIRE( tiled.neighborCount[tileIdx] == whole.neighborCount[wholeIdx] );
        }
}

TEST_CASE( "Fusion config and argument refusals", "[spectral][fusion]" )
{
    std::vector<float> scores( 4, 1.0f );
    Result result;
    QString error;

    Config badRadius;
    badRadius.radius = -1;
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, badRadius, &result, &error ) );
    REQUIRE( !error.isEmpty() );

    Config badBeta;
    badBeta.beta = 1.5;
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, badBeta, &result, &error ) );
    badBeta.beta = -0.5;
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, badBeta, &result, &error ) );
    badBeta.beta = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, badBeta, &result, &error ) );

    Config ok;
    REQUIRE_FALSE( fuseScores( nullptr, nullptr, 2, 2, ok, &result, &error ) );
    REQUIRE( !error.isEmpty() );
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 0, 2, ok, &result, &error ) );
    REQUIRE( fuseScores( scores.data(), nullptr, 2, 2, ok, nullptr ) == false );
}

TEST_CASE( "rs:spectral_spatial_fuse E2E: fuses detection scores without leaking NoData",
           "[spectral][fusion][operator][e2e]" )
{
    using namespace sicnu::testing;
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 8×8 single-band score raster: rising ramp plus two NoData pixels
    // (declared -9999 and a raw NaN).
    constexpr int kW = 8;
    constexpr int kH = 8;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, static_cast<float>( y * kW + x + 1 ) );
    b.withPixel( 1, 2, 2, -9999.0f );
    b.withPixel( 1, 5, 5, std::numeric_limits<float>::quiet_NaN() );
    REQUIRE( !b.writeToDisk( dir.filePath( "scores.tif" ) ).isEmpty() );
    {
        GDALDatasetH ds = GDALOpen( dir.filePath( "scores.tif" ).toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), -9999.0 );
        GDALClose( ds );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_spatial_fuse" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = dir.filePath( "scores.tif" ).toStdString();
    params["output"] = dir.filePath( "fused.tif" ).toStdString();
    params["radius"] = 1;
    params["beta"] = 1.0; // pure valid-window mean for hand-checkable values

    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    // 64 pixels − 1 declared NoData − 1 raw NaN = 62 fused.
    REQUIRE( result["fusedPixels"].asUInt64() == 62 );
    REQUIRE( result["invalidPixels"].asUInt64() == 2 );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "fused.tif" ) ) );
    std::vector<float> fused( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, fused.data(), kW, kH ) );

    // Pixel (0,0): window {1,2,9,10} (row-major 1-based values) → mean 5.5.
    REQUIRE( fused[0] == Catch::Approx( 5.5f ).margin( 1e-4 ) );
    // Declared-NoData pixel (2,2) stays NoData in the output.
    REQUIRE( std::isnan( fused[static_cast<size_t>( 2 ) * kW + 2] ) );
    // Neighbor (3,2) of the NoData pixel renormalizes over valid members only:
    // the window of (row 2, col 3) covers rows 1..3, cols 2..4 → values
    // 11,12,13,19(NoData),20,21,27,28,29 → mean(11,12,13,20,21,27,28,29)
    // = 161/8 = 20.125 (the NaN at (5,5) is outside this window).
    REQUIRE( fused[static_cast<size_t>( 2 ) * kW + 3] ==
             Catch::Approx( 20.125f ).margin( 1e-3 ) );
}

// ---------------------------------------------------------------------------
// Spectral Intelligence 13.0: edge-preserving bilateral aggregate.
// ---------------------------------------------------------------------------

TEST_CASE( "Bilateral fusion preserves a step edge that the mean smears",
           "[spectral][fusion][bilateral]" )
{
    // 8×8 plane: left half (x < 4) scores 0, right half scores 100, one
    // NoData hole at (4,4). With r = 1 the spatial sigma is 0.5 and the range
    // sigma 1, so a 100-apart neighbor carries weight exp(-100^2/2) = 0 —
    // the aggregate on the left side of the edge is exactly 0 and the fused
    // value is exactly 0, while the mean version smears it to 400/9.
    constexpr int kW = 8;
    constexpr int kH = 8;
    std::vector<float> scores( static_cast<size_t>( kW ) * kH, 0.0f );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 4; x < kW; ++x )
            scores[static_cast<size_t>( y ) * kW + x] = 100.0f;
    scores[static_cast<size_t>( 4 ) * kW + 4] = kNaN; // NoData hole on the edge

    Config meanConfig; // r=1, beta=0.5, mean
    Config bilateral;
    bilateral.method = Method::Bilateral;
    bilateral.sigmaRange = 1.0;

    Result meanResult;
    Result bilateralResult;
    REQUIRE( fuseScores( scores.data(), nullptr, kW, kH, meanConfig, &meanResult ) );
    REQUIRE( fuseScores( scores.data(), nullptr, kW, kH, bilateral, &bilateralResult ) );

    // Interior of each region: both methods keep the constant exactly.
    REQUIRE( bilateralResult.fused[static_cast<size_t>( 1 ) * kW + 1] ==
             Catch::Approx( 0.0f ).margin( 1e-6 ) );
    REQUIRE( bilateralResult.fused[static_cast<size_t>( 1 ) * kW + 6] ==
             Catch::Approx( 100.0f ).margin( 1e-6 ) );
    REQUIRE( meanResult.fused[static_cast<size_t>( 1 ) * kW + 1] ==
             Catch::Approx( 0.0f ).margin( 1e-6 ) );

    // Pixel (3,1) sits left of the edge: mean over {0,0,0,0,0,100,100,100,100}
    // = 400/9 ≈ 44.44; bilateral excludes the 100-neighbors (weight 0) → 0.
    const size_t edgePixel = static_cast<size_t>( 1 ) * kW + 3;
    REQUIRE( meanResult.fused[edgePixel] == Catch::Approx( 400.0 / 9.0 ).margin( 1e-4 ) );
    REQUIRE( bilateralResult.fused[edgePixel] == Catch::Approx( 0.0f ).margin( 1e-6 ) );
    // The edge is preserved, not smeared: the two methods differ by more than
    // the whole step height times beta.
    REQUIRE( std::fabs( bilateralResult.fused[edgePixel] - meanResult.fused[edgePixel] ) > 10.0 );

    // Symmetrically on the right side of the edge: pixel (4,1) → bilateral 100.
    const size_t edgePixelRight = static_cast<size_t>( 1 ) * kW + 4;
    REQUIRE( meanResult.fused[edgePixelRight] ==
             Catch::Approx( 500.0 / 9.0 ).margin( 1e-4 ) );
    REQUIRE( bilateralResult.fused[edgePixelRight] ==
             Catch::Approx( 100.0f ).margin( 1e-6 ) );

    // NoData hole stays NaN and is not smeared into its neighbors.
    REQUIRE( std::isnan( bilateralResult.fused[static_cast<size_t>( 4 ) * kW + 4] ) );
    REQUIRE( bilateralResult.covered[static_cast<size_t>( 4 ) * kW + 4] == 0 );
    REQUIRE( bilateralResult.neighborCount[static_cast<size_t>( 3 ) * kW + 4] == 8 );
}

TEST_CASE( "Bilateral fusion: constant plane, identities and sigma extremes",
           "[spectral][fusion][bilateral]" )
{
    // Constant plane: range weights are all 1, so the aggregate is the
    // constant regardless of beta/radius.
    {
        std::vector<float> scores( 16, 2.5f );
        Config config;
        config.method = Method::Bilateral;
        config.sigmaRange = 0.75;
        Result result;
        REQUIRE( fuseScores( scores.data(), nullptr, 4, 4, config, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == Catch::Approx( 2.5f ).margin( 1e-6 ) );
    }

    // Identities: beta = 0 is an exact pass-through; radius = 0 is an exact
    // pass-through for the bilateral method too (single-member window).
    {
        std::vector<float> scores{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        Config beta0;
        beta0.method = Method::Bilateral;
        beta0.beta = 0.0;
        Result result;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, beta0, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == scores[p] );

        Config radius0;
        radius0.method = Method::Bilateral;
        radius0.radius = 0;
        radius0.beta = 0.9;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 3, radius0, &result ) );
        for ( size_t p = 0; p < scores.size(); ++p )
            REQUIRE( result.fused[p] == scores[p] );
    }

    // sigmaRange → 0: only the self member survives → aggregate == self.
    // sigmaRange → large: the range weights vanish → the plain mean.
    {
        std::vector<float> scores{ kNaN, 5.0f, 7.0f };
        Config tiny;
        tiny.method = Method::Bilateral;
        tiny.beta = 1.0;
        tiny.sigmaRange = 1e-9;
        Result result;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 1, tiny, &result ) );
        REQUIRE( std::isnan( result.fused[0] ) );
        REQUIRE( result.fused[1] == Catch::Approx( 5.0f ).margin( 1e-6 ) );
        REQUIRE( result.fused[2] == Catch::Approx( 7.0f ).margin( 1e-6 ) );

        Config wide;
        wide.method = Method::Bilateral;
        wide.beta = 1.0;
        wide.sigmaRange = 1e6;
        REQUIRE( fuseScores( scores.data(), nullptr, 3, 1, wide, &result ) );
        REQUIRE( result.fused[1] == Catch::Approx( 6.0f ).margin( 1e-4 ) );
        REQUIRE( result.fused[2] == Catch::Approx( 6.0f ).margin( 1e-4 ) );
    }
}

TEST_CASE( "Bilateral fusion tile-halo equivalence: interior matches whole-plane "
           "bit-exactly",
           "[spectral][fusion][bilateral][tile]" )
{
    constexpr int kW = 8;
    constexpr int kH = 8;
    std::vector<float> scores( static_cast<size_t>( kW ) * kH );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            scores[static_cast<size_t>( y ) * kW + x] =
                ( x < 4 ) ? 1.0f : 90.0f; // step edge
    scores[9] = kNaN;
    scores[static_cast<size_t>( 7 ) * 8 + 3] = kNaN;

    Config config;
    config.method = Method::Bilateral;
    config.sigmaRange = 2.0;
    config.radius = 2;
    Result whole;
    REQUIRE( fuseScores( scores.data(), nullptr, kW, kH, config, &whole ) );

    // 4×4 sub-tile at offset (2,3) plus a 2-pixel halo.
    constexpr int kTW = 4;
    constexpr int kTH = 4;
    constexpr int kOx = 2;
    constexpr int kOy = 3;
    constexpr int kR = 2;
    std::vector<float> haloed( static_cast<size_t>( kTW + 2 * kR ) *
                               ( kTH + 2 * kR ) );
    for ( int y = 0; y < kTH + 2 * kR; ++y )
        for ( int x = 0; x < kTW + 2 * kR; ++x )
        {
            const int sy = kOy - kR + y;
            const int sx = kOx - kR + x;
            haloed[static_cast<size_t>( y ) * ( kTW + 2 * kR ) + x] =
                scores[static_cast<size_t>( sy ) * kW + sx];
        }
    Result tiled;
    REQUIRE( fuseScores( haloed.data(), nullptr, kTW + 2 * kR, kTH + 2 * kR, config,
                         &tiled ) );

    for ( int y = 0; y < kTH; ++y )
        for ( int x = 0; x < kTW; ++x )
        {
            const size_t wholeIdx = static_cast<size_t>( kOy + y ) * kW + ( kOx + x );
            const size_t tileIdx =
                static_cast<size_t>( y + kR ) * ( kTW + 2 * kR ) + ( x + kR );
            INFO( "interior pixel (" << x << "," << y << ")" );
            if ( std::isnan( whole.fused[wholeIdx] ) )
                REQUIRE( std::isnan( tiled.fused[tileIdx] ) );
            else
                REQUIRE( tiled.fused[tileIdx] == whole.fused[wholeIdx] );
            REQUIRE( tiled.neighborCount[tileIdx] == whole.neighborCount[wholeIdx] );
        }
}

TEST_CASE( "Bilateral fusion refuses invalid sigmaRange", "[spectral][fusion][bilateral]" )
{
    std::vector<float> scores( 4, 1.0f );
    Result result;
    QString error;

    Config zero;
    zero.method = Method::Bilateral;
    zero.sigmaRange = 0.0;
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, zero, &result, &error ) );
    REQUIRE( error.contains( QStringLiteral( "sigmaRange" ) ) );

    Config negative;
    negative.method = Method::Bilateral;
    negative.sigmaRange = -1.0;
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, negative, &result, &error ) );

    Config nan;
    nan.method = Method::Bilateral;
    nan.sigmaRange = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE( fuseScores( scores.data(), nullptr, 2, 2, nan, &result, &error ) );

    // The mean method does not care about sigmaRange (ignored, not refused).
    Config meanIgnore;
    meanIgnore.sigmaRange = -5.0;
    REQUIRE( fuseScores( scores.data(), nullptr, 2, 2, meanIgnore, &result ) );
}

TEST_CASE( "rs:spectral_spatial_fuse E2E: bilateral method, streaming and method "
           "refusal",
           "[spectral][fusion][bilateral][operator][e2e]" )
{
    using namespace sicnu::testing;
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 300×300 single-band plane with a vertical step edge and two NoData
    // pixels — wide enough to span multiple 256-tiles, so the operator's halo
    // streaming is exercised against the kernel oracle.
    constexpr int kW = 300;
    constexpr int kH = 300;
    RsSyntheticRasterBuilder b( kW, kH, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            b.withPixel( 1, x, y, ( x < 150 ) ? 0.0f : 100.0f );
    b.withPixel( 1, 10, 10, -9999.0f );
    b.withPixel( 1, 290, 290, std::numeric_limits<float>::quiet_NaN() );
    const QString inputPath = b.writeToDisk( dir.filePath( "edge_scores.tif" ) );
    REQUIRE( !inputPath.isEmpty() );
    {
        GDALDatasetH ds = GDALOpen( inputPath.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), -9999.0 );
        GDALClose( ds );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_spatial_fuse" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = dir.filePath( "edge_bilateral.tif" ).toStdString();
    params["radius"] = 2;
    params["beta"] = 0.5;
    params["method"] = "bilateral";
    params["sigmaRange"] = 1.0;

    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    REQUIRE( result["method"].asString() == "bilateral" );
    REQUIRE( result["sigmaRange"].asDouble() == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( result["fusedPixels"].asUInt64() ==
             static_cast<Json::UInt64>( kW ) * kH - 2 );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "edge_bilateral.tif" ) ) );
    std::vector<float> fused( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandData( 1, fused.data(), kW, kH ) );

    // Kernel oracle over the same plane (whole-frame).
    std::vector<float> plane( static_cast<size_t>( kW ) * kH );
    std::vector<uint8_t> valid( static_cast<size_t>( kW ) * kH, 1 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            plane[static_cast<size_t>( y ) * kW + x] = ( x < 150 ) ? 0.0f : 100.0f;
    plane[static_cast<size_t>( 10 ) * kW + 10] = kNaN;
    valid[static_cast<size_t>( 10 ) * kW + 10] = 0;
    plane[static_cast<size_t>( 290 ) * kW + 290] = kNaN;
    valid[static_cast<size_t>( 290 ) * kW + 290] = 0;
    Config config;
    config.radius = 2;
    config.beta = 0.5;
    config.method = Method::Bilateral;
    config.sigmaRange = 1.0;
    Result oracle;
    REQUIRE( fuseScores( plane.data(), valid.data(), kW, kH, config, &oracle ) );

    size_t mismatches = 0;
    for ( size_t p = 0; p < plane.size(); ++p )
    {
        const bool oracleNaN = std::isnan( oracle.fused[p] );
        if ( oracleNaN != std::isnan( fused[p] ) ||
             ( !oracleNaN && fused[p] != oracle.fused[p] ) )
            ++mismatches;
    }
    // The operator's halo streaming is bit-exact against the whole-frame
    // kernel (pure window function, identical window iteration order).
    REQUIRE( mismatches == 0 );

    // The edge is preserved: a pixel two columns left of the edge fuses to
    // ~0 and its mean-window counterpart would smear toward the step.
    CHECK( fused[static_cast<size_t>( 150 ) * kW + 148] ==
           Catch::Approx( 0.0f ).margin( 1e-4 ) );
    CHECK( fused[static_cast<size_t>( 150 ) * kW + 152] ==
           Catch::Approx( 100.0f ).margin( 1e-4 ) );

    // Unknown method is a typed refusal.
    Json::Value badParams = params;
    badParams["method"] = "guided";
    badParams["output"] = dir.filePath( "bad.tif" ).toStdString();
    REQUIRE_THROWS_AS( op->run( badParams, context ), RSOperatorError );

    // sigmaRange <= 0 on the bilateral path is a typed refusal.
    Json::Value badSigma = params;
    badSigma["sigmaRange"] = 0.0;
    badSigma["output"] = dir.filePath( "bad_sigma.tif" ).toStdString();
    REQUIRE_THROWS_AS( op->run( badSigma, context ), RSOperatorError );

    // Default (no method) stays the mean and reports it.
    Json::Value defaultParams( Json::objectValue );
    defaultParams["input"] = inputPath.toStdString();
    defaultParams["output"] = dir.filePath( "default.tif" ).toStdString();
    Json::Value defaultResult;
    REQUIRE_NOTHROW( defaultResult = op->run( defaultParams, context ) );
    REQUIRE( defaultResult["method"].asString() == "mean" );
    REQUIRE( !defaultResult.isMember( "sigmaRange" ) );
}
