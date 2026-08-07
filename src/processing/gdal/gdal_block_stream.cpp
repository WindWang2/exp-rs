// gdal_block_stream.cpp — Out-of-core streaming iterator for huge GeoTIFFs.
#include "gdal_block_stream.h"
#include "gdal_dataset_wrapper.h"

#include <algorithm>

GdalBlockStream::GdalBlockStream( const GdalDatasetWrapper &ds, int bandNum,
                                  int tileWidth, int tileHeight )
  : m_ds( ds )
  , m_bandNum( bandNum )
  , m_tileWidth( std::max( 1, tileWidth ) )
  , m_tileHeight( std::max( 1, tileHeight ) )
  , m_rasterWidth( ds.width() )
  , m_rasterHeight( ds.height() )
{
    buildTiles();
}

void GdalBlockStream::buildTiles()
{
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

bool GdalBlockStream::forEach( const TileCallback &callback ) const
{
    if ( m_tiles.empty() )
        return false;

    const size_t maxPixels = static_cast<size_t>( m_tileWidth ) * static_cast<size_t>( m_tileHeight );
    std::vector<float> buffer( maxPixels );

    for ( const Tile &tile : m_tiles )
    {
        if ( !m_ds.readBandWindow( m_bandNum, tile.xOffset, tile.yOffset,
                                   tile.width, tile.height, buffer.data() ) )
            return false;
        if ( !callback( tile, buffer.data() ) )
            return false;
    }
    return true;
}
