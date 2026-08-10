// test_gdal_multiband_block_stream.cpp — multi-band streaming primitives tests.
//
// Verifies the new per-pixel-hyperspectral streaming helper (E1) reads the right
// BIP layout, handles edge tiles, and that the streaming output writer round-
// trips tile-written data identically to a whole-raster read. These primitives
// back the operator migration in Phase E2.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/gdal/gdal_multiband_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <gdal.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
// Build a multi-band raster where pixel (x,y,band b) value = b*1000 + y*W + x.
// This encodes band + position so the BIP layout is unambiguous to verify.
void buildEncodedRaster( const QString &path, int width, int height, int bands )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, width, height, bands, GDT_Float32, gt, QString() );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<size_t>( width ) * height );
    for ( int b = 1; b <= bands; ++b )
    {
        for ( int y = 0; y < height; ++y )
            for ( int x = 0; x < width; ++x )
                band[static_cast<size_t>( y ) * width + x] =
                    static_cast<float>( b * 1000 + y * width + x );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                               band.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
}
} // namespace

TEST_CASE( "GdalMultibandBlockStream: tile count + BIP values", "[block_stream][multiband]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "m.tif" ) );
    constexpr int W = 7, H = 5, B = 3;
    buildEncodedRaster( path, W, H, B );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalMultibandBlockStream stream( ds, B, 3, 3 );
    // 7/3=3 cols, 5/3=2 rows → 6 tiles.
    REQUIRE( stream.tileCount() == 6 );
    REQUIRE( stream.bandCount() == B );

    int tilesVisited = 0;
    bool ok = stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const int gx = tile.xOffset + x;
                const int gy = tile.yOffset + y;
                const size_t localIdx = static_cast<size_t>( y ) * tile.width + x;
                for ( int b = 0; b < B; ++b )
                {
                    const float got = bip[localIdx * B + b];
                    const float want = static_cast<float>( ( b + 1 ) * 1000 + gy * W + gx );
                    REQUIRE( got == Approx( want ).margin( 1e-4 ) );
                }
            }
        }
        ++tilesVisited;
        return true;
    } );
    REQUIRE( ok );
    REQUIRE( tilesVisited == 6 );
}

TEST_CASE( "GdalMultibandBlockStream: edge tile clamping + early abort", "[block_stream][multiband]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "m.tif" ) );
    constexpr int W = 5, H = 5, B = 2;
    buildEncodedRaster( path, W, H, B );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    GdalMultibandBlockStream stream( ds, B, 4, 4 );
    // Bottom-right edge tile (index 3): 1x1 at (4,4).
    const auto &edge = stream.tile( 3 );
    REQUIRE( edge.xOffset == 4 );
    REQUIRE( edge.yOffset == 4 );
    REQUIRE( edge.width == 1 );
    REQUIRE( edge.height == 1 );

    // Early-abort: callback returns false after the first tile.
    int visited = 0;
    bool ok = stream.forEach( [&]( const GdalMultibandBlockStream::Tile &, const float * ) {
        ++visited;
        return false; // abort
    } );
    REQUIRE_FALSE( ok ); // forEach reports abort
    REQUIRE( visited == 1 );
}

TEST_CASE( "GdalStreamingOutput: tile-written raster reads back identically", "[block_stream][streaming_output]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out.tif" ) );
    constexpr int W = 10, H = 8, B = 2;
    buildEncodedRaster( inPath, W, H, B );

    GdalDatasetWrapper inDs;
    REQUIRE( inDs.open( inPath ) );

    // Stream the input tile-by-tile into a new output (identity copy), one band
    // per tile, using the streaming writer.
    GdalStreamingOutput out( outPath, W, H, B, GDT_Float32, inDs.geoTransform(), inDs.projection() );
    REQUIRE( out.isOpen() );

    GdalMultibandBlockStream stream( inDs, B, 4, 4 );
    std::vector<float> bandTile( 4 * 4 );
    bool ok = stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
        const size_t tilePixels = static_cast<size_t>( tile.width ) * tile.height;
        for ( int b = 0; b < B; ++b )
        {
            for ( size_t p = 0; p < tilePixels; ++p )
                bandTile[p] = bip[p * B + b];
            if ( !out.writeTile( b + 1, tile, bandTile.data() ) )
                return false;
        }
        return true;
    } );
    REQUIRE( ok );
    out.close();

    // Read the output back whole and compare to the expected encoding.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outPath ) );
    REQUIRE( outDs.width() == W );
    REQUIRE( outDs.height() == H );
    REQUIRE( outDs.bandCount() == B );
    std::vector<float> band( static_cast<size_t>( W ) * H );
    for ( int b = 1; b <= B; ++b )
    {
        REQUIRE( outDs.readBandData( b, band.data(), W, H ) );
        for ( int y = 0; y < H; ++y )
            for ( int x = 0; x < W; ++x )
                REQUIRE( band[static_cast<size_t>( y ) * W + x] ==
                         Approx( static_cast<float>( b * 1000 + y * W + x ) ).margin( 1e-4 ) );
    }
}
