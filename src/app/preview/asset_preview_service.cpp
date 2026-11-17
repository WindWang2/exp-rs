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
/// cannot be scaled, so it renders black like NoData. Non-finite samples
/// (±Inf) are not displayable either: letting one into the stretch collapses
/// every finite pixel to 0 and, worse, feeds NaN into the byte conversion
/// (UB). They are therefore a display miss — black, never a range input.
bool isDisplayMissing( double v, const geo::BandInfo &band )
{
  if ( !std::isfinite( v ) )
    return true;
  return band.hasNoData && geo::bandSentinelMatches( band, v );
}

/// Robust per-band display range: min/max over finite samples that are
/// neither NaN/±Inf nor declared NoData. A degenerate range renders as flat
/// gray.
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
    if ( !std::isfinite( v ) || isDisplayMissing( v, band ) )
      continue;
    min = std::min( min, v );
    max = std::max( max, v );
    ++seen;
  }
  if ( seen == 0 || !std::isfinite( min ) || !std::isfinite( max ) )
    return range;
  range.min = min;
  range.max = max;
  range.valid = true;
  return range;
}

quint8 scaleToByte( double v, const BandRange &range )
{
  // Belt and braces: the callers mask non-finite samples already, but a
  // float→integer conversion of NaN is UB, so the conversion below must
  // never be reachable with a non-finite numerator/denominator.
  if ( !std::isfinite( v ) )
    return 0;
  if ( !range.valid || !( range.max > range.min ) )
    return 128; // flat data → neutral gray, never a black box
  // Direct form while it is well defined. It is NOT for a legitimate Float64
  // span such as {-1e308, +1e308}: max - min overflows to +Inf and every ratio
  // becomes NaN. The half-range form keeps the denominator finite for any
  // finite pair (and is only needed there — halving first would underflow a
  // subnormal-only range to a zero denominator, which the direct form handles).
  const double span = range.max - range.min;
  double t = 0.0;
  if ( std::isfinite( span ) && span > 0.0 )
  {
    t = ( v - range.min ) / span;
  }
  else
  {
    const double halfSpan = range.max * 0.5 - range.min * 0.5;
    if ( !( halfSpan > 0.0 ) )
      return 128; // unresolvable span (subnormal-only) → flat gray, never UB
    t = ( v * 0.5 - range.min * 0.5 ) / halfSpan;
  }
  if ( !std::isfinite( t ) )
    return 0;
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
      out.error = QObject::tr("empty raster or zero bands" );
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
      out.error = QObject::tr("The %1×%2 raster has no built-in pyramids and exceeds the preview pixel cap %3 (refused to keep the UI responsive)" )
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
      out.error = QObject::tr("too few pixels read (incomplete data)" );
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
    out.error = QObject::tr("Raster read failed: %1" ).arg( QString::fromUtf8( e.what() ) );
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
    out.error = QObject::tr("Cannot open the vector data (unsupported driver or corrupt file)" );
    return out;
  }

  const long long count = layer.featureCount();
  if ( count > maxFeatures )
  {
    out.status = PreviewRender::Status::Unsupported;
    out.error = QObject::tr("The feature count %1 exceeds the preview cap %2 (refused to keep the UI responsive)" )
                  .arg( count )
                  .arg( maxFeatures );
    return out;
  }

  const QgsRectangle extent = layer.extent();
  if ( extent.isEmpty() || !extent.isFinite() )
  {
    out.status = PreviewRender::Status::Failed;
    out.error = QObject::tr("layer has no valid extent (empty layer)" );
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
    // Missing files never collide with any existing file's identity — but
    // they still must not collide ACROSS target sizes (a future negative
    // cache would otherwise serve a 16x16 miss for a 256x256 request).
    return QStringLiteral( "%1|%2|missing|%3|%4" )
      .arg( request.kind == Kind::Raster ? QStringLiteral( "r" ) : QStringLiteral( "v" ) )
      .arg( request.path,
            QString::number( request.size.width() ),
            QString::number( request.size.height() ) );
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
    const auto previous = m_latestByReceiver.constFind( receiver );
    if ( previous != m_latestByReceiver.constEnd() && *previous != token )
    {
      // Superseded: stop the older request NOW (its queued worker returns
      // without reading pixels) instead of discovering it at delivery time.
      // A thousand rapid selections therefore leave ONE pending request, not
      // a thousand renders queued behind the one the user is waiting for.
      abortRequest( *previous );
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
  flight.originalPath = request.path;
  flight.cacheKey = key;
  flight.abort = QSharedPointer<QAtomicInt>::create( 0 );
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
  const QSharedPointer<QAtomicInt> abort = it->abort;

  QPointer<AssetPreviewService> self( this );
  QThreadPool &pool = m_pool ? *m_pool : RsScanPool::instance().pool();
  // Per-preview dispatch (not single-flight): with 2 pool workers, concurrent QGIS vector renders CAN overlap — each uses its own standalone layer/job/image.
  pool.start( [self, token, path, size, kind, abort]()
  {
    // Worker side: pure computation — no service state, no widgets. The
    // abort flag is shared with the service so a superseded/canceled request
    // costs a flag read instead of a full raster read.
    if ( abort->loadAcquire() != 0 )
      return;
    const PreviewRender render = kind == Kind::Raster
                                   ? renderRasterPreview( path, size )
                                   : renderVectorPreview( path, size );
    if ( abort->loadAcquire() != 0 )
      return;
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
    return; // canceled, superseded or already delivered
  const InFlight flight = *it;
  m_inFlight.erase( it );

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

  // Identity gate: the key was captured at request time, but the file may
  // have been rewritten while the worker ran. Admitting the pixels to the
  // cache is only honest while that captured identity still describes the
  // file — otherwise A's pixels would answer B's request (the preview would
  // show the wrong generation forever, not just once).
  bool identityChanged = false;
  if ( render.status == PreviewRender::Status::Ready && !render.image.isNull() )
  {
    identityChanged = cacheKey( flight.request ) != flight.cacheKey;
    if ( !identityChanged && !m_cache.contains( flight.cacheKey ) )
    {
      CacheEntry entry;
      entry.image = render.image;
      entry.bytes = static_cast<qint64>( render.image.sizeInBytes() );
      m_cache[flight.cacheKey] = entry;
      m_lru.append( flight.cacheKey );
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
    result.path = flight.originalPath;
    result.kind = flight.request.kind;
    result.requestedSize = flight.request.size;
    result.identityChanged = identityChanged;
    flight.callback( result );
  }
}

void AssetPreviewService::abortRequest( quint64 token )
{
  const auto it = m_inFlight.find( token );
  if ( it == m_inFlight.end() )
    return;
  if ( it->abort )
    it->abort->storeRelease( 1 );
  m_inFlight.erase( it );
}

void AssetPreviewService::cancel( quint64 token )
{
  abortRequest( token );
}

int AssetPreviewService::pendingRequests() const
{
  return m_inFlight.size();
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
