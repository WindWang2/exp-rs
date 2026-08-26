// test_image_fusion.cpp — Phase 11.1: Image fusion tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/image_fusion.h"

#include <cmath>
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

