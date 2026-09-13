// tests/test_sar_complex.cpp — complex (SLC) raster contract
// (Advanced SAR / PolSAR / InSAR 10.0, package A).
//
// Known-answer checks on the channel declaration grammar, the complex
// scalar kernels, the CFloat32 band validation, and the ComplexBandTileStream
// round-trip (values, halo edge replication, invalid-sample normalization)
// plus the CFloat32 streaming write path.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#include "processing/algorithms/sar/sar_complex.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

using namespace sicnu::sar;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_complex";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

const AppInit appInit{};

constexpr int kWidth = 5;
constexpr int kHeight = 4;
constexpr float kSentinel = -1.0f;

/// Writes a two-band CFloat32 fixture: band1 pixel (x,y) = (x + 0.5, y),
/// band2 = (x, y + 0.5); band1 pixel (1,1) is the sentinel pair.
bool writeComplexFixture( const QString &path, bool declareSentinel )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), kWidth, kHeight, 2,
                                  GDT_CFloat32, nullptr );
    if ( !ds )
        return false;
    for ( int b = 1; b <= 2; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        if ( declareSentinel )
            GDALSetRasterNoDataValue( band, static_cast<double>( kSentinel ) );
        std::vector<std::complex<float>> values( static_cast<size_t>( kWidth ) * kHeight );
        for ( int y = 0; y < kHeight; ++y )
        {
            for ( int x = 0; x < kWidth; ++x )
            {
                const float re = ( b == 1 ) ? static_cast<float>( x ) + 0.5f
                                            : static_cast<float>( x );
                const float im = ( b == 1 ) ? static_cast<float>( y )
                                            : static_cast<float>( y ) + 0.5f;
                if ( b == 1 && x == 1 && y == 1 && declareSentinel )
                    values[static_cast<size_t>( y ) * kWidth + x] = { kSentinel, kSentinel };
                else
                    values[static_cast<size_t>( y ) * kWidth + x] = { re, im };
            }
        }
        if ( GDALRasterIO( band, GF_Write, 0, 0, kWidth, kHeight, values.data(), kWidth,
                           kHeight, GDT_CFloat32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

} // anonymous namespace

TEST_CASE( "pol channel token parsing", "[sar][complex]" )
{
    PolChannel c{};
    REQUIRE( parsePolChannel( QStringLiteral( "HH" ), &c ) );
    REQUIRE( c == PolChannel::Hh );
    REQUIRE( parsePolChannel( QStringLiteral( " vh " ), &c ) );
    REQUIRE( c == PolChannel::Vh );
    REQUIRE_FALSE( parsePolChannel( QStringLiteral( "XX" ), &c ) );
    REQUIRE( polChannelToString( PolChannel::Hv ) == QLatin1String( "HV" ) );
}

TEST_CASE( "channel declaration grammar", "[sar][complex]" )
{
    std::vector<PolChannel> channels;
    QString error;

    REQUIRE( parseChannelDeclaration( QStringLiteral( "HH;HV;VV" ), &channels, &error ) );
    REQUIRE( channels.size() == 3 );
    REQUIRE( channels[0] == PolChannel::Hh );
    REQUIRE( channels[1] == PolChannel::Hv );
    REQUIRE( channels[2] == PolChannel::Vv );

    REQUIRE( parseChannelDeclaration( QStringLiteral( "HH;HV;VH;VV" ), &channels, &error ) );
    REQUIRE( channels.size() == 4 );
    REQUIRE( channels[2] == PolChannel::Vh );

    REQUIRE_FALSE( parseChannelDeclaration( QStringLiteral( "HH;HH;VV" ), &channels, &error ) );
    REQUIRE( error.contains( QLatin1String( "duplicate" ) ) );

    REQUIRE_FALSE( parseChannelDeclaration( QStringLiteral( "HH;ZZ" ), &channels, &error ) );
    REQUIRE( error.contains( QLatin1String( "ZZ" ) ) );

    REQUIRE_FALSE( parseChannelDeclaration( QStringLiteral( ";" ), &channels, &error ) );
}

TEST_CASE( "complex scalar kernels — closed forms", "[sar][complex]" )
{
    const std::complex<float> s{ 3.0f, 4.0f };
    REQUIRE( complexPower( s ) == Approx( 25.0 ) );
    REQUIRE( complexAmplitude( s ) == Approx( 5.0 ) );
    REQUIRE( complexPhaseRad( s ) == Approx( std::atan2( 4.0, 3.0 ) ) );

    // Phase of a negative-real sample: atan2 domain (−π, π].
    REQUIRE( complexPhaseRad( std::complex<float>{ -1.0f, 0.0f } ) == Approx( M_PI ) );

    // Zero sample: phase undefined (NaN), power defined.
    const std::complex<float> zero{ 0.0f, 0.0f };
    REQUIRE( std::isnan( complexPhaseRad( zero ) ) );
    REQUIRE( complexPower( zero ) == Approx( 0.0 ) );

    // Non-finite components propagate NaN (never clamped).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE( std::isnan( complexPower( { nan, 1.0f } ) ) );
    REQUIRE( std::isnan( complexAmplitude( { 1.0f, nan } ) ) );
    REQUIRE( std::isnan( complexPhaseRad( { nan, nan } ) ) );
}

TEST_CASE( "complex band validation", "[sar][complex]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "fixture.tif" ) );
    REQUIRE( writeComplexFixture( path, /*declareSentinel=*/false ) );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    QString error;
    REQUIRE( validateComplexBands( ds, { 1, 2 }, &error ) );

    // Out of range.
    REQUIRE_FALSE( validateComplexBands( ds, { 3 }, &error ) );
    REQUIRE( error.contains( QLatin1String( "out of range" ) ) );

    // Duplicate mapping.
    REQUIRE_FALSE( validateComplexBands( ds, { 1, 1 }, &error ) );
    REQUIRE( error.contains( QLatin1String( "twice" ) ) );

    // Empty.
    REQUIRE_FALSE( validateComplexBands( ds, {}, &error ) );

    ds.close();

    // Detected (float) input must be refused, not downcast.
    const QString floatPath = dir.filePath( QStringLiteral( "float.tif" ) );
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH fds = GDALCreate( driver, floatPath.toUtf8().constData(), kWidth, kHeight, 1,
                                   GDT_Float32, nullptr );
    REQUIRE( fds != nullptr );
    GDALClose( fds );

    GdalDatasetWrapper fdsWrap;
    REQUIRE( fdsWrap.open( floatPath ) );
    REQUIRE_FALSE( validateComplexBands( fdsWrap, { 1 }, &error ) );
    REQUIRE( error.contains( QLatin1String( "CFloat32" ) ) );
    REQUIRE_FALSE( isComplexBand( fdsWrap, 1 ) );
}

TEST_CASE( "complex tile stream — values, halo, invalid normalization", "[sar][complex]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "fixture.tif" ) );
    REQUIRE( writeComplexFixture( path, /*declareSentinel=*/true ) );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    SECTION( "halo-less stream preserves values and band layout" )
    {
        ComplexBandTileStream stream( ds, { 1, 2 }, 3, 2 );
        REQUIRE( stream.bandCount() == 2 );
        REQUIRE( stream.tileCount() == 4 ); // 5x4 on 3x2 tiles: 2 cols x 2 rows

        int visited = 0;
        REQUIRE( stream.forEach( [&]( const ComplexTile &tile,
                                       const std::complex<float> *bip ) {
            for ( int y = 0; y < tile.height; ++y )
            {
                for ( int x = 0; x < tile.width; ++x )
                {
                    const std::complex<float> b1 =
                        bip[( static_cast<size_t>( y ) * tile.bufferWidth + x ) * 2 + 0];
                    const std::complex<float> b2 =
                        bip[( static_cast<size_t>( y ) * tile.bufferWidth + x ) * 2 + 1];
                    const int gx = tile.xOffset + x;
                    const int gy = tile.yOffset + y;
                    if ( gx == 1 && gy == 1 )
                    {
                        REQUIRE( std::isnan( b1.real() ) );
                        REQUIRE( std::isnan( b1.imag() ) );
                    }
                    else
                    {
                        REQUIRE( b1.real() == Approx( static_cast<float>( gx ) + 0.5f ) );
                        REQUIRE( b1.imag() == Approx( static_cast<float>( gy ) ) );
                    }
                    REQUIRE( b2.real() == Approx( static_cast<float>( gx ) ) );
                    REQUIRE( b2.imag() == Approx( static_cast<float>( gy ) + 0.5f ) );
                }
            }
            ++visited;
            return true;
        } ) );
        REQUIRE( visited == 4 );
    }

    SECTION( "halo edge replication" )
    {
        ComplexBandTileStream stream( ds, { 1 }, 5, 4, /*halo=*/2 );
        REQUIRE( stream.tileCount() == 1 );
        REQUIRE( stream.forEach( [&]( const ComplexTile &tile,
                                       const std::complex<float> *bip ) {
            REQUIRE( tile.bufferWidth == kWidth + 4 );
            REQUIRE( tile.bufferHeight == kHeight + 4 );
            // Top-left halo pixel must replicate the (0,0) raster sample.
            const std::complex<float> corner = bip[0];
            REQUIRE( corner.real() == Approx( 0.5f ) );
            REQUIRE( corner.imag() == Approx( 0.0f ) );
            // Core pixel (1,1) is the sentinel → normalized NaN; its halo
            // replication is NaN as well.
            const size_t stride = static_cast<size_t>( tile.bufferWidth );
            const std::complex<float> sentinel =
                bip[( static_cast<size_t>( 1 + tile.halo ) * stride ) + ( 1 + tile.halo )];
            REQUIRE( std::isnan( sentinel.real() ) );
            // Bottom-right halo replicates the (4,3) sample.
            const std::complex<float> br =
                bip[( static_cast<size_t>( kHeight + 2 * tile.halo - 1 ) * stride )
                     + ( kWidth + 2 * tile.halo - 1 )];
            REQUIRE( br.real() == Approx( 4.5f ) );
            REQUIRE( br.imag() == Approx( 3.0f ) );
            return true;
        } ) );
    }

    SECTION( "callback abort short-circuits" )
    {
        ComplexBandTileStream stream( ds, { 1 }, 3, 2 );
        int visited = 0;
        REQUIRE_FALSE( stream.forEach( [&]( const ComplexTile &,
                                            const std::complex<float> * ) {
            ++visited;
            return false;
        } ) );
        REQUIRE( visited == 1 );
    }
}

TEST_CASE( "complex tile stream — multi-tile with halo matches the raster exactly",
           "[sar][complex]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "fixture.tif" ) );
    REQUIRE( writeComplexFixture( path, /*declareSentinel=*/true ) );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );

    // 5x4 raster on 2x2 tiles with halo 1: every core pixel must equal the
    // source sample and every halo pixel must equal the edge-replicated
    // source sample — the tile-seam mapping the ensemble kernels rely on.
    ComplexBandTileStream stream( ds, { 1, 2 }, 2, 2, /*halo=*/1 );
    REQUIRE( stream.tileCount() == 6 ); // ceil(5/2)=3 cols x ceil(4/2)=2 rows
    REQUIRE( stream.forEach( [&]( const ComplexTile &tile,
                                  const std::complex<float> *bip ) {
        for ( int y = 0; y < tile.bufferHeight; ++y )
        {
            for ( int x = 0; x < tile.bufferWidth; ++x )
            {
                const std::complex<float> b1 =
                    bip[( static_cast<size_t>( y ) * tile.bufferWidth + x ) * 2 + 0];
                // Raster position of this buffer cell, edge-clamped.
                const int gx = std::clamp( tile.xOffset - tile.halo + x, 0, kWidth - 1 );
                const int gy = std::clamp( tile.yOffset - tile.halo + y, 0, kHeight - 1 );
                if ( gx == 1 && gy == 1 )
                {
                    // Sentinel pair (clamped-to halo cells included): the
                    // whole sample is normalized to (NaN, NaN).
                    REQUIRE( std::isnan( b1.real() ) );
                    REQUIRE( std::isnan( b1.imag() ) );
                    continue;
                }
                REQUIRE( b1.real() == Approx( static_cast<float>( gx ) + 0.5f ) );
                REQUIRE( b1.imag() == Approx( static_cast<float>( gy ) ) );
            }
        }
        return true;
    } ) );
}

TEST_CASE( "complex streaming write round-trip", "[sar][complex]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "out.tif" ) );

    GdalStreamingOutput output( path, kWidth, kHeight, 1, GDT_CFloat32,
                                { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 }, QString() );
    REQUIRE( output.isOpen() );

    std::vector<std::complex<float>> values( static_cast<size_t>( kWidth ) * kHeight );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
            values[static_cast<size_t>( y ) * kWidth + x] =
                std::complex<float>{ static_cast<float>( x ), static_cast<float>( y ) };

    REQUIRE( writeComplexTile( output, 1, 0, 0, kWidth, kHeight, values.data() ) );
    REQUIRE( output.setBandNoDataValue( 1, 0.0 ) );
    output.close();
    REQUIRE( !output.isOpen() );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    REQUIRE( isComplexBand( ds, 1 ) );
    bool hasNodata = false;
    REQUIRE( ds.bandNoDataValue( 1, &hasNodata ) == Approx( 0.0 ) );
    REQUIRE( hasNodata );

    ComplexBandTileStream stream( ds, { 1 }, 4, 4 );
    REQUIRE( stream.forEach( [&]( const ComplexTile &tile,
                                  const std::complex<float> *bip ) {
        for ( int y = 0; y < tile.height; ++y )
            for ( int x = 0; x < tile.width; ++x )
            {
                const std::complex<float> v =
                    bip[static_cast<size_t>( y ) * tile.bufferWidth + x];
                if ( tile.xOffset + x == 0 && tile.yOffset + y == 0 )
                {
                    // Sample (0,0) is exactly (0, 0) — it matches the
                    // declared (0, 0) sentinel pair and MUST come back NaN.
                    REQUIRE( std::isnan( v.real() ) );
                    REQUIRE( std::isnan( v.imag() ) );
                    continue;
                }
                REQUIRE( v.real() == Approx( static_cast<float>( tile.xOffset + x ) ) );
                REQUIRE( v.imag() == Approx( static_cast<float>( tile.yOffset + y ) ) );
            }
        return true;
    } ) );
}
