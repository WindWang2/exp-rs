// test_obia_segmentation.cpp — Shared OBIA segmentation helper tests
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_segmentation.h"

#include <gdal.h>

#include <QTemporaryDir>

static bool g_gdalInit = ( GDALAllRegister(), true );

static QString createTestRaster( const QString &dir, int w, int h )
{
    QString path = dir + "/seg_input.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    QVector<float> row( w );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            row[c] = ( c < w / 2 ) ? 50.0f : 150.0f;
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
    }

    GDALClose( ds );
    return path;
}

TEST_CASE( "ObiaSegmentation: isOtbAvailable is callable", "[obia][segmentation]" )
{
    // Availability depends on environment; call must not crash.
    (void) RsObiaSegmentation::isOtbAvailable();
    REQUIRE( true );
}

TEST_CASE( "ObiaSegmentation: built-in segmenter produces segments", "[obia][segmentation]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString inputPath = createTestRaster( tempDir.path(), 16, 16 );
    REQUIRE( !inputPath.isEmpty() );

    RsObiaSegmentationConfig cfg;
    cfg.rasterPath = inputPath;
    cfg.bandIndices = { 1 };
    cfg.preferOtb = false;
    cfg.smoothKernel = 3;
    cfg.quantizeBins = 4;
    cfg.minRegionSize = 10;

    const RsObiaSegmentationResult result = RsObiaSegmentation::run( cfg );
    REQUIRE( result.ok );
    REQUIRE( !result.usedOtb );
    REQUIRE( result.segMap.segmentCount() > 0 );
}

TEST_CASE( "ObiaSegmentation: preferOtb falls back when OTB unavailable", "[obia][segmentation]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString inputPath = createTestRaster( tempDir.path(), 16, 16 );
    REQUIRE( !inputPath.isEmpty() );

    RsObiaSegmentationConfig cfg;
    cfg.rasterPath = inputPath;
    cfg.bandIndices = { 1 };
    cfg.preferOtb = true;
    cfg.smoothKernel = 3;
    cfg.quantizeBins = 4;
    cfg.minRegionSize = 10;

    const RsObiaSegmentationResult result = RsObiaSegmentation::run( cfg );
    REQUIRE( result.ok );
    REQUIRE( result.segMap.segmentCount() > 0 );

    if ( !RsObiaSegmentation::isOtbAvailable() )
        REQUIRE( !result.usedOtb );
}

#endif // SICNU_HAS_OPENCV