// tests/raster_bit_compare.h — Bit-exact raster comparison for serial regression anchors (ADR 0124)
#pragma once

#include <gdal.h>
#include <gdal_priv.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sicnu::testing {

/// Outcome of a bit-exact raster comparison. When not identical, `detail`
/// names the first differing property so failures read as diagnostics.
struct RasterBitCompareReport
{
    bool identical = false;
    std::string detail;
};

/**
 * Compares two rasters bit-exactly: grid size, band count, per-band data
 * type, geotransform, per-band nodata declaration, and raw pixel bytes.
 *
 * Descriptive metadata (creation options, compressor, timestamps) is
 * deliberately ignored: per ADR 0124 the serial, single-threaded result is
 * the regression anchor, and streaming/parallel re-implementations must
 * reproduce its pixels exactly (bit-exact grade) rather than its file
 * container bytes.
 */
inline RasterBitCompareReport compareRastersBitExact( const std::string &pathA,
                                                      const std::string &pathB )
{
    RasterBitCompareReport rep;

    GDALAllRegister();

    GDALDatasetH dsA = GDALOpen( pathA.c_str(), GA_ReadOnly );
    if ( !dsA )
    {
        rep.detail = "failed to open A: " + pathA;
        return rep;
    }
    GDALDatasetH dsB = GDALOpen( pathB.c_str(), GA_ReadOnly );
    if ( !dsB )
    {
        rep.detail = "failed to open B: " + pathB;
        GDALClose( dsA );
        return rep;
    }

    auto fail = [&]( const std::string &why ) {
        rep.detail = why;
        GDALClose( dsA );
        GDALClose( dsB );
        return rep;
    };

    const int wA = GDALGetRasterXSize( dsA );
    const int hA = GDALGetRasterYSize( dsA );
    const int wB = GDALGetRasterXSize( dsB );
    const int hB = GDALGetRasterYSize( dsB );
    if ( wA != wB || hA != hB )
        return fail( "size mismatch: " + std::to_string( wA ) + "x" + std::to_string( hA )
                     + " vs " + std::to_string( wB ) + "x" + std::to_string( hB ) );

    const int nBandsA = GDALGetRasterCount( dsA );
    const int nBandsB = GDALGetRasterCount( dsB );
    if ( nBandsA != nBandsB )
        return fail( "band count mismatch: " + std::to_string( nBandsA ) + " vs "
                     + std::to_string( nBandsB ) );

    double gtA[6] = {};
    double gtB[6] = {};
    GDALGetGeoTransform( dsA, gtA );
    GDALGetGeoTransform( dsB, gtB );
    for ( int i = 0; i < 6; ++i )
    {
        if ( gtA[i] != gtB[i] )
            return fail( "geotransform[" + std::to_string( i ) + "] mismatch: "
                         + std::to_string( gtA[i] ) + " vs " + std::to_string( gtB[i] ) );
    }

    const size_t nPixels = static_cast<size_t>( wA ) * static_cast<size_t>( hA );
    for ( int b = 1; b <= nBandsA; ++b )
    {
        GDALRasterBandH bandA = GDALGetRasterBand( dsA, b );
        GDALRasterBandH bandB = GDALGetRasterBand( dsB, b );
        const GDALDataType dtA = GDALGetRasterDataType( bandA );
        const GDALDataType dtB = GDALGetRasterDataType( bandB );
        if ( dtA != dtB )
            return fail( "band " + std::to_string( b ) + " dtype mismatch: "
                         + std::to_string( dtA ) + " vs " + std::to_string( dtB ) );

        int okA = 0;
        int okB = 0;
        const double ndA = GDALGetRasterNoDataValue( bandA, &okA );
        const double ndB = GDALGetRasterNoDataValue( bandB, &okB );
        const bool ndMatches = ( std::isnan( ndA ) && std::isnan( ndB ) ) || ( ndA == ndB );
        if ( okA != okB || ( okA && !ndMatches ) )
            return fail( "band " + std::to_string( b ) + " nodata mismatch" );

        const size_t sampleBytes = GDALGetDataTypeSizeBytes( dtA );
        const size_t bufBytes = nPixels * sampleBytes;
        std::vector<unsigned char> bufA( bufBytes );
        std::vector<unsigned char> bufB( bufBytes );

        if ( CE_None != GDALRasterIO( bandA, GF_Read, 0, 0, wA, hA,
                                      bufA.data(), wA, hA, dtA, 0, 0 ) )
            return fail( "band " + std::to_string( b ) + ": read failed on A" );
        if ( CE_None != GDALRasterIO( bandB, GF_Read, 0, 0, wB, hB,
                                      bufB.data(), wB, hB, dtB, 0, 0 ) )
            return fail( "band " + std::to_string( b ) + ": read failed on B" );

        if ( std::memcmp( bufA.data(), bufB.data(), bufBytes ) != 0 )
        {
            // Locate the first differing pixel so the message names a coordinate.
            for ( size_t p = 0; p < nPixels; ++p )
            {
                const unsigned char *pa = bufA.data() + p * sampleBytes;
                const unsigned char *pb = bufB.data() + p * sampleBytes;
                if ( std::memcmp( pa, pb, sampleBytes ) != 0 )
                    return fail( "band " + std::to_string( b ) + ": pixels differ at ("
                                 + std::to_string( static_cast<long long>( p % wA ) ) + ","
                                 + std::to_string( static_cast<long long>( p / wA ) ) + ")" );
            }
            return fail( "band " + std::to_string( b ) + ": bytes differ" );
        }
    }

    rep.identical = true;
    GDALClose( dsA );
    GDALClose( dsB );
    return rep;
}

} // namespace sicnu::testing
