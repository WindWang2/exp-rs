// gdal_block_stream.cpp — Out-of-core streaming iterator for huge GeoTIFFs.
#include "gdal_block_stream.h"
#include "gdal_dataset_wrapper.h"

#include <algorithm>

GdalBlockStream::GdalBlockStream( const GdalDatasetWrapper &ds, int bandNum,
                                  int tileWidth, int tileHeight, int halo )
  : m_ds( ds )
  , m_bandNum( bandNum )
  , m_tileWidth( std::max( 1, tileWidth ) )
  , m_tileHeight( std::max( 1, tileHeight ) )
  , m_halo( std::max( 0, halo ) )
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
            t.halo = m_halo;
            t.bufferWidth = width + 2 * m_halo;
            t.bufferHeight = height + 2 * m_halo;
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

    if ( m_halo == 0 )
    {
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

    // Halo > 0: read with border margin and replicate-clamp edges
    const size_t maxBufPixels = static_cast<size_t>( m_tileWidth + 2 * m_halo ) * static_cast<size_t>( m_tileHeight + 2 * m_halo );
    std::vector<float> buffer( maxBufPixels );
    std::vector<float> readBuf( maxBufPixels );

    for ( const Tile &tile : m_tiles )
    {
        const int bufW = tile.bufferWidth;
        const int bufH = tile.bufferHeight;

        // Bounding box of valid pixels in the source raster
        const int readX = std::max( 0, tile.xOffset - m_halo );
        const int readY = std::max( 0, tile.yOffset - m_halo );
        const int readRight = std::min( m_rasterWidth, tile.xOffset + tile.width + m_halo );
        const int readBottom = std::min( m_rasterHeight, tile.yOffset + tile.height + m_halo );
        const int readW = readRight - readX;
        const int readH = readBottom - readY;

        if ( readW <= 0 || readH <= 0 )
            return false;

        if ( !m_ds.readBandWindow( m_bandNum, readX, readY, readW, readH, readBuf.data() ) )
            return false;

        // Destination offsets in the halo buffer
        const int dstStartX = ( tile.xOffset - m_halo < 0 ) ? ( -( tile.xOffset - m_halo ) ) : 0;
        const int dstStartY = ( tile.yOffset - m_halo < 0 ) ? ( -( tile.yOffset - m_halo ) ) : 0;

        // 1. Copy valid read rectangle into buffer
        for ( int r = 0; r < readH; ++r )
        {
            const float *srcRow = readBuf.data() + static_cast<size_t>( r ) * readW;
            float *dstRow = buffer.data() + static_cast<size_t>( dstStartY + r ) * bufW + dstStartX;
            std::copy( srcRow, srcRow + readW, dstRow );
        }

        // 2. Replicate horizontal borders (left and right margins) for valid rows
        for ( int r = dstStartY; r < dstStartY + readH; ++r )
        {
            float *row = buffer.data() + static_cast<size_t>( r ) * bufW;
            const float leftVal = row[dstStartX];
            for ( int x = 0; x < dstStartX; ++x )
                row[x] = leftVal;
            const float rightVal = row[dstStartX + readW - 1];
            for ( int x = dstStartX + readW; x < bufW; ++x )
                row[x] = rightVal;
        }

        // 3. Replicate vertical borders (top and bottom margins) across entire bufW
        for ( int r = 0; r < dstStartY; ++r )
        {
            const float *srcRow = buffer.data() + static_cast<size_t>( dstStartY ) * bufW;
            float *dstRow = buffer.data() + static_cast<size_t>( r ) * bufW;
            std::copy( srcRow, srcRow + bufW, dstRow );
        }
        for ( int r = dstStartY + readH; r < bufH; ++r )
        {
            const float *srcRow = buffer.data() + static_cast<size_t>( dstStartY + readH - 1 ) * bufW;
            float *dstRow = buffer.data() + static_cast<size_t>( r ) * bufW;
            std::copy( srcRow, srcRow + bufW, dstRow );
        }

        if ( !callback( tile, buffer.data() ) )
            return false;
    }
    return true;
}
