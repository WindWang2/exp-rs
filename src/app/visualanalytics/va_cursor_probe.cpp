/***************************************************************************
 * va_cursor_probe.cpp — async hover sampling (newest-wins, stale-drop)
 ***************************************************************************/
#include "va_cursor_probe.h"

#include "widgets/rs_scan_pool.h"
#include "workbench/marshal_ui.h"

#include <QDateTime>
#include <QMetaType>
#include <QPointer>
#include <QTimer>

#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsexception.h>
#include <qgsproject.h>
#include <qgspointxy.h>
#include <qgsrasterlayer.h>

#include <cmath>

#include "geospatial/raster/raster_reader.h"

namespace sicnu::app::va
{

VaCursorProbe::VaCursorProbe( RasterProvider provider, QObject *parent )
    : QObject( parent )
    , m_provider( std::move( provider ) )
{
}

VaCursorProbe::~VaCursorProbe()
{
    cancel();
}

void VaCursorProbe::cancel()
{
    // Supersede the generation: the worker observes staleness on its next
    // poll and any queued completion is dropped by the generation check.
    RsScanPool::instance().cancel( m_generation.load( std::memory_order_relaxed ), this );
    RsScanPool::instance().nextGeneration( this );
    m_busy.store( false, std::memory_order_release );
    m_pending.valid = false;
}

void VaCursorProbe::request( const QgsPointXY &point, const QString &crsWkt, int band )
{
    if ( point.isEmpty() || band < 1 )
        return;
    // Newest-wins: fold this move into the pending request. An already
    // running job is left alone — its result is dropped as stale if a newer
    // generation starts before it delivers.
    if ( m_pending.valid )
        ++m_stats.coalesced;
    m_pending = PendingPoint{ true, point.x(), point.y(), crsWkt, band };
    ++m_stats.requests;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceStart = m_lastStartMs == 0 ? kDwellThrottleMs : now - m_lastStartMs;
    if ( m_busy.load( std::memory_order_acquire ) )
        return; // completion path drains the pending point
    if ( sinceStart < kDwellThrottleMs )
    {
        // Re-arm when the dwell window closes.
        QTimer::singleShot( kDwellThrottleMs - sinceStart, this,
                            [this] { startPending(); } );
        return;
    }
    startPending();
}

void VaCursorProbe::startPending()
{
    if ( !m_pending.valid )
        return;
    if ( m_busy.load( std::memory_order_acquire ) )
        return; // a newer start will drain it on completion
    const PendingPoint pending = m_pending;
    m_pending.valid = false;

    // GUI-thread provider query: resolve the layer NOW, keep only path +
    // transformed point. No layer pointer ever crosses to the pool thread.
    QgsRasterLayer *layer = m_provider ? m_provider() : nullptr;
    if ( !layer || !layer->isValid() )
        return;
    const QByteArray pathUtf8 = layer->source().toUtf8();
    const QgsCoordinateReferenceSystem layerCrs = layer->crs();

    QgsPointXY layerPoint( pending.x, pending.y );
    const QgsCoordinateReferenceSystem viewCrs( pending.crsWkt );
    if ( viewCrs.isValid() && layerCrs.isValid() && viewCrs != layerCrs )
    {
        try
        {
            QgsCoordinateTransform ct( viewCrs, layerCrs, QgsProject::instance() );
            layerPoint = ct.transform( layerPoint );
        }
        catch ( const QgsCsException & )
        {
            // Fail closed: an untransformable cursor is not sampled.
            emit sampled( false, 0.0, pending.band, false,
                          tr( "光标坐标无法变换到图层 CRS。" ) );
            return;
        }
    }
    const double x = layerPoint.x();
    const double y = layerPoint.y();
    const int band = std::max( 1, pending.band );

    cancel(); // supersede anything residual, mint the new generation
    const quint64 generation = RsScanPool::instance().nextGeneration( this );
    m_generation.store( generation, std::memory_order_relaxed );
    m_busy.store( true, std::memory_order_release );
    m_lastStartMs = QDateTime::currentMSecsSinceEpoch();

    const auto stale = [this, generation]() {
        return RsScanPool::instance().isStale( generation, this );
    };

    QPointer<VaCursorProbe> self( this );
    RsScanPool::instance().pool().start(
      [self, generation, pathUtf8, x, y, band, stale]() {
          // Pool thread: 1×1 read through the geospatial contract seam.
          QString message;
          bool ok = false;
          bool noData = false;
          double value = 0;
          try
          {
              auto reader = sicnu::geo::RasterReader::open( pathUtf8.constData() );
              const auto &meta = reader.metadata();
              if ( band > meta.bandCount )
              {
                  message = QStringLiteral( "波段超出范围。" );
              }
              else if ( !meta.hasGeotransform )
              {
                  message = QStringLiteral( "栅格没有地理变换，无法定位像素。" );
              }
              else if ( meta.geotransform[2] != 0.0 || meta.geotransform[4] != 0.0 )
              {
                  // Fail closed: the inverse below assumes a north-up
                  // affine; a rotated raster would silently sample the
                  // WRONG pixel (a wrong value is the one thing this probe
                  // must never deliver).
                  message = QStringLiteral( "旋转栅格不支持光标采样。" );
              }
              else
              {
                  // GDAL-order geotransform inverse (north-up affine).
                  const double gt0 = meta.geotransform[0];
                  const double gt1 = meta.geotransform[1];
                  const double gt3 = meta.geotransform[3];
                  const double gt5 = meta.geotransform[5];
                  if ( std::abs( gt1 ) < 1e-15 || std::abs( gt5 ) < 1e-15 )
                  {
                      message = QStringLiteral( "栅格分辨率退化，无法定位像素。" );
                  }
                  else
                  {
                      const double colF = ( x - gt0 ) / gt1;
                      const double rowF = ( y - gt3 ) / gt5;
                      const int col = static_cast<int>( std::floor( colF ) );
                      const int row = static_cast<int>( std::floor( rowF ) );
                      if ( col < 0 || row < 0 || col >= meta.width || row >= meta.height )
                      {
                          noData = true;
                          message = QStringLiteral( "光标在栅格范围外。" );
                      }
                      else if ( stale() )
                      {
                          return; // superseded mid-flight: nothing is delivered
                      }
                      else
                      {
                          const std::vector<double> values = reader.readWindow(
                            { band }, { col, row, 1, 1 } );
                          if ( values.size() == 1 )
                          {
                              const double raw = values.front();
                              const auto &bandInfo =
                                meta.bands.at( static_cast<size_t>( band - 1 ) );
                              const double scaled =
                                sicnu::geo::RasterReader::applyScaleOffset( bandInfo, raw );
                              const bool isNaN = std::isnan( scaled );
                              const bool isSentinel =
                                bandInfo.hasNoData && !bandInfo.noDataIsNaN
                                && std::abs( scaled - bandInfo.noDataValue ) <= 1e-9;
                              if ( isNaN || isSentinel )
                              {
                                  noData = true;
                                  message = QStringLiteral( "NoData" );
                              }
                              else
                              {
                                  ok = true;
                                  value = scaled;
                              }
                          }
                          else
                          {
                              message = QStringLiteral( "采样读取返回 %1 个值。" )
                                          .arg( values.size() );
                          }
                      }
                  }
              }
          }
          catch ( const std::exception &e )
          {
              message = QString::fromUtf8( e.what() );
          }

          ui_callback::marshalTo(
            self.data(), [self, generation, ok, noData, value, band, message]() {
                if ( !self )
                    return;
                if ( RsScanPool::instance().isStale( generation, self.data() ) )
                {
                    ++self->m_stats.staleDrops;
                    return;
                }
                self->m_busy.store( false, std::memory_order_release );
                ++self->m_stats.delivered;
                emit self->sampled( ok, value, band, noData, message );
                self->startPending(); // drain a request folded in meanwhile
            } );
      } );
}

} // namespace sicnu::app::va
