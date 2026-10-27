// test_image_fusion.cpp — Phase 11.1: Image fusion tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/fusion_quality_report.h"
#include "processing/algorithms/image_fusion.h"
#include "processing/algorithms/math_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <json/json.h>

#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <limits>
#include <gdal.h>
#include <sstream>
#include <vector>

using Catch::Approx;

static const float NODATA = -9999.0f;

// ===========================================================================
// Brovey
// ===========================================================================

TEST_CASE( "Brovey: uniform bands produce uniform output", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 200.0f );

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::brovey( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    for ( int i = 0; i < N; ++i )
    {
        // Each band = (100/300) * 200 = 66.67
        REQUIRE( result[0][i] == Approx( 200.0f / 3.0f ).margin( 0.1f ) );
        REQUIRE( result[1][i] == Approx( 200.0f / 3.0f ).margin( 0.1f ) );
        REQUIRE( result[2][i] == Approx( 200.0f / 3.0f ).margin( 0.1f ) );
    }
}

TEST_CASE( "Brovey: preserves spectral ratio", "[fusion]" )
{
    const int W = 4, H = 4, N = W * H;
    // R=200, G=100, B=50 → ratio 4:2:1
    std::vector<float> r( N, 200.0f ), g( N, 100.0f ), b( N, 50.0f );
    std::vector<float> pan( N, 350.0f );

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::brovey( msBands, 3, pan.data(), W, H, NODATA );

    // sum = 350, ratio: R=200/350, G=100/350, B=50/350
    // fused R = (200/350)*350 = 200, G = (100/350)*350 = 100, B = (50/350)*350 = 50
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] == Approx( 200.0f ).margin( 0.1f ) );
        REQUIRE( result[1][i] == Approx( 100.0f ).margin( 0.1f ) );
        REQUIRE( result[2][i] == Approx( 50.0f ).margin( 0.1f ) );
    }
}

TEST_CASE( "Brovey: nodata preserved", "[fusion]" )
{
    const int W = 4, H = 4, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 200.0f );
    r[0] = NODATA;
    pan[1] = NODATA;

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::brovey( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result[0][0] == NODATA );
    REQUIRE( result[1][0] == NODATA );
    REQUIRE( result[2][0] == NODATA );
    REQUIRE( result[0][1] == NODATA );
    REQUIRE( result[1][1] == NODATA );
    REQUIRE( result[2][1] == NODATA );
    // Other pixels should be valid
    REQUIRE( result[0][2] != NODATA );
    REQUIRE( result[1][2] != NODATA );
    REQUIRE( result[2][2] != NODATA );
}

TEST_CASE( "Brovey: null input returns empty", "[fusion]" )
{
    auto result = ImageFusion::brovey( {}, 0, nullptr, 0, 0, NODATA );
    REQUIRE( result.isEmpty() );
}

// ===========================================================================
// IHS Fusion
// ===========================================================================

TEST_CASE( "IHS: uniform RGB produces uniform output", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 100.0f ); // same as intensity

    auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                           pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    for ( int i = 0; i < N; ++i )
    {
        // With matched pan ≈ intensity, output should be close to input
        REQUIRE( result[0][i] == Approx( 100.0f ).margin( 5.0f ) );
        REQUIRE( result[1][i] == Approx( 100.0f ).margin( 5.0f ) );
        REQUIRE( result[2][i] == Approx( 100.0f ).margin( 5.0f ) );
    }
}

TEST_CASE( "IHS: higher pan produces valid output", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 200.0f ); // brighter pan

    auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                           pan.data(), W, H, NODATA );

    // Output should be valid (non-negative, non-nodata)
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] >= 0.0f );
        REQUIRE( result[1][i] >= 0.0f );
        REQUIRE( result[2][i] >= 0.0f );
        REQUIRE( result[0][i] != NODATA );
    }
}

TEST_CASE( "IHS: chromatic RGB reproduces input when pan matches intensity (#328)", "[fusion]" )
{
    const int W = 4, H = 4, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 50.0f ), b( N, 0.0f );
    std::vector<float> pan( N, 50.0f ); // (100 + 50 + 0) / 3 = 50.0f

    auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                           pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] == Approx( 100.0f ).margin( 0.01f ) );
        REQUIRE( result[1][i] == Approx( 50.0f ).margin( 0.01f ) );
        REQUIRE( result[2][i] == Approx( 0.0f ).margin( 0.01f ) );
    }
}

TEST_CASE( "IHS: pure green and pure red do not swap channels (#328)", "[fusion]" )
{
    const int W = 2, H = 2, N = W * H;
    std::vector<float> r( N, 0.0f ), g( N, 120.0f ), b( N, 0.0f );
    std::vector<float> pan( N, 40.0f ); // I = 40.0f

    auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                           pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] == Approx( 0.0f ).margin( 0.01f ) );
        REQUIRE( result[1][i] == Approx( 120.0f ).margin( 0.01f ) );
        REQUIRE( result[2][i] == Approx( 0.0f ).margin( 0.01f ) );
    }
}

TEST_CASE( "IHS: null input returns empty", "[fusion]" )
{
    auto result = ImageFusion::ihsFusion( nullptr, nullptr, nullptr,
                                           nullptr, 0, 0, NODATA );
    REQUIRE( result.isEmpty() );
}

TEST_CASE( "IHS: NaN NoData pixels stay NoData instead of collapsing to black", "[fusion][hardening16]" )
{
    // RED on master: NaN passed every `== nodata` gate (NaN compares false),
    // flowed through rgbToIhs/ihsToRgb, and the non-negative clamp
    // (std::max(0.0f, NaN) == 0.0f) wrote legit-looking black pixels.
    const int W = 4, H = 4, N = W * H;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    SECTION( "declared float sentinel + NaN holes" )
    {
        std::vector<float> r( N, 100.0f ), g( N, 60.0f ), b( N, 30.0f );
        std::vector<float> pan( N, 63.3f );
        r[0] = nan; g[0] = nan; b[0] = nan;

        auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                               pan.data(), W, H, NODATA );
        REQUIRE( result.size() == 3 );
        for ( int band = 0; band < 3; ++band )
        {
            REQUIRE( result[band][0] == NODATA );
            // Valid pixels keep their fused values.
            REQUIRE( result[band][1] != NODATA );
            REQUIRE( std::isfinite( result[band][1] ) );
        }
    }

    SECTION( "NaN sentinel (the calibrated/QUAC output convention)" )
    {
        std::vector<float> r( N, 100.0f ), g( N, 60.0f ), b( N, 30.0f );
        std::vector<float> pan( N, 63.3f );
        r[0] = nan; g[0] = nan; b[0] = nan;
        pan[5] = nan;

        auto result = ImageFusion::ihsFusion( r.data(), g.data(), b.data(),
                                               pan.data(), W, H, nan );
        REQUIRE( result.size() == 3 );
        for ( int band = 0; band < 3; ++band )
        {
            REQUIRE( std::isnan( result[band][0] ) ); // NaN MS hole -> NaN
            REQUIRE( std::isnan( result[band][5] ) ); // NaN pan -> NaN
            REQUIRE( std::isfinite( result[band][1] ) ); // valid pixel survives
        }
    }
}

// ===========================================================================
// PCA Fusion
// ===========================================================================

TEST_CASE( "PCA Fusion: produces correct number of bands", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 150.0f );

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::pcaFusion( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    REQUIRE( result[0].size() == N );
    REQUIRE( result[1].size() == N );
    REQUIRE( result[2].size() == N );
}

TEST_CASE( "PCA Fusion: null input returns empty", "[fusion]" )
{
    auto result = ImageFusion::pcaFusion( {}, 0, nullptr, 0, 0, NODATA );
    REQUIRE( result.isEmpty() );
}

TEST_CASE( "PCA Fusion: uniform input produces uniform output", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 100.0f );

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::pcaFusion( msBands, 3, pan.data(), W, H, NODATA );

    // With uniform input and pan ≈ intensity, output should be close to input
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] == Approx( 100.0f ).margin( 10.0f ) );
        REQUIRE( result[1][i] == Approx( 100.0f ).margin( 10.0f ) );
        REQUIRE( result[2][i] == Approx( 100.0f ).margin( 10.0f ) );
    }
}

// ===========================================================================
// Gram-Schmidt (GS) Fusion
// ===========================================================================

TEST_CASE( "Gram-Schmidt: produces correct number of bands", "[fusion]" )
{
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 150.0f );

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::gramSchmidtFusion( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    REQUIRE( result[0].size() == static_cast<size_t>( N ) );
    REQUIRE( result[1].size() == static_cast<size_t>( N ) );
    REQUIRE( result[2].size() == static_cast<size_t>( N ) );
}

TEST_CASE( "Gram-Schmidt: null input returns empty", "[fusion]" )
{
    auto result = ImageFusion::gramSchmidtFusion( {}, 0, nullptr, 0, 0, NODATA );
    REQUIRE( result.isEmpty() );
}

TEST_CASE( "Gram-Schmidt: uniform MS with pan equal to simulated pan preserves values", "[fusion]" )
{
    // GS simulates a low-res pan as the mean of the MS bands. When the supplied
    // high-res pan equals that simulated mean, GS component 1 is replaced by an
    // identical (histogram-matched) value, so the inverse transform recovers
    // the original MS values.
    const int W = 8, H = 8, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 100.0f ); // == mean(r,g,b)

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::gramSchmidtFusion( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( result[0][i] == Approx( 100.0f ).margin( 10.0f ) );
        REQUIRE( result[1][i] == Approx( 100.0f ).margin( 10.0f ) );
        REQUIRE( result[2][i] == Approx( 100.0f ).margin( 10.0f ) );
    }
}

TEST_CASE( "Gram-Schmidt: nodata preserved", "[fusion]" )
{
    const int W = 4, H = 4, N = W * H;
    std::vector<float> r( N, 100.0f ), g( N, 100.0f ), b( N, 100.0f );
    std::vector<float> pan( N, 150.0f );
    r[0] = NODATA;     // nodata in one MS band
    pan[1] = NODATA;   // nodata in pan

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    auto result = ImageFusion::gramSchmidtFusion( msBands, 3, pan.data(), W, H, NODATA );

    REQUIRE( result.size() == 3 );
    REQUIRE( result[0][0] == NODATA ); // MS nodata propagates
    REQUIRE( result[0][1] == NODATA ); // pan nodata propagates
    REQUIRE( result[0][2] != NODATA ); // valid pixel stays valid
}

#include "processing/gdal/gdal_dataset_wrapper.h"
#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>

TEST_CASE( "ImageFusion processNativeFusion writes brovey output", "[fusion][gdal]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 2, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.f, 20.f, 30.f, 40.f };
    std::vector<float> ms1 = { 4.f, 8.f, 12.f, 16.f };
    std::vector<float> ms2 = { 2.f, 4.f, 6.f, 8.f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                           pan.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           ms1.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           ms2.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( error.isEmpty() );
}

TEST_CASE( "ImageFusion processNativeFusion writes gram-schmidt output", "[fusion][gdal]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_gs.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 2, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 3, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.f, 20.f, 30.f, 40.f };
    std::vector<float> ms1 = { 4.f, 8.f, 12.f, 16.f };
    std::vector<float> ms2 = { 2.f, 4.f, 6.f, 8.f };
    std::vector<float> ms3 = { 6.f, 12.f, 18.f, 24.f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                           pan.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           ms1.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           ms2.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 3 ), GF_Write, 0, 0, 2, 2,
                           ms3.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "gram_schmidt" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( error.isEmpty() );

    // Verify output has 3 bands (GS preserves band count)
    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 3 );
}

TEST_CASE( "ImageFusion rejects non-co-registered pan and MS rasters", "[fusion][gdal][c3]" )
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused.tif" ) );

    // Same grid, different CRSs.
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 2, 2, 1, GDT_Float32, gt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 2, GDT_Float32, gt,
                                          QStringLiteral( "EPSG:4326" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );
    std::vector<float> pan = { 10.f, 20.f, 30.f, 40.f };
    std::vector<float> ms = { 4.f, 8.f, 12.f, 16.f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                           pan.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           ms.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    CHECK_FALSE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    CHECK( error.contains( QStringLiteral( "co-registered" ) ) );
    CHECK_FALSE( QFile::exists( outputPath ) );
}

TEST_CASE( "ImageFusion allows differing resolutions (pan-sharpening design)", "[fusion][gdal][c3]" )
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused.tif" ) );

    // Same CRS and extent; pan at 1 m (4x4), MS at 2 m (2x2) — the MS is
    // resampled onto the pan grid by design.
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 2.0, 0.0, 0.0, 0.0, -2.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 1, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );
    std::vector<float> pan( 16, 10.0f );
    std::vector<float> ms( 4, 5.0f );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           pan.data(), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           ms.data(), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( QFile::exists( outputPath ) );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    CHECK( out.width() == 4 );
    CHECK( out.height() == 4 );
}

TEST_CASE( "ImageFusion rejects mirrored or south-up rasters (#332)", "[fusion][gdal]" )
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_southup.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused.tif" ) );

    // Pan is north-up (-1.0 pixelH), MS is south-up (+1.0 pixelH)
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msPath, 4, 4, 1, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );
    std::vector<float> pan( 16, 10.0f );
    std::vector<float> ms( 16, 5.0f );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           pan.data(), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 4, 4,
                           ms.data(), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    CHECK_FALSE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    CHECK( error.contains( QStringLiteral( "opposite axis orientation" ) ) );
    CHECK_FALSE( QFile::exists( outputPath ) );
}

TEST_CASE( "Native PCA Fusion - Jacobi Eigen Decomposition Accuracy", "[image_fusion][pca]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_pca.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_pca.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_pca.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    constexpr int W = 4, H = 4, B = 3;
    GDALDatasetH panDs = createOutputTiff( panPath, W, H, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, W, H, B, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    // Synthetic correlated 3-band MS image and PAN image
    std::vector<float> pan( W * H, 50.0f );
    std::vector<float> ms1( W * H ), ms2( W * H ), ms3( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        pan[i] = 20.0f + 5.0f * ( i % 4 );
        ms1[i] = 10.0f + 2.0f * i;
        ms2[i] = 20.0f + 1.5f * i + 3.0f * ( i % 2 );
        ms3[i] = 15.0f + 2.5f * i - 1.0f * ( i % 3 );
    }

    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, W, H,
                           pan.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, W, H,
                           ms1.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, W, H,
                           ms2.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 3 ), GF_Write, 0, 0, W, H,
                           ms3.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "pca" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 3 );
    for ( int b = 1; b <= 3; ++b )
    {
        std::vector<float> fusedData( W * H );
        REQUIRE( out.readBandData( b, fusedData.data(), W, H ) );
        for ( float val : fusedData )
        {
            CHECK( std::isfinite( val ) );
            CHECK( val > 0.0f );
        }
    }
}


TEST_CASE( "Brovey: negative band sums map to nodata, not sign-inverted values (#611)", "[fusion]" )
{
    constexpr int W = 2, H = 1;
    const float NODATA = -9999.0f;
    // Band 2 has negative values such that the sum is negative for pixel 0.
    std::vector<float> b1 = { 0.3f, 0.2f };
    std::vector<float> b2 = { -0.5f, 0.3f };
    std::vector<float> pan = { 10.0f, 10.0f };
    QVector<const float *> ms = { b1.data(), b2.data() };
    auto result = ImageFusion::brovey( ms, 2, pan.data(), W, H, NODATA );
    REQUIRE( result.size() == 2 );
    // sum for pixel 0 = -0.2 (negative) -> nodata; pixel 1 sum = 0.5 -> valid.
    REQUIRE( result[0][0] == NODATA );
    REQUIRE( result[1][0] == NODATA );
    REQUIRE( result[0][1] != NODATA );
}

TEST_CASE( "PCA Fusion: output correlates positively with pan regardless of eigenvector sign (#611)", "[fusion]" )
{
    // Construct a scene whose leading principal component is negatively
    // correlated with the pan band: all MS bands DECREASE where pan
    // increases. Without the sign convention, the fused output would be
    // spatially inverted relative to pan.
    constexpr int W = 8, H = 1;
    const float NODATA = -9999.0f;
    std::vector<float> mb0, mb1, mb2;
    std::vector<float> pan( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        const float t = static_cast<float>( i );
        mb0.push_back( 100.0f - 3.0f * t );  // strongly anti-correlated with pan
        mb1.push_back( 50.0f - 1.0f * t );
        mb2.push_back( 20.0f - 0.5f * t );
        pan[i] = t;
    }
    QVector<const float *> msBands = { mb0.data(), mb1.data(), mb2.data() };
    auto result = ImageFusion::pcaFusion( msBands, 3, pan.data(), W, H, NODATA );
    REQUIRE( result.size() == 3 );
    // The fused product must vary in the SAME direction as pan: the last
    // pixel (brightest pan) must exceed the first pixel (darkest pan) in
    // every band - PC1 (dominated by the common trend) now carries the pan
    // structure with the corrected sign.
    for ( int b = 0; b < 3; ++b )
    {
        REQUIRE( result[b][W * H - 1] > result[b][0] );
    }
}

// ===========================================================================
// Linear fusion weight padding (#677)
// ===========================================================================

TEST_CASE( "Linear fusion pads a short msWeights list instead of reading out of bounds (#677)", "[fusion][677]" )
{
    constexpr int W = 2, H = 1;
    std::vector<float> b1 = { 1.0f, 2.0f };
    std::vector<float> b2 = { 3.0f, 4.0f };
    std::vector<float> pan = { 10.0f, 20.0f };
    QVector<const float *> ms = { b1.data(), b2.data() };

    // Non-empty but shorter than the band count: used to index weights[1]
    // out of bounds (UB). Now padded with the equal-weight default
    // (1 - panWeight = 0.8) and fused deterministically.
    QVector<float> shortWeights = { 0.5f };
    auto result = ImageFusion::linearWeighted( ms, 2, pan.data(), W, H, NODATA,
                                               shortWeights, 0.2f );
    REQUIRE( result.size() == 2 );
    // Band 1 keeps its explicit weight 0.5.
    CHECK( result[0][0] == Approx( 0.5f * 1.0f + 0.2f * 10.0f ).margin( 1e-4 ) );
    CHECK( result[0][1] == Approx( 0.5f * 2.0f + 0.2f * 20.0f ).margin( 1e-4 ) );
    // Band 2 is fused with the padded default weight 0.8.
    CHECK( result[1][0] == Approx( 0.8f * 3.0f + 0.2f * 10.0f ).margin( 1e-4 ) );
    CHECK( result[1][1] == Approx( 0.8f * 4.0f + 0.2f * 20.0f ).margin( 1e-4 ) );
}

TEST_CASE( "Streaming linear fusion pads a short msWeights list (#677)", "[fusion][gdal][677]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_pad.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_pad.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_pad.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.0f, 20.0f };
    std::vector<float> ms1 = { 1.0f, 2.0f };
    std::vector<float> ms2 = { 3.0f, 4.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 1,
                           ms2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "linear" );
    params.panWeight = 0.2f;
    params.msWeights = { 0.5f };  // 1 entry for a 2-band MS raster (#677)
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    std::vector<float> out1( 2 ), out2( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 2, 1 ) );
    CHECK( out1[0] == Approx( 0.5f * 1.0f + 0.2f * 10.0f ).margin( 1e-4 ) );
    CHECK( out1[1] == Approx( 0.5f * 2.0f + 0.2f * 20.0f ).margin( 1e-4 ) );
    CHECK( out2[0] == Approx( 0.8f * 3.0f + 0.2f * 10.0f ).margin( 1e-4 ) );
    CHECK( out2[1] == Approx( 0.8f * 4.0f + 0.2f * 20.0f ).margin( 1e-4 ) );
}

// ===========================================================================
// Streaming Brovey must match the in-memory kernel on partial coverage (#700)
// ===========================================================================

TEST_CASE( "Streaming brovey invalidates partially-NoData pixels like the kernel (#700)", "[fusion][gdal][700]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_nd.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_nd.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_nd.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 2x1 MS raster, 2 bands: pixel 0 has NoData in band 2 only.
    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.0f, 10.0f };
    std::vector<float> ms1 = { 4.0f, 4.0f };
    std::vector<float> ms2 = { NODATA, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( panDs, 1 ), NODATA ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 1,
                           ms2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( msDs, 1 ), NODATA ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( msDs, 2 ), NODATA ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    // Reference: the in-memory kernel marks pixel 0 invalid in every band.
    const float b1v = 4.0f, b2v = 2.0f, pv = 10.0f;
    std::vector<float> k1 = { b1v, b1v }, k2 = { NODATA, b2v }, kp = { pv, pv };
    QVector<const float *> kms = { k1.data(), k2.data() };
    auto expected = ImageFusion::brovey( kms, 2, kp.data(), 2, 1, NODATA );
    REQUIRE( expected[0][0] == NODATA );
    REQUIRE( expected[1][0] == NODATA );
    REQUIRE( expected[0][1] != NODATA );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    // The streamed output must agree: pixel 0 is NoData in every band, not a
    // partial-sum fusion of band 1 alone. R14-A per-band wash: streamed voids
    // are NaN with a NaN band tag (the spectral-family convention), not an
    // echo of the input's finite sentinel.
    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    std::vector<float> out1( 2 ), out2( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 2, 1 ) );
    CHECK( std::isnan( out1[0] ) );
    CHECK( std::isnan( out2[0] ) );
    bool hasNd = false;
    double ndTag = out.bandNoDataValue( 1, &hasNd );
    CHECK( hasNd );
    CHECK( std::isnan( ndTag ) );
    // Pixel 1 fuses normally: (4/6)*10, (2/6)*10.
    CHECK( out1[1] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    CHECK( out2[1] == Approx( ( 2.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
}

TEST_CASE( "Streaming brovey honors heterogeneous per-band NoData declarations [r14a]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_het.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "ms_het_raw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_het.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_het.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 2x1 inputs, 2 MS bands with DIFFERENT declared sentinels: pan and MS
    // band 1 void at 0.0, MS band 2 voids at -9999. Pixel 0 carries a band-2
    // void. Under the old single working sentinel (pan's 0.0), the -9999
    // passed as data and the pixel fused to garbage instead of void.
    // GTiff stores ONE NoData tag per dataset, so the MS declarations ride on
    // a VRT wrapper (test_r5_nodata_contracts' fixture strategy).
    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.0f, 10.0f };
    std::vector<float> ms1 = { 4.0f, 4.0f };
    std::vector<float> ms2 = { -9999.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( panDs, 1 ), 0.0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 1,
                           ms2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    {
        QFile vrt( msPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"1\">\n";
        // A14: the VRT carries the source geotransform (as GDAL-produced
        // VRTs do). A GT-less MS paired with the referenced pan above is
        // exactly the asymmetric case the fusion now refuses.
        ts << "  <GeoTransform>0.0, 1.0, 0.0, 0.0, 0.0, -1.0</GeoTransform>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        ts << "    <NoDataValue>0</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_het_raw.tif"
              "</SourceFilename><SourceBand>1</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
        ts << "    <NoDataValue>-9999</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_het_raw.tif"
              "</SourceFilename><SourceBand>2</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "</VRTDataset>\n";
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    std::vector<float> out1( 2 ), out2( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 2, 1 ) );
    // Band-2 void voids the fused pixel in every band (brovey's any-void rule).
    CHECK( std::isnan( out1[0] ) );
    CHECK( std::isnan( out2[0] ) );
    // Pixel 1 fuses normally: (4/6)*10, (2/6)*10.
    CHECK( out1[1] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    CHECK( out2[1] == Approx( ( 2.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    // Void outputs carry the NaN band tag (family convention after the wash).
    bool hasNd = false;
    double ndTag = out.bandNoDataValue( 1, &hasNd );
    CHECK( hasNd );
    CHECK( std::isnan( ndTag ) );
}

// ===========================================================================
// F15 (ADR 0163): HPF method + fusion quality report
// ===========================================================================

namespace
{
// Creates a 2x2 pan/MS fixture pair and returns true on success.
bool writeHpfFixture( const QString &panPath, const QString &msPath,
                      const std::vector<float> &pan, const std::vector<float> &ms )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 2, 2, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 1, GDT_Float32, gt, QString() );
    if ( !panDs || !msDs )
        return false;
    bool ok = GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                            const_cast<float *>( pan.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None &&
              GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                            const_cast<float *>( ms.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( panDs );
    GDALClose( msDs );
    return ok;
}
} // namespace

TEST_CASE( "ImageFusion processNativeFusion hpf has a known closed-form answer", "[fusion][gdal][f15]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString panPath = dir.filePath( QStringLiteral( "pan.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_hpf.tif" ) );

    // pan = [0,100;100,200]: every pixel's clipped 3x3 mean is exactly 100.
    const std::vector<float> pan = { 0.f, 100.f, 100.f, 200.f };
    const std::vector<float> ms( 4, 300.0f );
    REQUIRE( writeHpfFixture( panPath, msPath, pan, ms ) );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "hpf" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> fused( 4 );
    REQUIRE( out.readBandWindow( 1, 0, 0, 2, 2, fused.data() ) );
    // fused = MS + (pan - 100): hand-computed per pixel.
    CHECK( fused[0] == Approx( 300.f + ( 0.f - 100.f ) ).margin( 0.01f ) );
    CHECK( fused[1] == Approx( 300.f + ( 100.f - 100.f ) ).margin( 0.01f ) );
    CHECK( fused[2] == Approx( 300.f + ( 100.f - 100.f ) ).margin( 0.01f ) );
    CHECK( fused[3] == Approx( 300.f + ( 200.f - 100.f ) ).margin( 0.01f ) );
}

TEST_CASE( "ImageFusion processNativeFusion qualityReport verdicts are honest", "[fusion][gdal][f15]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Case 1: identical pan/MS radiometry -> fused == MS -> report passes.
    {
        const QString panPath = dir.filePath( QStringLiteral( "eq_pan.tif" ) );
        const QString msPath = dir.filePath( QStringLiteral( "eq_ms.tif" ) );
        const QString outputPath = dir.filePath( QStringLiteral( "eq_fused.tif" ) );
        const QString reportPath = dir.filePath( QStringLiteral( "eq_report.json" ) );
        const std::vector<float> v( 4, 300.0f );
        REQUIRE( writeHpfFixture( panPath, msPath, v, v ) );

        ImageFusion::NativeFusionParams params;
        params.method = QStringLiteral( "hpf" );
        params.qualityReportPath = reportPath;
        rs::fusion::FusionQualityReport report;
        QString error;
        REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params,
                                                   &error, &report ) );
        REQUIRE( QFile::exists( reportPath ) );
        CHECK( report.passed );
        CHECK( report.qIndex == Approx( 1.0 ).margin( 1e-6 ) );
        CHECK( report.ergas == Approx( 0.0 ).margin( 1e-4 ) );

        // The written artifact parses and carries the fixed schema.
        QFile f( reportPath );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        const QByteArray bytes = f.readAll();
        Json::Value root;
        Json::CharReaderBuilder rb;
        std::istringstream in( bytes.toStdString() );
        std::string errs;
        REQUIRE( Json::parseFromStream( rb, in, &root, &errs ) );
        CHECK( root["schema"].asString() == "exp-rs/fusion-quality-report@1" );
        CHECK( root["verdict"]["passed"].asBool() );
    }

    // Case 2: strong pan contrast -> high-pass injection -> distortion flagged.
    {
        const QString panPath = dir.filePath( QStringLiteral( "bad_pan.tif" ) );
        const QString msPath = dir.filePath( QStringLiteral( "bad_ms.tif" ) );
        const QString outputPath = dir.filePath( QStringLiteral( "bad_fused.tif" ) );
        const QString reportPath = dir.filePath( QStringLiteral( "bad_report.json" ) );
        const std::vector<float> pan = { 0.f, 100.f, 100.f, 200.f };
        const std::vector<float> ms( 4, 300.0f );
        REQUIRE( writeHpfFixture( panPath, msPath, pan, ms ) );

        ImageFusion::NativeFusionParams params;
        params.method = QStringLiteral( "hpf" );
        params.qualityReportPath = reportPath;
        rs::fusion::FusionQualityReport report;
        QString error;
        REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params,
                                                   &error, &report ) );
        REQUIRE( QFile::exists( reportPath ) );
        CHECK_FALSE( report.passed );
        CHECK_FALSE( report.violations.empty() );
    }
}

// ===========================================================================
// R15-A (O1) — CLOSED AS FALSE POSITIVE (empirical re-qualification).
//
// Original hypothesis: GDAL's RasterIO bilinear kernel ignores NoData, so a
// declared sentinel adjacent to valid pixels blends into a FINITE
// contaminated value that the exact-match wash cannot catch (the R14-A
// residual "values GDAL resampling already blended across a sentinel edge
// stay finite"). The RED driver was a VRT fixture WITHOUT a <GeoTransform>
// element.
//
// What the diagnostics established (diag1-diag4, see .planning/r15-a/
// FINDINGS.md):
//  1. GDAL's VRT parser does NOT inherit the source geotransform; a VRT
//     without <GeoTransform> reports the DEFAULT IDENTITY transform
//     [0,1,0,0,0,1]. The fusion then computes scaleX = 1.0, takes the
//     same-resolution branch (readBandWindow instead of
//     readBandWindowScaled), clamps the pan-grid window to the smaller MS
//     raster and pads the remainder with band 2's declared sentinel. The
//     wash voids exactly that padded region, leaving only the in-range
//     non-sentinel pixels valid: {NaN, 6.667, NaN, NaN, 6.667, 6.667,
//     NaN x10} — the pattern that looked like "contamination".
//  2. With a realistic fixture (VRT carrying the source geotransform, as
//     every GDAL-produced VRT does), the fusion resamples onto the pan grid
//     and GDAL's GRIORA_Bilinear is NoData-AWARE for bands with a declared
//     sentinel: void taps are excluded from the kernel and the remaining
//     weights are renormalized (the family convention — resampler.cpp
//     weight renormalization, GDAL native behavior, rs_simple_segmenter).
//     The void value itself passes through only where it is the dominant
//     tap, and there the exact-match wash catches it. No sentinel ever
//     blends into a finite neighbor value.
//
// Net effect: production behavior with a georeferenced fixture is already
// correct per the family convention. The tests below are the regression
// locks for that behavior: the void MS pixel maps to a void pan pixel in
// every band; every other pan pixel fuses from valid data only, with exact
// brovey values (4/6*10, 2/6*10). The fixture MUST carry the VRT
// GeoTransform — omitting it silently routes the fusion through the
// same-resolution clamp path and fabricates a fake defect (this is also
// what finding A14's guard now blocks).
// ===========================================================================

TEST_CASE( "Streaming brovey resampling keeps a mild declared sentinel void without blending neighbors [r15a][nodata][resample]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_rs1.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "ms_rs1_raw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_rs1.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_rs1.tif" ) );

    // pan 4x4 @1m (uniform 10); MS 2x2 @2m — the MS is resampled onto the
    // pan grid. MS band 2 declares -1 and carries the void at the top-left
    // MS pixel; every other MS pixel is valid (2.0). The VRT MUST carry the
    // source GeoTransform (see the block comment above): without it GDAL
    // defaults the VRT to the identity transform and the fusion silently
    // takes the same-resolution path.
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 2.0, 0.0, 0.0, 0.0, -2.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 2, 2, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const std::vector<float> pan( 16, 10.0f );
    const std::vector<float> ms1 = { 4.0f, 4.0f, 4.0f, 4.0f };
    const std::vector<float> ms2 = { -1.0f, 2.0f, 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           const_cast<float *>( pan.data() ), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms1.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms2.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    {
        // Per-band NoData declarations ride a VRT wrapper (GTiff stores ONE
        // NoData tag per dataset; same strategy as the [r14a] hetero test).
        // Band 1 declares none; band 2 declares -1. The GeoTransform element
        // is what makes the fixture a realistic heterogeneous-resolution
        // pair — without it the VRT defaults to the identity transform.
        QFile vrt( msPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"2\">\n";
        ts << "  <GeoTransform>0.0, 2.0, 0.0, 0.0, 0.0, -2.0</GeoTransform>\n";
        ts << "  <SRS>EPSG:32648</SRS>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_rs1_raw.tif"
              "</SourceFilename><SourceBand>1</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
        ts << "    <NoDataValue>-1</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_rs1_raw.tif"
              "</SourceFilename><SourceBand>2</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "</VRTDataset>\n";
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    REQUIRE( out.width() == 4 );
    REQUIRE( out.height() == 4 );
    std::vector<float> out1( 16 ), out2( 16 );
    REQUIRE( out.readBandData( 1, out1.data(), 4, 4 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 4, 4 ) );

    // The pan pixel whose resampling kernel is dominated by the void MS
    // pixel is void in every band (index 0 in row-major 4x4). GDAL passes
    // the declared sentinel through at that pixel and the exact-match wash
    // in readMsWindow voids it — no finite blend survives.
    CHECK( std::isnan( out1[0] ) );
    CHECK( std::isnan( out2[0] ) );
    // Every other pan pixel fuses from valid data only:
    // out1 = 4/(4+2)*10, out2 = 2/(4+2)*10. GDAL's NoData-aware bilinear
    // excludes the void tap and renormalizes (family convention), so the
    // neighbors are NOT contaminated by the sentinel — the O1 hypothesis.
    for ( int i = 1; i < 16; ++i )
    {
        CHECK( out1[static_cast<size_t>( i )] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
        CHECK( out2[static_cast<size_t>( i )] == Approx( ( 2.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    }
    // No physically impossible negative reflectance survives anywhere.
    for ( size_t i = 0; i < out1.size(); ++i )
    {
        if ( !std::isnan( out1[i] ) )
            CHECK( out1[i] >= 0.0f );
        if ( !std::isnan( out2[i] ) )
            CHECK( out2[i] >= 0.0f );
    }
}

TEST_CASE( "Streaming brovey resampling with the classic -9999 sentinel keeps the void pixel void [r15a][nodata][resample]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_rs2.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "ms_rs2_raw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_rs2.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_rs2.tif" ) );

    // Same geometry as the mild-sentinel case with the classic -9999
    // declaration. The behavior is sentinel-agnostic: the void MS pixel
    // maps to a void pan pixel, the neighbors renormalize to exact values,
    // and #611's negative-sum guard never engages (the valid sum is 4+2=6).
    // Coverage note (review R2-1): for THIS sentinel the negative-sum guard
    // would also void pixel 0 if the exact-match wash were removed (sum
    // 4-9999 <= 1e-10), so this lock is wash-removal-blind by construction —
    // the mild-sentinel lock above carries that sensitivity (its blended
    // sum stays positive). This lock pins the classic sentinel's end state.
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 2.0, 0.0, 0.0, 0.0, -2.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 2, 2, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const std::vector<float> pan( 16, 10.0f );
    const std::vector<float> ms1 = { 4.0f, 4.0f, 4.0f, 4.0f };
    const std::vector<float> ms2 = { -9999.0f, 2.0f, 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           const_cast<float *>( pan.data() ), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms1.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms2.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    {
        QFile vrt( msPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"2\">\n";
        ts << "  <GeoTransform>0.0, 2.0, 0.0, 0.0, 0.0, -2.0</GeoTransform>\n";
        ts << "  <SRS>EPSG:32648</SRS>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_rs2_raw.tif"
              "</SourceFilename><SourceBand>1</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
        ts << "    <NoDataValue>-9999</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">ms_rs2_raw.tif"
              "</SourceFilename><SourceBand>2</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "</VRTDataset>\n";
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    std::vector<float> out1( 16 ), out2( 16 );
    REQUIRE( out.readBandData( 1, out1.data(), 4, 4 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 4, 4 ) );

    CHECK( std::isnan( out1[0] ) );
    CHECK( std::isnan( out2[0] ) );
    for ( int i = 1; i < 16; ++i )
    {
        CHECK( out1[static_cast<size_t>( i )] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
        CHECK( out2[static_cast<size_t>( i )] == Approx( ( 2.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    }
}

// The SAME corrected fixture on the quality-report path: the fidelity
// statistics compare the fused output against the MS reference resampled
// onto the pan grid (processNativeFusion's second readBandWindowScaled
// call). The void pan pixel is excluded from the statistics in both the
// fused and the reference read, so the per-band mean ratios equal the
// valid-pixel truth exactly: band 1 fuses to 4/6*10 against a clean
// reference of 4.0, band 2 to 2/6*10 against a reference of 2.0 — both
// ratios are 5/3.
TEST_CASE( "Streaming brovey qualityReport excludes the void pixel from fidelity statistics [r15a][nodata][resample]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "q_pan.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "q_ms_raw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "q_ms.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "q_fused.tif" ) );
    const QString reportPath = dir.filePath( QStringLiteral( "q_report.json" ) );

    // Same fixture as the mild-sentinel case above: pan 4x4 @1m (uniform
    // 10), MS 2x2 @2m with band 2 declaring -1 and carrying the void at the
    // top-left MS pixel; band 1 is uniformly 4.0 with no declaration.
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 2.0, 0.0, 0.0, 0.0, -2.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 2, 2, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const std::vector<float> pan( 16, 10.0f );
    const std::vector<float> ms1 = { 4.0f, 4.0f, 4.0f, 4.0f };
    const std::vector<float> ms2 = { -1.0f, 2.0f, 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           const_cast<float *>( pan.data() ), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms1.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms2.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    {
        QFile vrt( msPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"2\">\n";
        ts << "  <GeoTransform>0.0, 2.0, 0.0, 0.0, 0.0, -2.0</GeoTransform>\n";
        ts << "  <SRS>EPSG:32648</SRS>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">q_ms_raw.tif"
              "</SourceFilename><SourceBand>1</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
        ts << "    <NoDataValue>-1</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">q_ms_raw.tif"
              "</SourceFilename><SourceBand>2</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "</VRTDataset>\n";
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    params.qualityReportPath = reportPath;
    rs::fusion::FusionQualityReport report;
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params,
                                               &error, &report ) );
    REQUIRE( error.isEmpty() );
    REQUIRE( QFile::exists( reportPath ) );

    // Only the 15 valid pan pixels contribute to the per-band statistics:
    // band 1 fuses to 4/6*10 against a clean reference of 4.0, band 2 to
    // 2/6*10 against a reference of 2.0 — both ratios are exactly 5/3.
    REQUIRE( report.meanRatio.size() == 2 );
    CHECK( report.meanRatio[0] == Approx( ( 4.0 / 6.0 * 10.0 ) / 4.0 ).margin( 2e-3 ) );
    CHECK( report.meanRatio[1] == Approx( ( 2.0 / 6.0 * 10.0 ) / 2.0 ).margin( 2e-3 ) );
}

// ===========================================================================
// R15-A (A14): asymmetric georeferencing is not co-registered.
//
// compareGrids returns NO issues when either side lacks a geotransform
// (raster_grid_compat.cpp: "Both unreferenced: not spatially comparable.
// The caller falls back to dimension checks") — the documented fallback
// covers BOTH-unreferenced synthetic rasters. But the fusion derives its
// resample ratio from the geotransforms: a GT-less raster reports GDAL's
// DEFAULT IDENTITY transform, so a pair where exactly one side is
// referenced silently routes through the same-resolution clamp path
// (readBandWindow clamps the pan-grid window to the smaller MS raster and
// pads the remainder with the declared sentinel) and voids most of the
// output with NO error — the diag4 evidence that re-qualified O1.
//
// Exactly-one-side-referenced must be refused; the both-unreferenced
// synthetic fallback must keep working.
// ===========================================================================

TEST_CASE( "ImageFusion rejects a pan/MS pair where exactly one side lacks a geotransform [r15a][a14]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "a14_pan.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "a14_ms_raw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "a14_ms.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "a14_fused.tif" ) );

    // pan 4x4 @1m EPSG:32648 (uniform 10); MS 2x2 @2m EPSG:32648 with band 2
    // declaring -1. The MS VRT carries the SRS (so the CRS check passes) but
    // NOT the GeoTransform — as a GDAL-produced VRT never would, but as a
    // hand-written or stripped wrapper can.
    std::array<double, 6> panGt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::array<double, 6> msGt = { 0.0, 2.0, 0.0, 0.0, 0.0, -2.0 };
    GDALDatasetH panDs = createOutputTiff( panPath, 4, 4, 1, GDT_Float32, panGt,
                                           QStringLiteral( "EPSG:32648" ) );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 2, 2, GDT_Float32, msGt,
                                          QStringLiteral( "EPSG:32648" ) );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const std::vector<float> pan( 16, 10.0f );
    const std::vector<float> ms1 = { 4.0f, 4.0f, 4.0f, 4.0f };
    const std::vector<float> ms2 = { -1.0f, 2.0f, 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 4, 4,
                           const_cast<float *>( pan.data() ), 4, 4, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms1.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms2.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    {
        // SRS yes (CRS check passes), GeoTransform NO (the A14 gap).
        QFile vrt( msPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"2\">\n";
        ts << "  <SRS>EPSG:32648</SRS>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">a14_ms_raw.tif"
              "</SourceFilename><SourceBand>1</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n";
        ts << "    <NoDataValue>-1</NoDataValue>\n";
        ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">a14_ms_raw.tif"
              "</SourceFilename><SourceBand>2</SourceBand></SimpleSource>\n";
        ts << "  </VRTRasterBand>\n";
        ts << "</VRTDataset>\n";
    }

    // Sanity: the fixture really is the asymmetric case (pan referenced,
    // MS not) — otherwise this test would silently prove nothing.
    {
        GdalDatasetWrapper probe;
        REQUIRE( probe.open( msPath ) );
        CHECK_FALSE( probe.hasGeoTransform() );
        GdalDatasetWrapper panProbe;
        REQUIRE( panProbe.open( panPath ) );
        CHECK( panProbe.hasGeoTransform() );
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    CHECK_FALSE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    CHECK( error.contains( QStringLiteral( "co-registered" ) ) );
    CHECK( error.contains( QStringLiteral( "geotransform" ) ) );
    CHECK_FALSE( QFile::exists( outputPath ) );
}

TEST_CASE( "ImageFusion still accepts a both-unreferenced synthetic pair [r15a][a14]",
           "[fusion][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // The documented compareGrids fallback: NEITHER side referenced ->
    // dimension-checked same-resolution read. Both rasters ride VRT
    // wrappers without GeoTransform/SRS (the only way to produce a
    // GT-less dataset in this fixture family); both are 2x2 so the
    // same-resolution read is well-defined.
    const QString panRawPath = dir.filePath( QStringLiteral( "a14b_pan_raw.tif" ) );
    const QString msRawPath = dir.filePath( QStringLiteral( "a14b_ms_raw.tif" ) );
    const QString panPath = dir.filePath( QStringLiteral( "a14b_pan.vrt" ) );
    const QString msPath = dir.filePath( QStringLiteral( "a14b_ms.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "a14b_fused.tif" ) );

    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH panDs = createOutputTiff( panRawPath, 2, 2, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msRawPath, 2, 2, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );
    const std::vector<float> pan = { 10.0f, 10.0f, 10.0f, 10.0f };
    const std::vector<float> ms1 = { 4.0f, 4.0f, 4.0f, 4.0f };
    const std::vector<float> ms2 = { 2.0f, 2.0f, 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( pan.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms1.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( ms2.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );
    auto writeBareVrt = [&]( const QString &path, int bands, const QString &rawName ) {
        QFile vrt( path );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream ts( &vrt );
        ts << "<VRTDataset rasterXSize=\"2\" rasterYSize=\"2\">\n";
        for ( int b = 1; b <= bands; ++b )
        {
            ts << "  <VRTRasterBand dataType=\"Float32\" band=\"" << b << "\">\n";
            ts << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">" << rawName
               << "</SourceFilename><SourceBand>" << b << "</SourceBand></SimpleSource>\n";
            ts << "  </VRTRasterBand>\n";
        }
        ts << "</VRTDataset>\n";
    };
    writeBareVrt( panPath, 1, QStringLiteral( "a14b_pan_raw.tif" ) );
    writeBareVrt( msPath, 2, QStringLiteral( "a14b_ms_raw.tif" ) );

    // Fixture sanity (review R2-4): both wrappers really are unreferenced —
    // otherwise a future GDAL change (e.g. VRTs inheriting the source
    // geotransform) would silently turn this into a both-referenced case
    // that no longer exercises the documented fallback.
    {
        GdalDatasetWrapper panProbe;
        REQUIRE( panProbe.open( panPath ) );
        CHECK_FALSE( panProbe.hasGeoTransform() );
        GdalDatasetWrapper msProbe;
        REQUIRE( msProbe.open( msPath ) );
        CHECK_FALSE( msProbe.hasGeoTransform() );
    }

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 2 );
    std::vector<float> out1( 4 ), out2( 4 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 2 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 2, 2 ) );
    for ( int i = 0; i < 4; ++i )
    {
        CHECK( out1[static_cast<size_t>( i )] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
        CHECK( out2[static_cast<size_t>( i )] == Approx( ( 2.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
    }
}

// ===========================================================================
// R18-A WP2: ±Inf inputs are washed at the fusion read boundary like any
// other invalid sample (band_math.cpp and the atmospheric streaming paths
// already treat !isfinite as void). Before this contract the read boundary
// washed only declared finite sentinels, so an Inf pan pixel leaked into the
// fused product (brovey: ±Inf output) and an Inf neighbour poisoned HPF box
// means (ms − Inf = −Inf two pixels away from the bad sample).
// ===========================================================================

TEST_CASE( "Streaming brovey washes an Inf pan pixel to NoData instead of emitting Inf [r18a][wp2]",
           "[fusion][gdal][r18a]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_inf.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_inf.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_inf.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> pan = { inf, 10.0f };
    std::vector<float> ms1 = { 4.0f, 4.0f };
    std::vector<float> ms2 = { 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 1,
                           ms2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> out1( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    CHECK( std::isnan( out1[0] ) ); // Inf pan pixel is a void, never ±Inf data
    CHECK( !std::isinf( out1[0] ) );
    // The clean pixel still fuses: (4/6)*10.
    CHECK( out1[1] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
}

TEST_CASE( "Streaming hpf does not poison neighbours of an Inf pan pixel [r18a][wp2]",
           "[fusion][gdal][r18a]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_inf_hpf.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_inf_hpf.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_inf_hpf.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 3x1 scene, pan centre Inf: the HPF box mean around it used to become
    // Inf, so the centre's NEIGHBOUR came out as ms − Inf = −Inf.
    GDALDatasetH panDs = createOutputTiff( panPath, 3, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 3, 1, 1, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> pan = { 10.0f, inf, 10.0f };
    std::vector<float> ms = { 100.0f, 100.0f, 100.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 3, 1,
                           pan.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 3, 1,
                           ms.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "hpf" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> out1( 3 );
    REQUIRE( out.readBandData( 1, out1.data(), 3, 1 ) );
    for ( int i = 0; i < 3; ++i )
    {
        // Every pixel is finite or NaN — an Inf sample never propagates as
        // ±Inf data into neighbours.
        CHECK( !std::isinf( out1[i] ) );
    }
}

TEST_CASE( "Streaming brovey washes an Inf MS pixel to NoData in every band [r18a][wp2]",
           "[fusion][gdal][r18a]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_msinf.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_msinf.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_msinf.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> pan = { 10.0f, 10.0f };
    std::vector<float> ms1 = { inf, 4.0f };
    std::vector<float> ms2 = { 2.0f, 2.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 1,
                           ms2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "brovey" );
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> out1( 2 ), out2( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    REQUIRE( out.readBandData( 2, out2.data(), 2, 1 ) );
    // A partially-invalid pixel is void in every band (#700 semantics applied
    // to Inf): never a band-1-only Inf product.
    CHECK( std::isnan( out1[0] ) );
    CHECK( std::isnan( out2[0] ) );
    CHECK( !std::isinf( out1[0] ) );
    CHECK( !std::isinf( out2[0] ) );
    CHECK( out1[1] == Approx( ( 4.0f / 6.0f ) * 10.0f ).margin( 1e-3 ) );
}

// ===========================================================================
// R18-A WP5: linearWeighted input guards. Every sibling kernel (brovey,
// pcaFusion, gramSchmidt) validates its band pointers; linearWeighted used to
// dereference msBands[b] for b < nBands with no null/size check (QVector OOB
// is UB), and clamped the per-band ms weight to [0,1] while letting an
// out-of-range panWeight pass through unclamped — an asymmetric contract.
// ===========================================================================

TEST_CASE( "Linear fusion returns empty on a null band pointer instead of UB [r18a][wp5]",
           "[fusion][r18a]" )
{
    constexpr int W = 2, H = 1;
    std::vector<float> b1 = { 1.0f, 2.0f };
    std::vector<float> pan = { 10.0f, 20.0f };
    QVector<const float *> ms = { b1.data(), nullptr };

    const auto result = ImageFusion::linearWeighted( ms, 2, pan.data(), W, H, NODATA,
                                                     {}, 0.5f );
    CHECK( result.isEmpty() );
}

TEST_CASE( "Linear fusion returns empty when nBands exceeds the msBands list [r18a][wp5]",
           "[fusion][r18a]" )
{
    constexpr int W = 2, H = 1;
    std::vector<float> b1 = { 1.0f, 2.0f };
    std::vector<float> pan = { 10.0f, 20.0f };
    QVector<const float *> ms = { b1.data() }; // one band handed to a 2-band call

    const auto result = ImageFusion::linearWeighted( ms, 2, pan.data(), W, H, NODATA,
                                                     {}, 0.5f );
    CHECK( result.isEmpty() );
}

TEST_CASE( "Linear fusion clamps an out-of-range panWeight like the ms weights [r18a][wp5]",
           "[fusion][r18a]" )
{
    constexpr int W = 2, H = 1;
    std::vector<float> b1 = { 1.0f, 2.0f };
    std::vector<float> pan = { 10.0f, 20.0f };
    QVector<const float *> ms = { b1.data() };

    // panWeight = 1.5 used to flow into out = msW*ms + 1.5*pan unclamped
    // while msW was clamped to [0,1]. Clamp both to the same domain.
    auto result = ImageFusion::linearWeighted( ms, 1, pan.data(), W, H, NODATA,
                                               {}, 1.5f );
    REQUIRE( result.size() == 1 );
    // Default ms weight = 1 - clamp(1.5) = 0 -> output tracks the pan only.
    CHECK( result[0][0] == Approx( 10.0f ).margin( 1e-4 ) );
    CHECK( result[0][1] == Approx( 20.0f ).margin( 1e-4 ) );

    auto resultNeg = ImageFusion::linearWeighted( ms, 1, pan.data(), W, H, NODATA,
                                                  {}, -0.5f );
    REQUIRE( resultNeg.size() == 1 );
    // Default ms weight = 1 - clamp(-0.5) = 1 -> output tracks the ms only.
    CHECK( resultNeg[0][0] == Approx( 1.0f ).margin( 1e-4 ) );
    CHECK( resultNeg[0][1] == Approx( 2.0f ).margin( 1e-4 ) );
}

// ===========================================================================
// R18-A WP6: distinguishable failure states. The streaming PCA path returned
// false with an EMPTY error string when the scene had zero valid pixels —
// callers could not tell "bad input" from anything else (Oracle-D: a
// recognizable state, never a silent pseudo-success).
// ===========================================================================

TEST_CASE( "Streaming PCA fusion reports why an all-NoData scene fails [r18a][wp6]",
           "[fusion][gdal][r18a]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_allnd.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_allnd.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_allnd.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 2, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 2, 2, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    const std::vector<float> allNd( 4, NODATA );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( allNd.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( panDs, 1 ), NODATA ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( allNd.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 2 ), GF_Write, 0, 0, 2, 2,
                           const_cast<float *>( allNd.data() ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( msDs, 1 ), NODATA ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( msDs, 2 ), NODATA ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "pca" );
    QString error;
    REQUIRE_FALSE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    CHECK( !error.isEmpty() ); // the refusal carries a reason
    CHECK( !QFile::exists( outputPath ) ); // no partial product staged
}

// R18-A review-R1 P2: the streaming linear route must clamp panWeight exactly
// like the in-memory kernel (linearWeighted).
TEST_CASE( "Streaming linear fusion clamps an out-of-range panWeight [r18a][r1]",
           "[fusion][gdal][r18a]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString panPath = dir.filePath( QStringLiteral( "pan_pw.tif" ) );
    const QString msPath = dir.filePath( QStringLiteral( "ms_pw.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "fused_pw.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH panDs = createOutputTiff( panPath, 2, 1, 1, GDT_Float32, gt, QString() );
    GDALDatasetH msDs = createOutputTiff( msPath, 2, 1, 1, GDT_Float32, gt, QString() );
    REQUIRE( panDs != nullptr );
    REQUIRE( msDs != nullptr );

    std::vector<float> pan = { 10.0f, 10.0f };
    std::vector<float> ms = { 1.0f, 1.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( panDs, 1 ), GF_Write, 0, 0, 2, 1,
                           pan.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( msDs, 1 ), GF_Write, 0, 0, 2, 1,
                           ms.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( panDs );
    GDALClose( msDs );

    ImageFusion::NativeFusionParams params;
    params.method = QStringLiteral( "linear" );
    params.panWeight = 1.5f; // clamped to 1.0: output tracks the pan only
    QString error;
    REQUIRE( ImageFusion::processNativeFusion( panPath, msPath, outputPath, params, &error ) );
    REQUIRE( error.isEmpty() );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> out1( 2 );
    REQUIRE( out.readBandData( 1, out1.data(), 2, 1 ) );
    CHECK( out1[0] == Approx( 10.0f ).margin( 1e-4 ) );
    CHECK( out1[1] == Approx( 10.0f ).margin( 1e-4 ) );
}


// ===========================================================================
// r21-A seam alignment — the in-memory kernels must agree with the
// streaming production path's validity statistics and non-finite policy.
// ===========================================================================

TEST_CASE( "PCA Fusion: per-band NoData keeps the joint-valid statistics (AG2-D1)",
           "[fusion][r21a]" )
{
    // Raster A: bands share the same NoData pixels -> joint set == per-band
    //            set (the only case the old code handled correctly).
    // Raster B: band 1 keeps pixel 2, band 2 keeps pixel 3 -> per-band means
    //            used to drift off the joint set and rotate PC1.
    // Metamorphic oracle: A and B hold IDENTICAL valid inputs on the joint
    // set (pixels 0/1), so with joint statistics their fused outputs at
    // those pixels must be identical.
    const int W = 2, H = 2, N = W * H;
    const float ND = NODATA;
    std::vector<float> b1A = { 1.0f, 2.0f, ND, ND };
    std::vector<float> b2A = { 1.0f, 2.0f, ND, ND };
    std::vector<float> b1B = { 1.0f, 2.0f, 3.0f, ND };
    std::vector<float> b2B = { 1.0f, 2.0f, ND, 4.0f };
    std::vector<float> pan = { 1.5f, 1.5f, 9.0f, 9.0f };

    QVector<const float *> bandsA = { b1A.data(), b2A.data() };
    QVector<const float *> bandsB = { b1B.data(), b2B.data() };
    const auto outA = ImageFusion::pcaFusion( bandsA, 2, pan.data(), W, H, ND );
    const auto outB = ImageFusion::pcaFusion( bandsB, 2, pan.data(), W, H, ND );
    REQUIRE( outA.size() == 2 );
    REQUIRE( outB.size() == 2 );

    // Joint-valid pixels carry identical fused values across A and B.
    for ( int b = 0; b < 2; ++b )
    {
        INFO( "band " << b );
        CHECK( outA[b][0] == Approx( outB[b][0] ).margin( 1e-4 ) );
        CHECK( outA[b][1] == Approx( outB[b][1] ).margin( 1e-4 ) );
    }
    // Pixels valid in only one band stay NoData in B (joint validity).
    for ( int b = 0; b < 2; ++b )
    {
        CHECK( outB[b][2] == NODATA );
        CHECK( outB[b][3] == NODATA );
    }
}

TEST_CASE( "Brovey: a non-finite band sample voids the whole pixel (AG2-D2)",
           "[fusion][r21a]" )
{
    const int W = 1, H = 3, N = W * H;
    const float inf = std::numeric_limits<float>::infinity();
    // Pixel 0: +Inf in blue; pixel 1: NaN in pan; pixel 2: fully valid.
    std::vector<float> r = { 50.0f, 50.0f, 50.0f };
    std::vector<float> g = { 50.0f, 50.0f, 50.0f };
    std::vector<float> b = { inf, 50.0f, 50.0f };
    std::vector<float> pan = { 300.0f, std::numeric_limits<float>::quiet_NaN(), 300.0f };

    QVector<const float *> msBands = { r.data(), g.data(), b.data() };
    const auto out = ImageFusion::brovey( msBands, 3, pan.data(), W, H, NODATA );
    REQUIRE( out.size() == 3 );
    // The streaming path washes ±Inf to NaN at read time, so the in-memory
    // kernel must treat them identically: NoData, never invented 0.0 black
    // bands (the old Inf/Inf=NaN path wrote 0 into the finite bands).
    for ( int band = 0; band < 3; ++band )
    {
        CHECK( out[band][0] == NODATA );
        CHECK( out[band][1] == NODATA );
        CHECK( out[band][2] == Approx( 100.0f ).margin( 1e-3 ) );
    }
}

TEST_CASE( "Linear weighted fusion: non-finite inputs become NoData (AG2-D2)",
           "[fusion][r21a]" )
{
    const int W = 1, H = 2, N = W * H;
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> ms = { inf, 10.0f };
    std::vector<float> pan = { 100.0f, 100.0f };

    QVector<const float *> msBands = { ms.data() };
    const QVector<float> weights = { 0.5f };
    const auto out = ImageFusion::linearWeighted( msBands, 1, pan.data(), W, H,
                                                  NODATA, weights, 0.5f );
    REQUIRE( out.size() == 1 );
    CHECK( out[0][0] == NODATA ); // 0.5*Inf + 0.5*100 used to publish +Inf
    CHECK( out[0][1] == Approx( 55.0f ).margin( 1e-4 ) );
}


// r21-A / AG2-F6 + R2 mutation-gap closure — histogramMatch must scale with
// the SAMPLE stddev (N-1), matching the streaming seam's StatsAccumulator.
// A 4-sample tile is where population (N) vs sample (N-1) differ visibly
// (sqrt(3/4) = 0.866); on large N they converge, which is why the drift
// survived without a small-tile oracle.
TEST_CASE( "histogramMatch uses the sample stddev, not the population one (AG2-F6)",
           "[fusion][r21a]" )
{
    // data has mean 2.5, population sd 1.118, sample sd 1.291 (4 samples).
    // ref  has mean 20,  population sd 11.18, sample sd 12.91.
    // scale = sdRef/sdData: population form 10.0, sample form 11.547.
    std::vector<float> data = { 1.0f, 2.0f, 3.0f, 4.0f };
    std::vector<float> ref  = { 10.0f, 20.0f, 30.0f, 40.0f };
    const float nd = -9999.0f;

    ImageFusion::histogramMatch( data.data(), data.size(), ref.data(), ref.size(), nd );

    // matched[i] = (data[i] - 2.5) * scale + 20. With the sample scale the
    // extremes land exactly on the reference extremes (both series have the
    // same standardized shape): 1 -> 10, 4 -> 40.
    CHECK( data[0] == Approx( 10.0f ).margin( 0.05f ) );
    CHECK( data[3] == Approx( 40.0f ).margin( 0.05f ) );
    // The population-scale form would give 10.0 + 1.5*... i.e. ~13.66 / 36.34.
    CHECK( data[0] != Approx( 13.66f ).margin( 0.5f ) );
}


// r21-A / R2 mutation-gap closure, rescoped by #1687: the fused dNBR loop
// must still void legacy UNDECLARED -9999-family band values (they would
// fabricate extreme dNBR change), but the protection now lives in the named
// legacyNegativeSentinelFree seam the dNBR branch applies — the shared
// bothNormalizable predicate is finite-only since #1687, because valid
// slightly-negative reflectances (atmospheric correction) must compute.
TEST_CASE( "dNBR fusion keeps the negative-sentinel rule (R2 M3)", "[fusion][r21a]" )
{
    const float nd = -9999.0f;

    // The dNBR seam rejects legacy undeclared sentinels ...
    CHECK( MathUtils::legacyNegativeSentinelFree( 0.5f, 0.3f ) );
    CHECK_FALSE( MathUtils::legacyNegativeSentinelFree( nd, 0.3f ) );
    CHECK_FALSE( MathUtils::legacyNegativeSentinelFree( 0.3f, nd ) );
    CHECK_FALSE( MathUtils::legacyNegativeSentinelFree( -999.0f, 0.3f ) );
    CHECK_FALSE( MathUtils::legacyNegativeSentinelFree(
        std::numeric_limits<float>::quiet_NaN(), 0.3f ) );
    CHECK_FALSE( MathUtils::legacyNegativeSentinelFree(
        std::numeric_limits<float>::infinity(), 0.3f ) );
    // ... while physically-bounded reflectances — including the equal
    // slightly-negative pairs atmospheric correction produces — pass.
    CHECK( MathUtils::legacyNegativeSentinelFree( -0.01f, -0.01f ) );
    CHECK( MathUtils::legacyNegativeSentinelFree( -1.0f, -1.0f ) );
    CHECK( MathUtils::legacyNegativeSentinelFree( 0.0f, 0.0f ) );

    // The shared predicate itself is finite-only (#1687): a finite -9999 is
    // normalizable — undeclared sentinels are the dNBR seam's concern, not
    // the kernel's guess.
    CHECK( MathUtils::bothNormalizable( nd, 0.3f ) );
    CHECK( MathUtils::bothNormalizable( -1.0f, -1.0f ) );
    CHECK( MathUtils::bothNormalizable( 0.0f, 0.0f ) );
    CHECK_FALSE( MathUtils::bothNormalizable(
        std::numeric_limits<float>::quiet_NaN(), 0.3f ) );
    CHECK_FALSE( MathUtils::bothNormalizable(
        std::numeric_limits<float>::infinity(), 0.3f ) );
}
