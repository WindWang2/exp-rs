// gdal_multiband_block_stream.cpp — multi-band out-of-core streaming iterator
// + streaming output writer.
#include "gdal_multiband_block_stream.h"
#include "gdal_dataset_wrapper.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// GdalMultibandBlockStream
// ---------------------------------------------------------------------------
GdalMultibandBlockStream::GdalMultibandBlockStream( const GdalDatasetWrapper &ds,
                                                    int bandCount,
                                                    int tileWidth, int tileHeight )
  : m_ds( ds )
  , m_bandCount( std::max( 1, bandCount ) )
  , m_tileWidth( std::max( 1, tileWidth ) )
  , m_tileHeight( std::max( 1, tileHeight ) )
  , m_rasterWidth( ds.width() )
  , m_rasterHeight( ds.height() )
{
    // Same row-major, edge-clamped grid as GdalBlockStream.
    m_tiles.clear();
    if ( m_rasterWidth <= 0 || m_rasterHeight <= 0 )
        return;
    const int cols = ( m_rasterWidth + m_tileWidth - 1 ) / m_tileWidth;
    const int rows = ( m_rasterHeight + m_tileHeight - 1 ) / m_tileHeight;
    m_tiles.reserve( static_cast<size_t>( cols ) * rows );
    int idx = 0;
    for ( int r = 0; r < rows; ++r )
    {
        const int yOffset = r * m_tileHeight;
        const int height = std::min( m_tileHeight, m_rasterHeight - yOffset );
        for ( int c = 0; c < cols; ++c )
        {
            const int xOffset = c * m_tileWidth;
            const int width = std::min( m_tileWidth, m_rasterWidth - xOffset );
            Tile t;
            t.xOffset = xOffset;
            t.yOffset = yOffset;
            t.width = width;
            t.height = height;
            t.index = idx;
            t.totalTiles = cols * rows;
            m_tiles.push_back( t );
            ++idx;
        }
    }
}

bool GdalMultibandBlockStream::forEach( const TileCallback &callback ) const
{
    if ( m_tiles.empty() || m_bandCount <= 0 )
        return false;

    const size_t tilePixels =
        static_cast<size_t>( m_tileWidth ) * static_cast<size_t>( m_tileHeight );
    std::vector<float> bandTile( tilePixels );                 // per-band scratch
    std::vector<float> bip( tilePixels * static_cast<size_t>( m_bandCount ) ); // BIP window

    for ( const Tile &tile : m_tiles )
    {
        const size_t thisTilePixels = static_cast<size_t>( tile.width ) * tile.height;
        // Read each band's window then scatter into the BIP layout.
        bool ok = true;
        for ( int b = 1; b <= m_bandCount; ++b )
        {
            if ( !m_ds.readBandWindow( b, tile.xOffset, tile.yOffset,
                                       tile.width, tile.height, bandTile.data() ) )
            {
                ok = false;
                break;
            }
            const size_t bandOff = static_cast<size_t>( b - 1 );
            for ( size_t p = 0; p < thisTilePixels; ++p )
                bip[p * static_cast<size_t>( m_bandCount ) + bandOff] = bandTile[p];
        }
        if ( !ok )
            return false;
        if ( !callback( tile, bip.data() ) )
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GdalStreamingOutput
// ---------------------------------------------------------------------------
GdalStreamingOutput::GdalStreamingOutput( const QString &path, int width, int height,
                                          int bands, int dtype,
                                          const std::array<double, 6> &geoTransform,
                                          const QString &projection )
{
    QString err;
    m_ds = createOutputTiff( path, width, height, bands, dtype, geoTransform, projection, &err );
}

GdalStreamingOutput::~GdalStreamingOutput()
{
    close();
}

bool GdalStreamingOutput::writeTile( int band, const GdalBlockStream::Tile &tile,
                                     const float *pixels )
{
    if ( !m_ds )
        return false;
    GDALRasterBandH b = GDALGetRasterBand( m_ds, band );
    if ( !b )
        return false;
    return GDALRasterIO( b, GF_Write, tile.xOffset, tile.yOffset, tile.width, tile.height,
                         const_cast<float *>( pixels ), tile.width, tile.height,
                         GDT_Float32, 0, 0 ) == CE_None;
}

void GdalStreamingOutput::close()
{
    if ( m_ds )
    {
        GDALFlushCache( m_ds );
        GDALClose( m_ds );
        m_ds = nullptr;
    }
}
