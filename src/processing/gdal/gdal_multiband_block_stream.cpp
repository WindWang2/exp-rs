// gdal_multiband_block_stream.cpp — multi-band out-of-core streaming iterator
// + streaming output writer.
#include "gdal_multiband_block_stream.h"
#include "gdal_dataset_wrapper.h"

#include <QDebug>
#include <QFile>
#include <algorithm>

// ---------------------------------------------------------------------------
// GdalMultibandBlockStream
// ---------------------------------------------------------------------------
GdalMultibandBlockStream::GdalMultibandBlockStream( const GdalDatasetWrapper &ds,
                                                    int bandCount,
                                                    int tileWidth, int tileHeight )
  : GdalMultibandBlockStream( ds, std::vector<int>{}, tileWidth, tileHeight )
{
    // Delegating ctor builds geometry with an empty band list; fill 1..bandCount.
    m_bandList.clear();
    const int n = std::max( 1, bandCount );
    m_bandList.reserve( n );
    for ( int b = 1; b <= n; ++b )
        m_bandList.push_back( b );
}

GdalMultibandBlockStream::GdalMultibandBlockStream( const GdalDatasetWrapper &ds,
                                                    const std::vector<int> &bandList,
                                                    int tileWidth, int tileHeight )
  : m_ds( ds )
  , m_bandList( bandList.empty() ? std::vector<int>{ 1 } : bandList )
  , m_tileWidth( std::max( 1, tileWidth ) )
  , m_tileHeight( std::max( 1, tileHeight ) )
  , m_rasterWidth( ds.width() )
  , m_rasterHeight( ds.height() )
{
    buildTiles();
}

void GdalMultibandBlockStream::buildTiles()
{
    m_tiles.clear();
    if ( m_rasterWidth <= 0 || m_rasterHeight <= 0 || m_bandList.empty() )
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
    const int bandCount = static_cast<int>( m_bandList.size() );
    if ( m_tiles.empty() || bandCount <= 0 )
        return false;

    const size_t tilePixels =
        static_cast<size_t>( m_tileWidth ) * static_cast<size_t>( m_tileHeight );
    std::vector<float> bip( tilePixels * static_cast<size_t>( bandCount ) ); // BIP window
    std::vector<float> bandTile;                                             // fallback scratch

    for ( const Tile &tile : m_tiles )
    {
        const size_t thisTilePixels = static_cast<size_t>( tile.width ) * tile.height;
        // Fast path: single dataset-level RasterIO directly into BIP memory layout.
        bool ok = m_ds.readWindowBip( m_bandList, tile.xOffset, tile.yOffset,
                                      tile.width, tile.height, bip.data() );
        if ( !ok )
        {
            // Fallback path: read band-by-band and scatter into BIP layout.
            if ( bandTile.size() < tilePixels )
                bandTile.resize( tilePixels );
            ok = true;
            for ( int bi = 0; bi < bandCount; ++bi )
            {
                const int bandNum = m_bandList[bi];
                if ( !m_ds.readBandWindow( bandNum, tile.xOffset, tile.yOffset,
                                           tile.width, tile.height, bandTile.data() ) )
                {
                    ok = false;
                    break;
                }
                for ( size_t p = 0; p < thisTilePixels; ++p )
                    bip[p * static_cast<size_t>( bandCount ) + bi] = bandTile[p];
            }
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
    : m_path( path )
{
    QString err;
    // createOutputTiff reports setter failures (georeference/projection) but
    // still returns the dataset; treat a reported error as fatal here so a
    // streaming writer never produces an unpositioned raster silently (#647).
    m_ds = createOutputTiff( path, width, height, bands, dtype, geoTransform, projection, &err );
    if ( m_ds && !err.isEmpty() )
    {
        GDALClose( m_ds );
        m_ds = nullptr;
    }
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

bool GdalStreamingOutput::setBandNoDataValue( int band, double nodata )
{
    if ( !m_ds )
        return false;
    GDALRasterBandH b = GDALGetRasterBand( m_ds, band );
    if ( !b )
        return false;
    return GDALSetRasterNoDataValue( b, nodata ) == CE_None;
}

bool GdalStreamingOutput::setNoDataValue( double nodata )
{
    if ( !m_ds )
        return false;
    int count = GDALGetRasterCount( m_ds );
    bool ok = true;
    for ( int i = 1; i <= count; ++i )
    {
        if ( !setBandNoDataValue( i, nodata ) )
            ok = false;
    }
    return ok;
}

void GdalStreamingOutput::close()
{
    if ( m_ds )
    {
        CPLErrorReset();
        if ( GDALFlushCache( m_ds ) != CE_None || CPLGetLastErrorType() == CE_Failure )
        {
            qWarning() << "GdalStreamingOutput::close flush error:" << CPLGetLastErrorMsg();
            CPLErrorReset();
        }
        CPLErrorReset();
        GDALClose( m_ds );
        if ( CPLGetLastErrorType() == CE_Failure )
        {
            qWarning() << "GdalStreamingOutput::close close error:" << CPLGetLastErrorMsg();
            CPLErrorReset();
        }
        m_ds = nullptr;
    }
    if ( m_removeOnClose )
        removeOutput();
}
void GdalStreamingOutput::removeOutput()
{
    if ( !m_path.isEmpty() )
        QFile::remove( m_path );
}

bool GdalStreamingOutput::closeWithError(QString *errorMessage)
{
    if ( !m_ds )
        return true;
    CPLErrorReset();
    if ( GDALFlushCache( m_ds ) != CE_None || CPLGetLastErrorType() == CE_Failure )
    {
        const char *msg = CPLGetLastErrorMsg();
        if ( errorMessage ) *errorMessage = msg ? QString::fromUtf8(msg) : QStringLiteral("Flush failed");
        CPLErrorReset();
        GDALClose( m_ds );
        m_ds = nullptr;
        removeOutput();
        return false;
    }
    CPLErrorReset();
    GDALClose( m_ds );
    if ( CPLGetLastErrorType() == CE_Failure )
    {
        const char *msg = CPLGetLastErrorMsg();
        if ( errorMessage ) *errorMessage = msg ? QString::fromUtf8(msg) : QStringLiteral("Close failed");
        CPLErrorReset();
        m_ds = nullptr;
        removeOutput();
        return false;
    }
    if ( m_removeOnClose )
    {
        // Abandoned mid-run: the dataset itself is fine, but the run that owns
        // it failed/cancelled - the partial result must not look like output (#647).
        m_ds = nullptr;
        removeOutput();
        return false;
    }
    m_ds = nullptr;
    return true;
}
