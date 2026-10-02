// tests/test_challenger_m3_1_harness.cpp
// Empirical Challenger Stress Harness for Milestone 3 Classic Operator Migration & Bit-Exact Parity
// Authored by challenger_m3_1

#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "operators/rs/rs_change_streaming.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "data/data_manager.h"
#include "raster_bit_compare.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace sicnu::operators;

#define HARNESS_ASSERT( cond, msg )                                                    \
    do                                                                                 \
    {                                                                                  \
        if ( !( cond ) )                                                               \
        {                                                                              \
            std::cerr << "[-] HARNESS ASSERTION FAILED: " << msg << "\n    at "        \
                      << __FILE__ << ":" << __LINE__ << " (" #cond ")" << std::endl;   \
            std::exit( 1 );                                                            \
        }                                                                              \
    } while ( 0 )

namespace {

std::string writeTestRaster( const QString &path, int width, int height,
                             const std::vector<std::vector<float>> &bands,
                             std::optional<double> nodata = std::nullopt,
                             const std::map<std::string, std::string> &meta = {} )
{
    ensureGdalInit();
    std::array<double, 6> geoTransform = { 0, 1, 0, 0, 0, -1 };
    QString error;
    if ( !writeGdalOutput( path, width, height, bands, geoTransform, "EPSG:4326", &error, nodata ) )
    {
        return error.toStdString();
    }
    if ( !meta.empty() )
    {
        GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
        if ( !ds ) return "failed to open raster for metadata update";
        for ( const auto &[k, v] : meta )
        {
            GDALSetMetadataItem( ds, k.c_str(), v.c_str(), nullptr );
        }
        GDALClose( ds );
    }
    return {};
}

void writeReferenceTiff( const QString &path, int width, int height,
                         const float *data, const std::array<double, 6> &gt,
                         const QString &proj,
                         double nodata = std::numeric_limits<double>::quiet_NaN() )
{
    QString err;
    GDALDatasetH ds = createOutputTiff( path, width, height, 1,
                                        static_cast<int>( GDT_Float32 ), gt, proj, &err );
    HARNESS_ASSERT( ds != nullptr, "Failed to create reference TIFF: " + err.toStdString() );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    HARNESS_ASSERT( GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                  const_cast<float *>( data ), width, height, GDT_Float32, 0, 0 ) == CE_None,
                    "Failed to write reference TIFF data" );
    GDALSetRasterNoDataValue( band, nodata );
    GDALClose( ds );
}

// ---------------------------------------------------------------------------
// Suite 1: Operator Parity & Bit-Exact Identity Oracle across all 5 operators
// ---------------------------------------------------------------------------
void testSuite1_OperatorParityBitExact()
{
    std::cout << "[*] Running Suite 1: Operator Parity & Bit-Exact Identity Oracle (NDVI, NDWI, EVI, SAVI, Diff)..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 600;
    constexpr int H = 600; // 3x3 tiles
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    // 1. NDVI & NDWI
    {
        const QString inPath = tmp.path() + "/s1_spectral.tif";
        const QString ndviOut = tmp.path() + "/s1_ndvi.tif";
        const QString ndviAnchor = tmp.path() + "/s1_ndvi_anchor.tif";
        const QString ndwiOut = tmp.path() + "/s1_ndwi.tif";
        const QString ndwiAnchor = tmp.path() + "/s1_ndwi_anchor.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( totalPixels );
        for ( size_t i = 0; i < totalPixels; ++i )
        {
            bands[0][i] = 10.0f + static_cast<float>( i % 19 );       // Blue
            bands[1][i] = 20.0f + static_cast<float>( ( i * 3 ) % 23 ); // Green
            bands[2][i] = 15.0f + static_cast<float>( ( i * 7 ) % 29 ); // Red
            bands[3][i] = 45.0f + static_cast<float>( ( i * 11 ) % 31 ); // NIR
        }
        HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Failed to write s1 test raster" );

        // NDVI Anchor: (NIR - Red) / (NIR + Red)
        std::vector<float> ndviExp( totalPixels );
        HARNESS_ASSERT( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(), ndviExp.data(), totalPixels ), "NDVI calc failed" );
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( ndviAnchor, W, H, ndviExp.data(), gt, "EPSG:4326" );

        auto opNdvi = RSOperatorRegistry::instance().create( "rs:ndvi" );
        HARNESS_ASSERT( opNdvi != nullptr, "rs:ndvi must be registered" );
        Json::Value pNdvi( Json::objectValue );
        pNdvi["input"] = inPath.toStdString();
        pNdvi["output"] = ndviOut.toStdString();
        pNdvi["nir"] = 4;
        pNdvi["red"] = 3;
        RSOperatorContext ctxNdvi;
        opNdvi->run( pNdvi, ctxNdvi );

        auto repNdvi = sicnu::testing::compareRastersBitExact( ndviOut.toStdString(), ndviAnchor.toStdString() );
        HARNESS_ASSERT( repNdvi.identical, "NDVI must be bit-exact: " + repNdvi.detail );

        // NDWI Anchor: (Green - NIR) / (Green + NIR)
        std::vector<float> ndwiExp( totalPixels );
        HARNESS_ASSERT( MathUtils::normalizedDifference( bands[1].data(), bands[3].data(), ndwiExp.data(), totalPixels ), "NDWI calc failed" );
        writeReferenceTiff( ndwiAnchor, W, H, ndwiExp.data(), gt, "EPSG:4326" );

        auto opNdwi = RSOperatorRegistry::instance().create( "rs:ndwi" );
        HARNESS_ASSERT( opNdwi != nullptr, "rs:ndwi must be registered" );
        Json::Value pNdwi( Json::objectValue );
        pNdwi["input"] = inPath.toStdString();
        pNdwi["output"] = ndwiOut.toStdString();
        pNdwi["green"] = 2;
        pNdwi["nir"] = 4;
        RSOperatorContext ctxNdwi;
        opNdwi->run( pNdwi, ctxNdwi );

        auto repNdwi = sicnu::testing::compareRastersBitExact( ndwiOut.toStdString(), ndwiAnchor.toStdString() );
        HARNESS_ASSERT( repNdwi.identical, "NDWI must be bit-exact: " + repNdwi.detail );
    }

    // 2. EVI & SAVI (unit reflectance)
    {
        const QString inPath = tmp.path() + "/s1_evi_savi.tif";
        const QString eviOut = tmp.path() + "/s1_evi.tif";
        const QString eviAnchor = tmp.path() + "/s1_evi_anchor.tif";
        const QString saviOut = tmp.path() + "/s1_savi.tif";
        const QString saviAnchor = tmp.path() + "/s1_savi_anchor.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( totalPixels );
        for ( size_t i = 0; i < totalPixels; ++i )
        {
            bands[0][i] = 0.05f + static_cast<float>( i % 13 ) * 0.01f; // Blue
            bands[2][i] = 0.10f + static_cast<float>( i % 17 ) * 0.01f; // Red
            bands[3][i] = 0.40f + static_cast<float>( i % 23 ) * 0.01f; // NIR
        }
        HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Failed to write s1 evi/savi raster" );

        std::vector<float> eviExp( totalPixels );
        HARNESS_ASSERT( SpectralIndices::eviUnit( bands[3].data(), bands[2].data(), bands[0].data(), eviExp.data(), totalPixels ), "EVI unit failed" );
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( eviAnchor, W, H, eviExp.data(), gt, "EPSG:4326" );

        auto opEvi = RSOperatorRegistry::instance().create( "rs:evi" );
        HARNESS_ASSERT( opEvi != nullptr, "rs:evi must be registered" );
        Json::Value pEvi( Json::objectValue );
        pEvi["input"] = inPath.toStdString();
        pEvi["output"] = eviOut.toStdString();
        pEvi["nir"] = 4;
        pEvi["red"] = 3;
        pEvi["blue"] = 1;
        RSOperatorContext ctxEvi;
        opEvi->run( pEvi, ctxEvi );

        auto repEvi = sicnu::testing::compareRastersBitExact( eviOut.toStdString(), eviAnchor.toStdString() );
        HARNESS_ASSERT( repEvi.identical, "EVI must be bit-exact: " + repEvi.detail );

        std::vector<float> saviExp( totalPixels );
        HARNESS_ASSERT( SpectralIndices::saviUnit( bands[3].data(), bands[2].data(), saviExp.data(), totalPixels ), "SAVI unit failed" );
        writeReferenceTiff( saviAnchor, W, H, saviExp.data(), gt, "EPSG:4326" );

        auto opSavi = RSOperatorRegistry::instance().create( "rs:savi" );
        HARNESS_ASSERT( opSavi != nullptr, "rs:savi must be registered" );
        Json::Value pSavi( Json::objectValue );
        pSavi["input"] = inPath.toStdString();
        pSavi["output"] = saviOut.toStdString();
        pSavi["nir"] = 4;
        pSavi["red"] = 3;
        RSOperatorContext ctxSavi;
        opSavi->run( pSavi, ctxSavi );

        auto repSavi = sicnu::testing::compareRastersBitExact( saviOut.toStdString(), saviAnchor.toStdString() );
        HARNESS_ASSERT( repSavi.identical, "SAVI must be bit-exact: " + repSavi.detail );
    }

    // 3. Change Difference
    {
        const QString before = tmp.path() + "/s1_before.tif";
        const QString after = tmp.path() + "/s1_after.tif";
        const QString diffOut = tmp.path() + "/s1_diff.tif";
        const QString diffAnchor = tmp.path() + "/s1_diff_anchor.tif";

        std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
        bBands[0].resize( totalPixels );
        aBands[0].resize( totalPixels );
        for ( size_t i = 0; i < totalPixels; ++i )
        {
            bBands[0][i] = 100.0f + static_cast<float>( i % 31 );
            aBands[0][i] = 110.0f + static_cast<float>( ( i * 7 ) % 37 );
        }
        HARNESS_ASSERT( writeTestRaster( before, W, H, bBands ).empty(), "Write before failed" );
        HARNESS_ASSERT( writeTestRaster( after, W, H, aBands ).empty(), "Write after failed" );

        std::vector<float> diffExp( totalPixels );
        for ( size_t i = 0; i < totalPixels; ++i )
            diffExp[i] = aBands[0][i] - bBands[0][i];
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( diffAnchor, W, H, diffExp.data(), gt, "EPSG:4326" );

        auto opDiff = RSOperatorRegistry::instance().create( "rs:change_difference" );
        HARNESS_ASSERT( opDiff != nullptr, "rs:change_difference must be registered" );
        Json::Value pDiff( Json::objectValue );
        pDiff["before"] = before.toStdString();
        pDiff["after"] = after.toStdString();
        pDiff["output"] = diffOut.toStdString();
        RSOperatorContext ctxDiff;
        opDiff->run( pDiff, ctxDiff );

        auto repDiff = sicnu::testing::compareRastersBitExact( diffOut.toStdString(), diffAnchor.toStdString() );
        HARNESS_ASSERT( repDiff.identical, "Change difference must be bit-exact: " + repDiff.detail );
    }

    std::cout << "  -> Suite 1 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 2: Extreme & Asymmetric Edge Tile Dimension Stress Oracle
// ---------------------------------------------------------------------------
void testSuite2_EdgeTileDimensionStress()
{
    std::cout << "[*] Running Suite 2: Extreme & Asymmetric Edge Tile Dimension Stress Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    struct DimTestCase {
        int w;
        int h;
        const char *desc;
    };

    const std::vector<DimTestCase> testCases = {
        { 1, 1, "1x1 (Single pixel minimal)" },
        { 256, 256, "256x256 (Exact single full tile)" },
        { 257, 257, "257x257 (4 tiles with 1-pixel trailing margins)" },
        { 19, 555, "19x555 (Tall and skinny 1x3 tiles)" },
        { 678, 13, "678x13 (Wide and flat 3x1 tiles)" },
        { 513, 513, "513x513 (3x3 tiles with 1-pixel margin in both axes)" }
    };

    for ( size_t tc = 0; tc < testCases.size(); ++tc )
    {
        const auto &testCase = testCases[tc];
        const int W = testCase.w;
        const int H = testCase.h;
        const size_t n = static_cast<size_t>( W ) * H;

        const QString inPath = tmp.path() + QString( "/s2_in_%1.tif" ).arg( tc );
        const QString outPath = tmp.path() + QString( "/s2_out_%1.tif" ).arg( tc );
        const QString anchorPath = tmp.path() + QString( "/s2_anchor_%1.tif" ).arg( tc );

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( n );
        for ( size_t i = 0; i < n; ++i )
        {
            bands[2][i] = 12.0f + static_cast<float>( i % 13 ); // Red
            bands[3][i] = 40.0f + static_cast<float>( ( i * 3 ) % 17 ); // NIR
        }
        HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write failed for " + std::string( testCase.desc ) );

        std::vector<float> expected( n );
        HARNESS_ASSERT( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(), expected.data(), n ), "NormDiff failed" );
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorPath, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = outPath.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        RSOperatorContext ctx;
        op->run( params, ctx );

        auto rep = sicnu::testing::compareRastersBitExact( outPath.toStdString(), anchorPath.toStdString() );
        HARNESS_ASSERT( rep.identical, "Edge tile failed on " + std::string( testCase.desc ) + ": " + rep.detail );
    }

    std::cout << "  -> Suite 2 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 3: Halo=0 Boundary Seam Continuity & Pixel-Exact Alignment Oracle
// ---------------------------------------------------------------------------
void testSuite3_HaloZeroBoundaryContinuity()
{
    std::cout << "[*] Running Suite 3: Halo=0 Boundary Seam Continuity & Pixel-Exact Alignment Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 512;
    constexpr int H = 512; // 2x2 tiles
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s3_seam_in.tif";
    const QString chunkedOut = tmp.path() + "/s3_seam_chunked.tif";
    const QString anchorOut = tmp.path() + "/s3_seam_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );

    for ( int y = 0; y < H; ++y )
    {
        for ( int x = 0; x < W; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * W + x;
            // High gradient across seam boundaries (x=255/256, y=255/256)
            bands[2][i] = 10.0f + static_cast<float>( x % 256 ) * 0.1f + static_cast<float>( y % 256 ) * 0.05f; // Red
            bands[3][i] = 50.0f + static_cast<float>( ( 255 - x % 256 ) ) * 0.1f + static_cast<float>( ( 255 - y % 256 ) ) * 0.05f; // NIR
        }
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s3 raster failed" );

    std::vector<float> expected( totalPixels );
    HARNESS_ASSERT( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(), expected.data(), totalPixels ), "NormDiff failed" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = chunkedOut.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    op->run( params, ctx );

    // 1. Full raster bit-exact comparison
    auto rep = sicnu::testing::compareRastersBitExact( chunkedOut.toStdString(), anchorOut.toStdString() );
    HARNESS_ASSERT( rep.identical, "Full raster must be bit-exact across seams: " + rep.detail );

    // 2. Explicit pixel value verification directly on tile seam boundaries
    GdalDatasetWrapper outDs;
    HARNESS_ASSERT( outDs.open( chunkedOut ), "Failed to open chunked output for seam inspection" );

    std::vector<float> seamBuf( W );
    // Inspect horizontal seam row 255 (tile row 0) and row 256 (tile row 1)
    HARNESS_ASSERT( outDs.readBandWindow( 1, 0, 255, W, 1, seamBuf.data() ), "Read row 255 failed" );
    for ( int x = 0; x < W; ++x )
    {
        const size_t idx = 255 * W + x;
        HARNESS_ASSERT( seamBuf[x] == expected[idx], "Row 255 pixel at x=" + std::to_string( x ) + " differs" );
    }

    HARNESS_ASSERT( outDs.readBandWindow( 1, 0, 256, W, 1, seamBuf.data() ), "Read row 256 failed" );
    for ( int x = 0; x < W; ++x )
    {
        const size_t idx = 256 * W + x;
        HARNESS_ASSERT( seamBuf[x] == expected[idx], "Row 256 pixel at x=" + std::to_string( x ) + " differs" );
    }

    // Inspect vertical seam col 255 (tile col 0) and col 256 (tile col 1)
    std::vector<float> colBuf( H );
    HARNESS_ASSERT( outDs.readBandWindow( 1, 255, 0, 1, H, colBuf.data() ), "Read col 255 failed" );
    for ( int y = 0; y < H; ++y )
    {
        const size_t idx = y * W + 255;
        HARNESS_ASSERT( colBuf[y] == expected[idx], "Col 255 pixel at y=" + std::to_string( y ) + " differs" );
    }

    HARNESS_ASSERT( outDs.readBandWindow( 1, 256, 0, 1, H, colBuf.data() ), "Read col 256 failed" );
    for ( int y = 0; y < H; ++y )
    {
        const size_t idx = y * W + 256;
        HARNESS_ASSERT( colBuf[y] == expected[idx], "Col 256 pixel at y=" + std::to_string( y ) + " differs" );
    }

    std::cout << "  -> Suite 3 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 4: Scale Probe Invariant (#801) Seam Artifact Challenge Oracle
// ---------------------------------------------------------------------------
void testSuite4_ScaleProbeInvariant801()
{
    std::cout << "[*] Running Suite 4: Scale Probe Invariant (#801) Seam Artifact Challenge Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 512;
    constexpr int H = 512; // 2x2 tiles
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    // Challenge case: Tile row 0 (y < 256) contains small values (< 100),
    // tile row 1 (y >= 256) contains large values (4000..8000).
    // A naive per-tile scale probe would produce unit_reflectance on top and dn_scale on bottom!
    // The dataset-level scale probe must classify the whole scene as dn_scale.
    const QString inPath = tmp.path() + "/s4_scale_split.tif";
    const QString eviOut = tmp.path() + "/s4_evi.tif";
    const QString eviAnchor = tmp.path() + "/s4_evi_anchor.tif";
    const QString saviOut = tmp.path() + "/s4_savi.tif";
    const QString saviAnchor = tmp.path() + "/s4_savi_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );

    for ( int y = 0; y < H; ++y )
    {
        for ( int x = 0; x < W; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * W + x;
            if ( y < 256 )
            {
                bands[0][i] = 10.0f + static_cast<float>( ( x + y ) % 5 );  // Blue
                bands[2][i] = 20.0f + static_cast<float>( ( x * 2 ) % 7 );  // Red
                bands[3][i] = 50.0f + static_cast<float>( ( y * 3 ) % 11 ); // NIR
            }
            else
            {
                bands[0][i] = 1000.0f + static_cast<float>( ( x + y ) % 50 );
                bands[2][i] = 2000.0f + static_cast<float>( ( x * 2 ) % 70 );
                bands[3][i] = 5000.0f + static_cast<float>( ( y * 3 ) % 110 );
            }
        }
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s4 raster failed" );

    // Whole-scene scale divisor is 10000.0f because max > 1000
    std::vector<float> nirNorm( totalPixels ), redNorm( totalPixels ), blueNorm( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        nirNorm[i] = bands[3][i] * 0.0001f;
        redNorm[i] = bands[2][i] * 0.0001f;
        blueNorm[i] = bands[0][i] * 0.0001f;
    }

    std::vector<float> eviExp( totalPixels );
    HARNESS_ASSERT( SpectralIndices::eviUnit( nirNorm.data(), redNorm.data(), blueNorm.data(), eviExp.data(), totalPixels ), "EVI calc failed" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( eviAnchor, W, H, eviExp.data(), gt, "EPSG:4326" );

    std::vector<float> saviExp( totalPixels );
    HARNESS_ASSERT( SpectralIndices::saviUnit( nirNorm.data(), redNorm.data(), saviExp.data(), totalPixels ), "SAVI calc failed" );
    writeReferenceTiff( saviAnchor, W, H, saviExp.data(), gt, "EPSG:4326" );

    // Run chunked EVI
    auto opEvi = RSOperatorRegistry::instance().create( "rs:evi" );
    Json::Value pEvi( Json::objectValue );
    pEvi["input"] = inPath.toStdString();
    pEvi["output"] = eviOut.toStdString();
    pEvi["nir"] = 4;
    pEvi["red"] = 3;
    pEvi["blue"] = 1;
    RSOperatorContext ctxEvi;
    Json::Value resEvi = opEvi->run( pEvi, ctxEvi );
    HARNESS_ASSERT( resEvi["numeric_domain"]["regime"].asString() == "dn_scale", "EVI regime must be dn_scale" );

    auto repEvi = sicnu::testing::compareRastersBitExact( eviOut.toStdString(), eviAnchor.toStdString() );
    HARNESS_ASSERT( repEvi.identical, "EVI scale probe must be bit-exact across tiles: " + repEvi.detail );

    // Run chunked SAVI
    auto opSavi = RSOperatorRegistry::instance().create( "rs:savi" );
    Json::Value pSavi( Json::objectValue );
    pSavi["input"] = inPath.toStdString();
    pSavi["output"] = saviOut.toStdString();
    pSavi["nir"] = 4;
    pSavi["red"] = 3;
    RSOperatorContext ctxSavi;
    Json::Value resSavi = opSavi->run( pSavi, ctxSavi );
    HARNESS_ASSERT( resSavi["numeric_domain"]["regime"].asString() == "dn_scale", "SAVI regime must be dn_scale" );

    auto repSavi = sicnu::testing::compareRastersBitExact( saviOut.toStdString(), saviAnchor.toStdString() );
    HARNESS_ASSERT( repSavi.identical, "SAVI scale probe must be bit-exact across tiles: " + repSavi.detail );

    // Also challenge explicit SICNU_NUMERIC_SCALE=5000 metadata
    const QString taggedInPath = tmp.path() + "/s4_tagged.tif";
    const QString taggedEviOut = tmp.path() + "/s4_tagged_evi.tif";
    const QString taggedEviAnchor = tmp.path() + "/s4_tagged_evi_anchor.tif";
    HARNESS_ASSERT( writeTestRaster( taggedInPath, W, H, bands, std::nullopt, { { "SICNU_NUMERIC_SCALE", "5000" } } ).empty(), "Tagged write failed" );

    for ( size_t i = 0; i < totalPixels; ++i )
    {
        nirNorm[i] = bands[3][i] / 5000.0f;
        redNorm[i] = bands[2][i] / 5000.0f;
        blueNorm[i] = bands[0][i] / 5000.0f;
    }
    HARNESS_ASSERT( SpectralIndices::eviUnit( nirNorm.data(), redNorm.data(), blueNorm.data(), eviExp.data(), totalPixels ), "Tagged EVI failed" );
    writeReferenceTiff( taggedEviAnchor, W, H, eviExp.data(), gt, "EPSG:4326" );

    Json::Value pTagged( Json::objectValue );
    pTagged["input"] = taggedInPath.toStdString();
    pTagged["output"] = taggedEviOut.toStdString();
    pTagged["nir"] = 4;
    pTagged["red"] = 3;
    pTagged["blue"] = 1;
    RSOperatorContext ctxTagged;
    opEvi->run( pTagged, ctxTagged );

    auto repTagged = sicnu::testing::compareRastersBitExact( taggedEviOut.toStdString(), taggedEviAnchor.toStdString() );
    HARNESS_ASSERT( repTagged.identical, "Explicit scale tag must be bit-exact: " + repTagged.detail );

    std::cout << "  -> Suite 4 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 5: Comprehensive NoData, NaN, Infinity & Degenerate Denominator Oracle
// ---------------------------------------------------------------------------
void testSuite5_NoDataNanInfDegenerateMasking()
{
    std::cout << "[*] Running Suite 5: Comprehensive NoData, NaN, Infinity & Degenerate Denominator Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 512;
    constexpr int H = 512;
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s5_mask_in.tif";
    const QString outPath = tmp.path() + "/s5_mask_out.tif";
    const QString anchorPath = tmp.path() + "/s5_mask_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );

    for ( size_t i = 0; i < totalPixels; ++i )
    {
        bands[2][i] = 20.0f + static_cast<float>( i % 17 ); // Red
        bands[3][i] = 40.0f + static_cast<float>( i % 23 ); // NIR
    }

    // Inject degenerate cases at various coordinates across multiple tiles
    // Tile 0 (top-left)
    bands[2][10] = -9999.0f; // Declared nodata in Red
    bands[3][20] = -9999.0f; // Declared nodata in NIR
    bands[2][30] = std::numeric_limits<float>::quiet_NaN(); // NaN in Red
    bands[3][40] = std::numeric_limits<float>::quiet_NaN(); // NaN in NIR
    bands[2][50] = std::numeric_limits<float>::infinity();  // +Inf in Red
    bands[3][60] = -std::numeric_limits<float>::infinity(); // -Inf in NIR
    bands[2][70] = 0.0f; bands[3][70] = 0.0f;              // 0/0 denominator
    bands[2][80] = 15.0f; bands[3][80] = -15.0f;           // Opposite sign: Red+NIR = 0

    // Tile 3 (bottom-right: y > 256, x > 256)
    const size_t br = 300 * W + 300;
    bands[2][br] = -9999.0f;
    bands[3][br + 1] = std::numeric_limits<float>::quiet_NaN();
    bands[2][br + 2] = 0.0f; bands[3][br + 2] = 0.0f;

    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands, -9999.0 ).empty(), "Write s5 raster failed" );

    std::vector<float> expected( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        const float r = bands[2][i];
        const float n = bands[3][i];
        if ( r == -9999.0f || n == -9999.0f || !std::isfinite( r ) || !std::isfinite( n ) )
        {
            expected[i] = std::numeric_limits<float>::quiet_NaN();
        }
        else
        {
            expected[i] = MathUtils::safeDiv( n - r, n + r );
        }
    }
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorPath, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    op->run( params, ctx );

    auto rep = sicnu::testing::compareRastersBitExact( outPath.toStdString(), anchorPath.toStdString() );
    HARNESS_ASSERT( rep.identical, "Degenerate masking must be bit-exact: " + rep.detail );

    std::cout << "  -> Suite 5 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 6: Change Detection Streaming Metrics & Pairwise Alignment Oracle
// ---------------------------------------------------------------------------
void testSuite6_ChangeDetectionStreamingMetrics()
{
    std::cout << "[*] Running Suite 6: Change Detection Streaming Metrics & Pairwise Alignment Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 333;
    constexpr int H = 444; // Irregular non-multiple of 256 (2x2 tiles)
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString before = tmp.path() + "/s6_before.tif";
    const QString after = tmp.path() + "/s6_after.tif";
    const QString diffOut = tmp.path() + "/s6_diff.tif";
    const QString diffAnchor = tmp.path() + "/s6_diff_anchor.tif";

    std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
    bBands[0].resize( totalPixels );
    aBands[0].resize( totalPixels );

    for ( size_t i = 0; i < totalPixels; ++i )
    {
        bBands[0][i] = 80.0f + static_cast<float>( i % 43 );
        aBands[0][i] = 120.0f + static_cast<float>( ( i * 5 ) % 47 );
    }

    // Embed sentinel in before, NaN in after
    bBands[0][15] = -9999.0f;
    aBands[0][25] = std::numeric_limits<float>::quiet_NaN();
    bBands[0][35] = std::numeric_limits<float>::infinity();

    HARNESS_ASSERT( writeTestRaster( before, W, H, bBands, -9999.0 ).empty(), "Write before failed" );
    HARNESS_ASSERT( writeTestRaster( after, W, H, aBands ).empty(), "Write after failed" );

    std::vector<float> expected( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        const float b = bBands[0][i];
        const float a = aBands[0][i];
        if ( b == -9999.0f || a == -9999.0f || !std::isfinite( b ) || !std::isfinite( a ) )
            expected[i] = std::numeric_limits<float>::quiet_NaN();
        else
            expected[i] = a - b;
    }
    const MathUtils::Stats oracleStats = MathUtils::computeStats( expected.data(), totalPixels );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( diffAnchor, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = diffOut.toStdString();

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );

    // 1. Bit-exact raster check
    auto rep = sicnu::testing::compareRastersBitExact( diffOut.toStdString(), diffAnchor.toStdString() );
    HARNESS_ASSERT( rep.identical, "Change difference raster must be bit-exact: " + rep.detail );

    // 2. Statistical convergence check
    const double meanDiff = std::abs( res["mean"].asDouble() - oracleStats.mean );
    const double stddevDiff = std::abs( res["stddev"].asDouble() - oracleStats.stddev );
    HARNESS_ASSERT( meanDiff < 1e-4, "Welford mean must match offline stats: diff=" + std::to_string( meanDiff ) );
    HARNESS_ASSERT( stddevDiff < 1e-4, "Welford stddev must match offline stats: diff=" + std::to_string( stddevDiff ) );
    HARNESS_ASSERT( res["min"].asDouble() == oracleStats.min, "Min must match" );
    HARNESS_ASSERT( res["max"].asDouble() == oracleStats.max, "Max must match" );

    std::cout << "  -> Suite 6 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 7: Resumption Bit-Exact Identity under Multi-Point Checkpoint Interruptions
// ---------------------------------------------------------------------------
void testSuite7_ResumptionMultiPointInterrupt()
{
    std::cout << "[*] Running Suite 7: Resumption Bit-Exact Identity under Multi-Point Interruptions..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s7_in.tif";
    const QString freshOut = tmp.path() + "/s7_fresh.tif";
    const QString resumedOut1 = tmp.path() + "/s7_resumed1.tif";
    const QString resumedOut2 = tmp.path() + "/s7_resumed2.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        bands[2][i] = 18.0f + static_cast<float>( i % 19 );
        bands[3][i] = 52.0f + static_cast<float>( i % 29 );
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s7 raster failed" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );

    // 1. Fresh uninterrupted run
    Json::Value pFresh( Json::objectValue );
    pFresh["input"] = inPath.toStdString();
    pFresh["output"] = freshOut.toStdString();
    pFresh["nir"] = 4;
    pFresh["red"] = 3;
    RSOperatorContext ctxFresh;
    op->run( pFresh, ctxFresh );

    // 2. Interrupted at tile 2
    {
        const std::string state1 = ( tmp.path() + "/state1" ).toStdString();
        Json::Value pRes1 = pFresh;
        pRes1["output"] = resumedOut1.toStdString();
        pRes1["resumeStateBase"] = state1;

        std::atomic<int> tileCount1{ 0 };
        RSOperatorContext ctxInt1;
        ctxInt1.setCancelCallback( [&] { return ++tileCount1 > 2; } );

        bool cancelled = false;
        try { op->run( pRes1, ctxInt1 ); }
        catch ( const RSOperatorError &e ) { cancelled = ( e.code() == ErrorCode::Cancelled ); }
        HARNESS_ASSERT( cancelled, "Run 1 must cancel at tile 2" );

        RSOperatorContext ctxCont1;
        Json::Value resCont1 = op->run( pRes1, ctxCont1 );
        HARNESS_ASSERT( resCont1["total_tiles"].asUInt64() == 9, "Total tiles must be 9" );
        HARNESS_ASSERT( resCont1["tiles_reused"].asUInt64() >= 2, "Must reuse >= 2 tiles" );

        auto rep1 = sicnu::testing::compareRastersBitExact( freshOut.toStdString(), resumedOut1.toStdString() );
        HARNESS_ASSERT( rep1.identical, "Resumed output 1 must be bit-exact to fresh: " + rep1.detail );
    }

    // 3. Interrupted at tile 6
    {
        const std::string state2 = ( tmp.path() + "/state2" ).toStdString();
        Json::Value pRes2 = pFresh;
        pRes2["output"] = resumedOut2.toStdString();
        pRes2["resumeStateBase"] = state2;

        std::atomic<int> tileCount2{ 0 };
        RSOperatorContext ctxInt2;
        ctxInt2.setCancelCallback( [&] { return ++tileCount2 > 6; } );

        bool cancelled = false;
        try { op->run( pRes2, ctxInt2 ); }
        catch ( const RSOperatorError &e ) { cancelled = ( e.code() == ErrorCode::Cancelled ); }
        HARNESS_ASSERT( cancelled, "Run 2 must cancel at tile 6" );

        RSOperatorContext ctxCont2;
        Json::Value resCont2 = op->run( pRes2, ctxCont2 );
        HARNESS_ASSERT( resCont2["total_tiles"].asUInt64() == 9, "Total tiles must be 9" );
        HARNESS_ASSERT( resCont2["tiles_reused"].asUInt64() >= 6, "Must reuse >= 6 tiles" );

        auto rep2 = sicnu::testing::compareRastersBitExact( freshOut.toStdString(), resumedOut2.toStdString() );
        HARNESS_ASSERT( rep2.identical, "Resumed output 2 must be bit-exact to fresh: " + rep2.detail );
    }

    std::cout << "  -> Suite 7 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 8: Pipeline vs Resumable Execution Mode Bit-Exact Parity Oracle
// ---------------------------------------------------------------------------
void testSuite8_ExecutionModesBitExact()
{
    std::cout << "[*] Running Suite 8: Pipeline vs Resumable Execution Mode Bit-Exact Parity Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 512;
    constexpr int H = 512;
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s8_in.tif";
    const QString pipOut = tmp.path() + "/s8_pip.tif";
    const QString resOut = tmp.path() + "/s8_res.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        bands[2][i] = 14.0f + static_cast<float>( i % 11 );
        bands[3][i] = 48.0f + static_cast<float>( i % 23 );
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s8 raster failed" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    // Run pipeline mode
    params["output"] = pipOut.toStdString();
    params["executionMode"] = "pipeline";
    RSOperatorContext ctxPip;
    op->run( params, ctxPip );

    // Run resumable mode
    params["output"] = resOut.toStdString();
    params["executionMode"] = "resumable";
    RSOperatorContext ctxRes;
    op->run( params, ctxRes );

    auto rep = sicnu::testing::compareRastersBitExact( pipOut.toStdString(), resOut.toStdString() );
    HARNESS_ASSERT( rep.identical, "Pipeline and Resumable modes must produce bit-identical rasters: " + rep.detail );

    std::cout << "  -> Suite 8 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 9: OutputCommitter Transaction & Exactly-Once Asset Oracle
// ---------------------------------------------------------------------------
void testSuite9_OutputCommitterIntegration()
{
    std::cout << "[*] Running Suite 9: OutputCommitter Transaction & Exactly-Once Asset Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 512;
    constexpr int H = 512;
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s9_in.tif";
    const QString tempOut = tmp.path() + "/s9_temp.tif";
    const QString stableOut = tmp.path() + "/s9_stable.tif";
    const QString anchorOut = tmp.path() + "/s9_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        bands[2][i] = 16.0f + static_cast<float>( i % 13 );
        bands[3][i] = 44.0f + static_cast<float>( i % 19 );
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s9 raster failed" );

    std::vector<float> expected( totalPixels );
    HARNESS_ASSERT( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(), expected.data(), totalPixels ), "NormDiff failed" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    commitSpec.kind = sicnu::data::AssetKind::Raster;
    commitSpec.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    commitSpec.autoLoad = false;

    sicnu::data::DerivationRecord derivation;
    derivation.algorithmId = QStringLiteral( "rs:ndvi" );
    derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    commitSpec.derivation = derivation;

    ChunkedRunOptions opts;
    opts.commitSpec = commitSpec;

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = stableOut.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    Json::Value res = rs::spectral_index_detail::runSpectralIndexCore(
        "NDVI", params, ctx, false, "rs:ndvi", opts );

    HARNESS_ASSERT( !QFile::exists( tempOut ), "tempPath must be consumed by committer" );
    HARNESS_ASSERT( QFile::exists( stableOut ), "stablePath must be created atomically" );
    HARNESS_ASSERT( !res["published_asset_id"].asString().empty(), "published_asset_id must be populated" );

    auto rep = sicnu::testing::compareRastersBitExact( stableOut.toStdString(), anchorOut.toStdString() );
    HARNESS_ASSERT( rep.identical, "Committer output must be bit-exact: " + rep.detail );

    std::cout << "  -> Suite 9 PASSED." << std::endl;
}

// ---------------------------------------------------------------------------
// Suite 10: Machine Epsilon & IEEE-754 Precision Invariance Oracle
// ---------------------------------------------------------------------------
void testSuite10_Ieee754MachineEpsilonPrecision()
{
    std::cout << "[*] Running Suite 10: Machine Epsilon & IEEE-754 Precision Invariance Oracle..." << std::endl;
    QTemporaryDir tmp;
    HARNESS_ASSERT( tmp.isValid(), "Temporary directory must be valid" );

    constexpr int W = 400;
    constexpr int H = 400; // 160,000 pixels across 4 tiles
    const size_t totalPixels = static_cast<size_t>( W ) * H;

    const QString inPath = tmp.path() + "/s10_in.tif";
    const QString chunkedOut = tmp.path() + "/s10_chunked.tif";
    const QString anchorOut = tmp.path() + "/s10_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( totalPixels );

    // Generate high-entropy float values testing standard IEEE-754 binary operations
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        const float v = static_cast<float>( i ) * 0.00314159265f;
        bands[2][i] = 10.0f + std::sin( v ) * 5.0f; // Red
        bands[3][i] = 50.0f + std::cos( v ) * 10.0f; // NIR
    }
    HARNESS_ASSERT( writeTestRaster( inPath, W, H, bands ).empty(), "Write s10 raster failed" );

    std::vector<float> expected( totalPixels );
    HARNESS_ASSERT( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(), expected.data(), totalPixels ), "NormDiff failed" );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = chunkedOut.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    op->run( params, ctx );

    // 1. Bit-exact raw byte equality
    auto rep = sicnu::testing::compareRastersBitExact( chunkedOut.toStdString(), anchorOut.toStdString() );
    HARNESS_ASSERT( rep.identical, "Raw bytes must be 100% identical: " + rep.detail );

    // 2. Read back floats and verify max absolute difference is strictly 0.0f
    GdalDatasetWrapper outDs;
    HARNESS_ASSERT( outDs.open( chunkedOut ), "Open chunked out failed" );
    std::vector<float> readBuf( totalPixels );
    HARNESS_ASSERT( outDs.readBandWindow( 1, 0, 0, W, H, readBuf.data() ), "Read chunked band failed" );

    float maxDiff = 0.0f;
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        const float diff = std::abs( readBuf[i] - expected[i] );
        if ( diff > maxDiff ) maxDiff = diff;
    }
    HARNESS_ASSERT( maxDiff == 0.0f, "Machine epsilon precision violated! maxDiff = " + std::to_string( maxDiff ) );

    std::cout << "  -> Suite 10 PASSED." << std::endl;
}

} // anonymous namespace

int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );

    std::cout << "========================================================" << std::endl;
    std::cout << "Milestone 3 Empirical Challenger Stress Verification" << std::endl;
    std::cout << "========================================================" << std::endl;

    testSuite1_OperatorParityBitExact();
    testSuite2_EdgeTileDimensionStress();
    testSuite3_HaloZeroBoundaryContinuity();
    testSuite4_ScaleProbeInvariant801();
    testSuite5_NoDataNanInfDegenerateMasking();
    testSuite6_ChangeDetectionStreamingMetrics();
    testSuite7_ResumptionMultiPointInterrupt();
    testSuite8_ExecutionModesBitExact();
    testSuite9_OutputCommitterIntegration();
    testSuite10_Ieee754MachineEpsilonPrecision();

    std::cout << "\n[+] ALL 10 CHALLENGE SUITES PASSED EMPIRICALLY!" << std::endl;
    return 0;
}
