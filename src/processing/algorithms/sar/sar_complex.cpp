// src/processing/algorithms/sar/sar_complex.cpp — complex raster contract
// implementation. See sar_complex.h for the domain/identity contract.
#include "sar_complex.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

bool parsePolChannel( const QString &token, PolChannel *out )
{
    const QString t = token.trimmed().toUpper();
    if ( t == QLatin1String( "HH" ) ) { *out = PolChannel::Hh; return true; }
    if ( t == QLatin1String( "HV" ) ) { *out = PolChannel::Hv; return true; }
    if ( t == QLatin1String( "VH" ) ) { *out = PolChannel::Vh; return true; }
    if ( t == QLatin1String( "VV" ) ) { *out = PolChannel::Vv; return true; }
    return false;
}

QString polChannelToString( PolChannel channel )
{
    switch ( channel )
    {
        case PolChannel::Hh: return QStringLiteral( "HH" );
        case PolChannel::Hv: return QStringLiteral( "HV" );
        case PolChannel::Vh: return QStringLiteral( "VH" );
        case PolChannel::Vv: return QStringLiteral( "VV" );
    }
    return QString();
}

bool parseChannelDeclaration( const QString &value,
                              std::vector<PolChannel> *out,
                              QString *error )
{
    out->clear();
    const QStringList tokens = value.split( ';', Qt::SkipEmptyParts );
    if ( tokens.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "empty channel declaration (expected e.g. \"HH;HV;VV\")" );
        return false;
    }
    for ( const QString &token : tokens )
    {
        PolChannel c;
        if ( !parsePolChannel( token, &c ) )
        {
            if ( error )
                *error = QStringLiteral( "unknown polarimetric channel token \"%1\"" ).arg( token.trimmed() );
            return false;
        }
        if ( std::find( out->begin(), out->end(), c ) != out->end() )
        {
            if ( error )
                *error = QStringLiteral( "duplicate channel \"%1\" in declaration" )
                             .arg( polChannelToString( c ) );
            return false;
        }
        out->push_back( c );
    }
    return true;
}

bool isComplexBand( const GdalDatasetWrapper &ds, int band1based )
{
    return ds.bandDataType( band1based ) == GDT_CFloat32;
}

bool validateComplexBands( const GdalDatasetWrapper &ds,
                           const std::vector<int> &bands1based,
                           QString *error )
{
    if ( bands1based.empty() )
    {
        if ( error )
            *error = QStringLiteral( "no complex bands requested" );
        return false;
    }
    for ( const int band : bands1based )
    {
        if ( band < 1 || band > ds.bandCount() )
        {
            if ( error )
                *error = QStringLiteral( "band %1 out of range [1, %2]" )
                             .arg( band ).arg( ds.bandCount() );
            return false;
        }
        if ( !isComplexBand( ds, band ) )
        {
            if ( error )
                *error = QStringLiteral( "band %1 is not CFloat32 — complex kernels refuse "
                                         "detected/float inputs (declare the SLC properly or "
                                         "use the detected-domain operators)" )
                             .arg( band );
            return false;
        }
    }
    for ( size_t i = 0; i < bands1based.size(); ++i )
    {
        for ( size_t j = i + 1; j < bands1based.size(); ++j )
        {
            if ( bands1based[i] == bands1based[j] )
            {
                if ( error )
                    *error = QStringLiteral( "band %1 mapped twice" ).arg( bands1based[i] );
                return false;
            }
        }
    }
    return true;
}

bool complexSampleValid( const GdalDatasetWrapper &ds, int band1based,
                         std::complex<float> sample )
{
    const float re = sample.real();
    const float im = sample.imag();
    if ( !std::isfinite( re ) || !std::isfinite( im ) )
        return false;
    bool hasSentinel = false;
    const double sentinel = ds.bandNoDataValue( band1based, &hasSentinel );
    if ( hasSentinel
         && static_cast<double>( re ) == sentinel
         && static_cast<double>( im ) == sentinel )
        return false;
    return true;
}

double complexPower( std::complex<float> sample )
{
    const double re = static_cast<double>( sample.real() );
    const double im = static_cast<double>( sample.imag() );
    if ( !std::isfinite( re ) || !std::isfinite( im ) )
        return std::numeric_limits<double>::quiet_NaN();
    return re * re + im * im;
}

double complexAmplitude( std::complex<float> sample )
{
    const double power = complexPower( sample );
    if ( std::isnan( power ) )
        return power;
    return std::sqrt( power );
}

double complexPhaseRad( std::complex<float> sample )
{
    const double re = static_cast<double>( sample.real() );
    const double im = static_cast<double>( sample.imag() );
    if ( !std::isfinite( re ) || !std::isfinite( im ) || ( re == 0.0 && im == 0.0 ) )
        return std::numeric_limits<double>::quiet_NaN();
    return std::atan2( im, re );
}

ComplexBandTileStream::ComplexBandTileStream( const GdalDatasetWrapper &ds,
                                              const std::vector<int> &bands1based,
                                              int tileWidth, int tileHeight,
                                              int halo )
    : m_ds( ds )
    , m_bands( bands1based )
    , m_tileWidth( tileWidth )
    , m_tileHeight( tileHeight )
    , m_halo( halo )
    , m_rasterWidth( ds.width() )
    , m_rasterHeight( ds.height() )
{
    if ( m_tileWidth <= 0 )
        m_tileWidth = 256;
    if ( m_tileHeight <= 0 )
        m_tileHeight = 256;
    if ( m_halo < 0 )
        m_halo = 0;

    const int cols = ( m_rasterWidth + m_tileWidth - 1 ) / m_tileWidth;
    const int rows = ( m_rasterHeight + m_tileHeight - 1 ) / m_tileHeight;
    const int total = std::max( 0, cols ) * std::max( 0, rows );
    m_tiles.reserve( static_cast<size_t>( std::max( 0, total ) ) );

    int idx = 0;
    for ( int r = 0; r < rows; ++r )
    {
        const int yOffset = r * m_tileHeight;
        const int height = std::min( m_tileHeight, m_rasterHeight - yOffset );
        for ( int c = 0; c < cols; ++c )
        {
            const int xOffset = c * m_tileWidth;
            const int width = std::min( m_tileWidth, m_rasterWidth - xOffset );
            ComplexTile t;
            t.xOffset = xOffset;
            t.yOffset = yOffset;
            t.width = width;
            t.height = height;
            t.halo = m_halo;
            t.bufferWidth = width + 2 * m_halo;
            t.bufferHeight = height + 2 * m_halo;
            t.index = idx;
            t.totalTiles = total;
            m_tiles.push_back( t );
            ++idx;
        }
    }
}

bool ComplexBandTileStream::readTile( int index, std::complex<float> *pixelsBip ) const
{
    if ( index < 0 || index >= static_cast<int>( m_tiles.size() ) || m_bands.empty()
         || !pixelsBip )
        return false;

    const ComplexTile &tile = m_tiles[static_cast<size_t>( index )];
    const int bandCount = static_cast<int>( m_bands.size() );

    // Per-band sentinel snapshot (declared NoData; NaN = no sentinel match).
    std::vector<double> sentinels( m_bands.size(), std::numeric_limits<double>::quiet_NaN() );
    for ( size_t b = 0; b < m_bands.size(); ++b )
    {
        bool has = false;
        const double s = m_ds.bandNoDataValue( m_bands[b], &has );
        if ( has && std::isfinite( s ) )
            sentinels[b] = s;
    }

    const int bufW = tile.bufferWidth;
    const int bufH = tile.bufferHeight;

    // Bounding box of valid pixels in the source raster (halo clamped).
    const int readX = std::max( 0, tile.xOffset - m_halo );
    const int readY = std::max( 0, tile.yOffset - m_halo );
    const int readRight = std::min( m_rasterWidth, tile.xOffset + tile.width + m_halo );
    const int readBottom = std::min( m_rasterHeight, tile.yOffset + tile.height + m_halo );
    const int readW = readRight - readX;
    const int readH = readBottom - readY;
    if ( readW <= 0 || readH <= 0 )
        return false;

    std::fill( pixelsBip,
               pixelsBip + static_cast<size_t>( bufW ) * bufH * bandCount,
               std::complex<float>( kNaN, kNaN ) );

    std::vector<std::complex<float>> readBuf( static_cast<size_t>( readW ) * readH );
    for ( int b = 0; b < bandCount; ++b )
    {
        if ( !m_ds.readBandWindowNative( m_bands[b], readX, readY, readW, readH,
                                         readBuf.data() ) )
            return false;

        // Normalize invalid samples to (NaN, NaN) and scatter the valid
        // window into the halo buffer's band plane with edge replication
        // for the clamped halo margin. Buffer position (x, y) maps to
        // raster position (xOffset − halo + x, yOffset − halo + y); the
        // clamped read window starts at (readX, readY).
        const double sentinel = sentinels[b];
        for ( int y = 0; y < bufH; ++y )
        {
            const int srcY = std::clamp( y + ( tile.yOffset - m_halo ) - readY, 0, readH - 1 );
            for ( int x = 0; x < bufW; ++x )
            {
                const int srcX = std::clamp( x + ( tile.xOffset - m_halo ) - readX, 0, readW - 1 );
                std::complex<float> sample = readBuf[static_cast<size_t>( srcY ) * readW + srcX];
                const float re = sample.real();
                const float im = sample.imag();
                if ( !std::isfinite( re ) || !std::isfinite( im )
                     || ( static_cast<double>( re ) == sentinel
                          && static_cast<double>( im ) == sentinel ) )
                    sample = std::complex<float>( kNaN, kNaN );
                pixelsBip[( static_cast<size_t>( y ) * bufW + x ) * bandCount + b] = sample;
            }
        }
    }
    return true;
}

bool ComplexBandTileStream::forEach( const TileCallback &callback ) const
{
    if ( m_tiles.empty() || m_bands.empty() )
        return false;

    const int bandCount = static_cast<int>( m_bands.size() );
    const size_t maxBufPixels =
        static_cast<size_t>( m_tileWidth + 2 * m_halo ) *
        static_cast<size_t>( m_tileHeight + 2 * m_halo );
    std::vector<std::complex<float>> buffer( maxBufPixels * m_bands.size() );

    for ( size_t i = 0; i < m_tiles.size(); ++i )
    {
        if ( !readTile( static_cast<int>( i ), buffer.data() ) )
            return false;
        if ( !callback( m_tiles[i], buffer.data() ) )
            return false;
    }
    return true;
}

bool writeComplexTile( GdalStreamingOutput &output, int band1based,
                       int xOffset, int yOffset, int width, int height,
                       const std::complex<float> *pixels )
{
    GdalBlockStream::Tile tile;
    tile.xOffset = xOffset;
    tile.yOffset = yOffset;
    tile.width = width;
    tile.height = height;
    tile.halo = 0;
    tile.bufferWidth = width;
    tile.bufferHeight = height;
    return output.writeTileRaw( band1based, tile, pixels, GDT_CFloat32 );
}

} // namespace sicnu::sar
