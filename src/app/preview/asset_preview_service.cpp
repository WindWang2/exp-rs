/***************************************************************************
 * asset_preview_service.cpp — bounded async preview implementation
 ***************************************************************************/
#include "asset_preview_service.h"

#include "geospatial/raster/raster_reader.h"
#include "widgets/rs_scan_pool.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>

#include <qgsmaprenderercustompainterjob.h>
#include <qgsmaprendererjob.h>
#include <qgsmapsettings.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::app
{

namespace
{

/// Fits the raster aspect ratio into the target box (bounded edges). Never
/// upsamples: readWindowResampled refuses dst > window, so the scale factor
/// is clamped to 1.0 — small rasters preview at native size.
QSize fittedSize( int rasterWidth, int rasterHeight, const QSize &target )
{
  if ( rasterWidth <= 0 || rasterHeight <= 0 || target.isEmpty() )
    return QSize( 1, 1 );
  const double scaleX =
    std::min<double>( target.width(), PreviewLimits::kMaxEdgePixels ) / rasterWidth;
  const double scaleY =
    std::min<double>( target.height(), PreviewLimits::kMaxEdgePixels ) / rasterHeight;
  const double s = std::min( std::min( scaleX, scaleY ), 1.0 );
  return QSize( std::max( 1, static_cast<int>( rasterWidth * s + 0.5 ) ),
                std::max( 1, static_cast<int>( rasterHeight * s + 0.5 ) ) );
}

/// Display-policy NoData test (a display choice, not a data claim): declared
/// NoData matches by value; declared-NoData-NaN matches NaN; undeclared NaN
/// cannot be scaled, so it renders black like NoData.
bool isDisplayMissing( double v, const geo::BandInfo &band )
{
  if ( band.noDataIsNaN )
    return std::isnan( v );
  if ( band.hasNoData )
    return v == band.noDataValue;
  return std::isnan( v );
}

/// Robust per-band display range: min/max over samples that are neither NaN
/// nor declared NoData. A degenerate range renders as flat gray.
struct BandRange
{
  double min = 0.0;
  double max = 0.0;
  bool valid = false;
};

BandRange computeRange( const double *begin, std::size_t count, const geo::BandInfo &band )
{
  BandRange range;
  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::lowest();
  std::size_t seen = 0;
  for ( std::size_t i = 0; i < count; ++i )
  {
    const double v = begin[i];
    if ( std::isnan( v ) || isDisplayMissing( v, band ) )
      continue;
    min = std::min( min, v );
    max = std::max( max, v );
    ++seen;
  }
  if ( seen == 0 )
    return range;
  range.min = min;
  range.max = max;
  range.valid = true;
  return range;
}

quint8 scaleToByte( double v, const BandRange &range )
{
  if ( !range.valid || range.max <= range.min )
    return 128; // flat data → neutral gray, never a black box
  const double t = ( v - range.min ) / ( range.max - range.min );
  return static_cast<quint8>( std::clamp( t, 0.0, 1.0 ) * 255.0 + 0.5 );
}

} // namespace

PreviewRender renderRasterPreview( const QString &path, const QSize &targetSize,
                                   long long maxNativePixels )
{
  PreviewRender out;
  try
  {
    geo::RasterReader reader = geo::RasterReader::open( path.toStdString() );
    const geo::RasterMetadata &meta = reader.metadata();
    if ( meta.width <= 0 || meta.height <= 0 || meta.bandCount <= 0 )
    {
      out.status = PreviewRender::Status::Failed;
      out.error = QStringLiteral( "空栅格或波段数为 0" );
      return out;
    }

    // Bound worst-case read time on the shared scan pool: without overview
    // levels a preview would read every pixel of the raster at native
    // resolution. Beyond the pixel cap this refuses typed (honest) instead
    // of occupying a worker for seconds/minutes.
    if ( meta.overviewCount <= 0
         && static_cast<long long>( meta.width ) * meta.height > maxNativePixels )
    {
      out.status = PreviewRender::Status::Unsupported;
      out.error = QStringLiteral( "栅格 %1×%2 无内建金字塔，超出预览像素上限 %3（已拒绝以保证界面响应）" )
                    .arg( meta.width )
                    .arg( meta.height )
                    .arg( maxNativePixels );
      return out;
    }

    // Preview band set: RGB triple when available, else the first band.
    std::vector<int> bands;
    if ( meta.bandCount >= 3 )
      bands = { 1, 2, 3 };
    else
      bands = { 1 };

    const QSize dst = fittedSize( meta.width, meta.height, targetSize );
    const geo::RasterWindow window{ 0, 0, meta.width, meta.height };

    const int level =
      reader.selectOverview( 1, dst.width(), dst.height(), geo::OverviewPolicy::Nearest );
    const std::vector<double> values =
      reader.readWindowResampled( bands, window, dst.width(), dst.height(),
                                  level, geo::OverviewPolicy::Nearest, "nearest" );
    const std::size_t plane =
      static_cast<std::size_t>( dst.width() ) * static_cast<std::size_t>( dst.height() );
    if ( values.size() < bands.size() * plane )
    {
      out.status = PreviewRender::Status::Failed;
      out.error = QStringLiteral( "读取的像素不足（数据不完整）" );
      return out;
    }

    // Ranges are computed per band over the (small) resampled buffer — the
    // memory cost is bounded by the thumbnail size, not the raster size.
    std::vector<BandRange> ranges;
    ranges.reserve( bands.size() );
    for ( std::size_t b = 0; b < bands.size(); ++b )
      ranges.push_back( computeRange( values.data() + b * plane, plane,
                                      meta.bands.at( static_cast<std::size_t>( bands[b] ) - 1 ) ) );

    QImage image( dst.width(), dst.height(), QImage::Format_RGB32 );
    for ( int y = 0; y < dst.height(); ++y )
    {
      QRgb *line = reinterpret_cast<QRgb *>( image.scanLine( y ) );
      for ( int x = 0; x < dst.width(); ++x )
      {
        const std::size_t idx = static_cast<std::size_t>( y ) * dst.width() + x;
        if ( bands.size() == 3 )
        {
          // Each channel is masked by ITS band's NoData — a NoData in any
          // component blackens the pixel instead of stretching garbage.
          bool missing = false;
          for ( std::size_t b = 0; b < 3; ++b )
          {
            if ( isDisplayMissing( values[b * plane + idx],
                                   meta.bands.at( static_cast<std::size_t>( bands[b] ) - 1 ) ) )
            {
              missing = true;
              break;
            }
          }
          if ( missing )
          {
            line[x] = qRgb( 0, 0, 0 );
            continue;
          }
          line[x] = qRgb( scaleToByte( values[idx], ranges[0] ),
                          scaleToByte( values[plane + idx], ranges[1] ),
                          scaleToByte( values[2 * plane + idx], ranges[2] ) );
          continue;
        }
        if ( isDisplayMissing( values[idx],
                               meta.bands.at( static_cast<std::size_t>( bands[0] ) - 1 ) ) )
        {
          line[x] = qRgb( 0, 0, 0 );
          continue;
        }
        const quint8 g = scaleToByte( values[idx], ranges[0] );
        line[x] = qRgb( g, g, g );
      }
    }
    out.image = image;
    return out;
  }
  catch ( const std::exception &e )
  {
    out.status = PreviewRender::Status::Failed;
    out.error = QStringLiteral( "栅格读取失败：%1" ).arg( QString::fromUtf8( e.what() ) );
    return out;
  }
}

PreviewRender renderVectorPreview( const QString &path, const QSize &targetSize,
                                   long long maxFeatures )
{
  PreviewRender out;
  // Everything (layer construction, rendering, destruction) happens on the
  // calling worker thread: the layer is a thread-local object that never
  // touches the GUI or a project.
  QgsVectorLayer layer( path, QStringLiteral( "preview" ), QStringLiteral( "ogr" ) );
  if ( !layer.isValid() )
  {
    out.status = PreviewRender::Status::Failed;
    out.error = QStringLiteral( "矢量数据无法打开（驱动不支持或文件损坏）" );
    return out;
  }

  const long long count = layer.featureCount();
  if ( count > maxFeatures )
  {
    out.status = PreviewRender::Status::Unsupported;
    out.error = QStringLiteral( "要素数 %1 超出预览上限 %2（已拒绝以保证界面响应）" )
                  .arg( count )
                  .arg( maxFeatures );
    return out;
  }

  const QgsRectangle extent = layer.extent();
  if ( extent.isEmpty() || !extent.isFinite() )
  {
    out.status = PreviewRender::Status::Failed;
    out.error = QStringLiteral( "图层无有效范围（空图层）" );
    return out;
  }

  const QSize dst(
    std::clamp( targetSize.width(), 16, PreviewLimits::kMaxEdgePixels ),
    std::clamp( targetSize.height(), 16, PreviewLimits::kMaxEdgePixels ) );

  QgsMapSettings settings;
  settings.setLayers( { &layer } );
  settings.setDestinationCrs( layer.crs() );
  settings.setExtent( extent );
  settings.setOutputSize( dst );
  settings.setBackgroundColor( Qt::white );
  // Preview discipline: no labeling machinery — cheap, stateless.
  settings.setFlag( Qgis::MapSettingsFlag::DrawLabeling, false );

  QImage image( dst, QImage::Format_RGB32 );
  image.fill( Qt::white );
  QPainter painter( &image );
  QgsMapRendererCustomPainterJob job( settings, &painter );
  job.start();
  job.waitForFinished();
  painter.end();
  out.image = image;
  return out;
}

// ---------------------------------------------------------------------------
// AssetPreviewService
// ---------------------------------------------------------------------------

AssetPreviewService::AssetPreviewService( QObject *parent )
  : QObject( parent )
{
}

AssetPreviewService::~AssetPreviewService()
{
  // Workers only compute and marshal back through invokeMethod(this) —
  // queued calls into a destroyed service are dropped by Qt; the registry
  // below is only touched on this object's thread.
}

void AssetPreviewService::setPool( QThreadPool *pool )
{
  m_pool = pool;
}

QString AssetPreviewService::cacheKey( const Request &request )
{
  QFileInfo info( request.path );
  if ( !info.exists() )
  {
    // Missing files never collide with any existing file's identity.
    return QStringLiteral( "%1|%2|missing" )
      .arg( request.kind == Kind::Raster ? QStringLiteral( "r" ) : QStringLiteral( "v" ) )
      .arg( request.path );
  }
  // Identity: path + size + mtime — a replaced/rewritten file invalidates
  // the cached preview without any external revision plumbing.
  return QStringLiteral( "%1|%2|%3|%4|%5|%6" )
    .arg( request.kind == Kind::Raster ? QStringLiteral( "r" ) : QStringLiteral( "v" ) )
    .arg( request.path,
          QString::number( info.size() ),
          QString::number( info.lastModified().toMSecsSinceEpoch() ),
          QString::number( request.size.width() ),
          QString::number( request.size.height() ) );
}

quint64 AssetPreviewService::requestPreview( const Request &request, QObject *receiver,
                                             std::function<void( const Result & )> callback )
{
  Request normalized = request;
  normalized.path = QDir::fromNativeSeparators( normalized.path );
  const quint64 token = m_nextToken++;
  if ( receiver == this )
  {
    // Self-referential requests behave as receiver-less: the supersede map
    // would die with the service anyway, and registering it would make the
    // delivery gate drop every result (found in review B-1).
    receiver = nullptr;
  }
  if ( receiver )
  {
    // Receiver-liveness cleanup. Qt::UniqueConnection is intentionally NOT
    // used — it is a no-op for lambdas; dedupe via the map instead (one
    // destroyed-connection per receiver identity, established on the first
    // request only).
    if ( !m_latestByReceiver.contains( receiver ) )
    {
      connect( receiver, &QObject::destroyed, this, [this, receiver]()
      { m_latestByReceiver.remove( receiver ); } );
    }
    m_latestByReceiver[receiver] = token;
  }

  // Cache hit → deliver synchronously on the caller's thread (the receiver
  // lives on the caller's thread in every panel use).
  const QString key = cacheKey( normalized );
  const auto cached = m_cache.constFind( key );
  if ( cached != m_cache.constEnd() )
  {
    m_lru.removeAll( key );
    m_lru.append( key );
    if ( callback )
    {
      Result result;
      result.status = PreviewRender::Status::Ready;
      result.image = cached->image;
      result.path = request.path;
      result.kind = request.kind;
      result.requestedSize = request.size;
      result.fromCache = true;
      callback( result );
    }
    return token;
  }

  InFlight flight;
  flight.receiver = receiver;
  flight.hadReceiver = receiver != nullptr;
  flight.callback = std::move( callback );
  flight.request = normalized;
  m_inFlight[token] = std::move( flight );
  dispatch( token );
  return token;
}

void AssetPreviewService::dispatch( quint64 token )
{
  const auto it = m_inFlight.constFind( token );
  if ( it == m_inFlight.constEnd() )
    return;
  const QString path = it->request.path;
  const QSize size = it->request.size;
  const Kind kind = it->request.kind;

  QPointer<AssetPreviewService> self( this );
  QThreadPool &pool = m_pool ? *m_pool : RsScanPool::instance().pool();
  // Per-preview dispatch (not single-flight): with 2 pool workers, concurrent QGIS vector renders CAN overlap — each uses its own standalone layer/job/image.
  pool.start( [self, token, path, size, kind]()
  {
    // Worker side: pure computation — no service state, no widgets.
    const PreviewRender render = kind == Kind::Raster
                                   ? renderRasterPreview( path, size )
                                   : renderVectorPreview( path, size );
    if ( !self )
      return; // service died while queued: drop
    QMetaObject::invokeMethod(
      self.data(),
      [self, token, render]()
      {
        if ( self )
          self->onComputed( token, render );
      },
      Qt::QueuedConnection );
  } );
}

void AssetPreviewService::onComputed( quint64 token, const PreviewRender &render )
{
  const auto it = m_inFlight.find( token );
  if ( it == m_inFlight.end() )
    return; // canceled or already delivered
  const InFlight flight = *it;
  m_inFlight.erase( it );

  if ( m_canceled.contains( token ) )
  {
    m_canceled.remove( token );
    return;
  }
  if ( flight.hadReceiver )
  {
    // A dead receiver's request drops — never deliver into captured dead
    // state (QPointer nulling is the death signal).
    if ( !flight.receiver )
      return;
    const auto latest = m_latestByReceiver.constFind( flight.receiver.data() );
    if ( latest == m_latestByReceiver.constEnd() || *latest != token )
      return; // superseded by a newer request for this receiver
  }

  // Ready results populate the bounded cache (LRU).
  if ( render.status == PreviewRender::Status::Ready && !render.image.isNull() )
  {
    const QString key = cacheKey( flight.request );
    if ( !m_cache.contains( key ) )
    {
      CacheEntry entry;
      entry.image = render.image;
      entry.bytes = static_cast<qint64>( render.image.sizeInBytes() );
      m_cache[key] = entry;
      m_lru.append( key );
      m_cacheTotalBytes += entry.bytes;
      evictIfNeeded();
    }
  }

  if ( flight.callback )
  {
    Result result;
    result.status = render.status;
    result.image = render.image;
    result.error = render.error;
    result.path = flight.request.path;
    result.kind = flight.request.kind;
    result.requestedSize = flight.request.size;
    flight.callback( result );
  }
}

void AssetPreviewService::cancel( quint64 token )
{
  m_canceled.insert( token );
  if ( m_canceled.size() >= 1024 )
  {
    // Prune only tokens whose delivery already happened (not in flight) —
    // a wholesale clear could resurrect a still-live canceled request.
    for ( auto it = m_canceled.begin(); it != m_canceled.end(); )
    {
      if ( !m_inFlight.contains( *it ) )
        it = m_canceled.erase( it );
      else
        ++it;
    }
  }
}

void AssetPreviewService::setCacheLimits( int maxEntries, qint64 maxBytes )
{
  m_maxEntries = std::max( 1, maxEntries );
  m_maxBytes = std::max<qint64>( 1, maxBytes );
  evictIfNeeded();
}

int AssetPreviewService::cacheEntries() const
{
  return m_cache.size();
}

qint64 AssetPreviewService::cacheBytes() const
{
  return m_cacheTotalBytes;
}

void AssetPreviewService::clearCache()
{
  m_cache.clear();
  m_lru.clear();
  m_cacheTotalBytes = 0;
}

void AssetPreviewService::evictIfNeeded()
{
  while ( ( m_cacheTotalBytes > m_maxBytes || m_cache.size() > m_maxEntries )
          && !m_lru.isEmpty() )
  {
    const QString victim = m_lru.takeFirst();
    const auto it = m_cache.constFind( victim );
    if ( it != m_cache.constEnd() )
    {
      m_cacheTotalBytes -= it->bytes;
      m_cache.remove( victim );
    }
  }
}

} // namespace sicnu::app
