// test_gdal_block_stream.cpp — Out-of-core block-streaming iterator tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;

#include <QTemporaryDir>
#include <gdal.h>

#include <array>
#include <vector>

namespace
{
// Build a small single-band GeoTIFF whose pixel value = y * width + x.
void buildIndexRaster( const QString &path, int width, int height )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, width, height, 1, GDT_Float32, gt, QString() );
    REQUIRE( ds != nullptr );
    std::vector<float> data( static_cast<size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
            data[static_cast<size_t>( y ) * width + x] =
                static_cast<float>( y * width + x );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                           data.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
}
} // namespace

TEST_CASE( "GdalBlockStream: tile count matches width/height tiling", "[block_stream]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    buildIndexRaster( path, 10, 10 );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    // 10x10 raster with 4x4 tiles → ceil(10/4)=3 cols × 3 rows = 9 tiles.
    GdalBlockStream stream( ds, 1, 4, 4 );
    REQUIRE( stream.tileCount() == 9 );
    REQUIRE( stream.rasterWidth() == 10 );
    REQUIRE( stream.rasterHeight() == 10 );
    REQUIRE( stream.tileWidth() == 4 );
    REQUIRE( stream.tileHeight() == 4 );
}

TEST_CASE( "GdalBlockStream: edge tiles are clamped", "[block_stream]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    buildIndexRaster( path, 10, 10 );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalBlockStream stream( ds, 1, 4, 4 );

    // First tile: full 4x4 at (0,0).
    const auto &t0 = stream.tile( 0 );
    REQUIRE( t0.xOffset == 0 );
    REQUIRE( t0.yOffset == 0 );
    REQUIRE( t0.width == 4 );
    REQUIRE( t0.height == 4 );

    // Last tile (index 8): bottom-right, clamped to 2x2 at (8,8).
    const auto &t8 = stream.tile( 8 );
    REQUIRE( t8.xOffset == 8 );
    REQUIRE( t8.yOffset == 8 );
    REQUIRE( t8.width == 2 );
    REQUIRE( t8.height == 2 );
    REQUIRE( t8.totalTiles == 9 );
}

TEST_CASE( "GdalBlockStream: forEach visits every pixel once with correct values", "[block_stream]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    const int W = 7, H = 5;
    buildIndexRaster( path, W, H );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalBlockStream stream( ds, 1, 3, 3 );

    // Track coverage in a separate buffer.
    std::vector<int> hits( static_cast<size_t>( W ) * H, 0 );

    int tilesVisited = 0;
    bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const int gx = tile.xOffset + x;
                const int gy = tile.yOffset + y;
                const size_t localIdx = static_cast<size_t>( y ) * tile.width + x;
                const size_t globalIdx = static_cast<size_t>( gy ) * W + gx;
                // Value should equal gy * W + gx (the index raster encoding).
                REQUIRE( pixels[localIdx] == Approx( static_cast<float>( gy * W + gx ) ).margin( 1e-4 ) );
                ++hits[globalIdx];
            }
        }
        ++tilesVisited;
        return true;
    } );

    REQUIRE( ok );
    REQUIRE( tilesVisited == stream.tileCount() );

    // Every pixel must be covered exactly once (no gaps, no overlaps).
    for ( size_t i = 0; i < hits.size(); ++i )
        REQUIRE( hits[i] == 1 );
}

TEST_CASE( "GdalBlockStream: callback abort stops iteration", "[block_stream]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    buildIndexRaster( path, 9, 9 );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalBlockStream stream( ds, 1, 3, 3 ); // 3x3 = 9 tiles

    int visited = 0;
    bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &, const float * ) {
        ++visited;
        return visited < 3; // abort after the 3rd tile
    } );

    REQUIRE_FALSE( ok );            // aborted → false
    REQUIRE( visited == 3 );
}

TEST_CASE( "GdalBlockStream: tile covers whole raster when tile >= size", "[block_stream]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    buildIndexRaster( path, 4, 4 );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalBlockStream stream( ds, 1, 256, 256 ); // tile bigger than raster
    REQUIRE( stream.tileCount() == 1 );
    const auto &t = stream.tile( 0 );
    REQUIRE( t.width == 4 );
    REQUIRE( t.height == 4 );
}

TEST_CASE( "GdalBlockStream: halo buffering and border replication", "[block_stream][halo]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "r.tif" ) );
    const int W = 6, H = 6;
    buildIndexRaster( path, W, H ); // values: y * 6 + x

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    const int halo = 2;
    GdalBlockStream stream( ds, 1, 3, 3, halo );
    REQUIRE( stream.halo() == 2 );
    REQUIRE( stream.tileCount() == 4 ); // 6x6 with 3x3 tiles -> 2x2 = 4 tiles

    int visited = 0;
    bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
        REQUIRE( tile.halo == 2 );
        REQUIRE( tile.bufferWidth == tile.width + 2 * halo );
        REQUIRE( tile.bufferHeight == tile.height + 2 * halo );

        const int bufW = tile.bufferWidth;

        // Verify inner valid tile content
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const int gx = tile.xOffset + x;
                const int gy = tile.yOffset + y;
                const size_t bufIdx = static_cast<size_t>( y + halo ) * bufW + ( x + halo );
                REQUIRE( pixels[bufIdx] == Approx( static_cast<float>( gy * W + gx ) ).margin( 1e-4 ) );
            }
        }

        // For top-left tile (index 0 at 0,0): verify top-left halo is replicate-clamped from (0,0) = 0.0
        if ( tile.index == 0 )
        {
            // Top-left corner halo [0,0]
            REQUIRE( pixels[0] == Approx( 0.0f ).margin( 1e-4 ) );
            // Top halo at x = halo
            REQUIRE( pixels[halo] == Approx( 0.0f ).margin( 1e-4 ) );
            // Left halo at y = halo, x = 0
            REQUIRE( pixels[halo * bufW] == Approx( 0.0f ).margin( 1e-4 ) );
        }

        ++visited;
        return true;
    } );

    REQUIRE( ok );
    REQUIRE( visited == 4 );
}
