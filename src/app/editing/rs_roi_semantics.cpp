// rs_roi_semantics.cpp — see rs_roi_semantics.h.
#include "rs_roi_semantics.h"

#include "rs_edit_command_guard.h"
#include "rs_edit_session.h"

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgsproject.h>
#include <qgsrasterblock.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include "rs_pixel_rasterizer.h"

#include <cmath>

namespace
{

struct RasterWindow
{
    int col0 = 0;
    int row0 = 0;
    int width = 0;
    int height = 0;
    double gt[6] = { 0, 0, 0, 0, 0, 0 }; // window-local geotransform
};

/// Bounded window: ROI bbox ∩ raster extent, in full-raster pixel indices.
/// Returns false when the ROI does not touch the raster extent at all.
bool windowForGeometry( const QgsRasterLayer *raster, const QgsGeometry &geomInRasterCrs,
                        RasterWindow *win )
{
    const QgsRectangle extent = raster->extent();
    const int rasterW = raster->width();
    const int rasterH = raster->height();
    if ( rasterW <= 0 || rasterH <= 0 || extent.isEmpty() )
        return false;

    const double xres = raster->rasterUnitsPerPixelX();
    const double yres = raster->rasterUnitsPerPixelY();
    if ( xres <= 0 || yres <= 0 || std::isnan( xres ) || std::isnan( yres ) )
        return false;

    const QgsRectangle bbox = geomInRasterCrs.boundingBox().intersect( extent );
    if ( bbox.isEmpty() || bbox.width() < 0 || bbox.height() < 0 )
        return false;

    const int col0 = std::max( 0, static_cast<int>( std::floor( ( bbox.xMinimum() - extent.xMinimum() ) / xres ) ) );
    const int row0 = std::max( 0, static_cast<int>( std::floor( ( extent.yMaximum() - bbox.yMaximum() ) / yres ) ) );
    const int col1 = std::min( rasterW - 1, static_cast<int>( std::ceil( ( bbox.xMaximum() - extent.xMinimum() ) / xres ) ) );
    const int row1 = std::min( rasterH - 1, static_cast<int>( std::ceil( ( extent.yMaximum() - bbox.yMinimum() ) / yres ) ) );
    if ( col1 < col0 || row1 < row0 )
        return false;

    win->col0 = col0;
    win->row0 = row0;
    win->width = col1 - col0 + 1;
    win->height = row1 - row0 + 1;
    // Window geotransform: same pixel size, origin at the window's first
    // pixel — rasterizer indices are window-local.
    win->gt[0] = extent.xMinimum() + col0 * xres;
    win->gt[1] = xres;
    win->gt[2] = 0.0;
    win->gt[3] = extent.yMaximum() - row0 * yres;
    win->gt[4] = 0.0;
    win->gt[5] = -yres;
    return true;
}

} // namespace

QString RsRoiSemantics::transformToRasterCrs( const QgsGeometry &geometry,
                                              const QgsCoordinateReferenceSystem &geomCrs,
                                              const QgsRasterLayer *raster,
                                              QgsGeometry *out )
{
    if ( !out )
        return QStringLiteral( "internal: null out param" );
    if ( geometry.isNull() || geometry.isEmpty() )
        return QStringLiteral( "roi geometry is null or empty" );
    if ( !raster )
        return QStringLiteral( "null raster layer" );

    const QgsCoordinateReferenceSystem rasterCrs = raster->crs();
    if ( !geomCrs.isValid() || !rasterCrs.isValid() )
        return QStringLiteral( "unresolved CRS (roi or raster)" );
    if ( geomCrs == rasterCrs )
    {
        *out = geometry;
        return QString();
    }

    const QgsCoordinateTransform transform( geomCrs, rasterCrs,
                                            QgsProject::instance()->transformContext() );
    if ( !transform.isValid() )
        return QStringLiteral( "no transform available from roi CRS to raster CRS" );

    QgsGeometry transformed = geometry;
    try
    {
        transformed.transform( transform );
    }
    catch ( const QgsCsException & )
    {
        return QStringLiteral( "CRS transform of the roi geometry failed" );
    }
    if ( transformed.isNull() || transformed.isEmpty() )
        return QStringLiteral( "CRS transform produced an empty geometry" );
    *out = transformed;
    return QString();
}

RsRoiStatsResult RsRoiSemantics::bandStatsPreview(
  QgsRasterLayer *raster,
  const QgsGeometry &roiInGeomCrs,
  const QgsCoordinateReferenceSystem &geomCrs,
  const std::function<bool()> &isCanceled,
  qlonglong maxPixels,
  const QVector<int> &bandNumbers )
{
    RsRoiStatsResult result;
    if ( maxPixels <= 0 )
        maxPixels = kDefaultMaxPixels;

    QgsGeometry inRasterCrs;
    const QString transformError = transformToRasterCrs( roiInGeomCrs, geomCrs, raster, &inRasterCrs );
    if ( !transformError.isEmpty() )
    {
        result.error = transformError;
        return result;
    }

    RasterWindow win;
    if ( !windowForGeometry( raster, inRasterCrs, &win ) )
    {
        result.error = QStringLiteral( "roi does not intersect the raster extent" );
        return result;
    }

    // Center-of-pixel footprint via the analysis-layer oracle, windowed.
    const QSet<quint64> windowIndices = RsPixelRasterizer::rasterize( inRasterCrs, win.gt, win.width, win.height );
    result.pixelCount = windowIndices.size();
    if ( result.pixelCount == 0 )
    {
        result.error = QStringLiteral( "roi covers no pixel centers" );
        return result;
    }
    if ( result.pixelCount > maxPixels )
    {
        result.error = QStringLiteral( "roi exceeds the preview pixel budget (%1 > %2)" )
                       .arg( result.pixelCount ).arg( maxPixels );
        result.pixelCount = 0;
        return result; // fail-closed: never return partial statistics
    }

    const int rasterW = raster->width();
    QgsRasterDataProvider *provider = raster->dataProvider();
    if ( !provider )
    {
        result.error = QStringLiteral( "raster has no data provider" );
        return result;
    }

    const QgsRectangle windowExtent(
      win.gt[0],
      win.gt[3] + win.gt[5] * win.height,
      win.gt[0] + win.gt[1] * win.width,
      win.gt[3] );

    result.bands.reserve( bandNumbers.size() );
    qlonglong validMaskCount = -1;

    for ( const int band : bandNumbers )
    {
        if ( isCanceled && isCanceled() )
        {
            result = RsRoiStatsResult();
            result.error = QStringLiteral( "canceled" );
            return result;
        }
        if ( band < 1 || band > provider->bandCount() )
        {
            result = RsRoiStatsResult();
            result.error = QStringLiteral( "band %1 out of range (1..%2)" )
                           .arg( band ).arg( provider->bandCount() );
            return result;
        }

        std::unique_ptr<QgsRasterBlock> block( provider->block(
          band, windowExtent, win.width, win.height ) );
        if ( !block || block->isEmpty() )
        {
            result = RsRoiStatsResult();
            result.error = QStringLiteral( "failed to read band %1" ).arg( band );
            return result;
        }

        double sum = 0.0;
        double sumSq = 0.0;
        double minV = 0.0;
        double maxV = 0.0;
        qlonglong valid = 0;
        qlonglong polled = 0;

        for ( auto it = windowIndices.constBegin(); it != windowIndices.constEnd(); ++it )
        {
            if ( ++polled % 4096 == 0 && isCanceled && isCanceled() )
            {
                result = RsRoiStatsResult();
                result.error = QStringLiteral( "canceled" );
                return result;
            }
            const quint64 idx = *it;
            const int localRow = static_cast<int>( idx / win.width );
            const int localCol = static_cast<int>( idx % win.width );
            if ( block->isNoData( localRow, localCol ) )
                continue;
            const double v = block->value( localRow, localCol );
            if ( std::isnan( v ) )
                continue;
            if ( valid == 0 )
            {
                minV = maxV = v;
            }
            else
            {
                minV = std::min( minV, v );
                maxV = std::max( maxV, v );
            }
            sum += v;
            sumSq += v * v;
            ++valid;
        }

        RsRoiBandStats stats;
        stats.band = band;
        if ( valid > 0 )
        {
            stats.min = minV;
            stats.max = maxV;
            stats.mean = sum / valid;
            const double variance = std::max( 0.0, sumSq / valid - stats.mean * stats.mean );
            stats.stddev = std::sqrt( variance );
        }
        result.validPixelCount = validMaskCount < 0 ? valid : std::min( result.validPixelCount, valid );
        validMaskCount = result.validPixelCount;
        result.bands.append( stats );
    }

    result.ok = true;
    return result;
}

bool RsRoiSemantics::writeClassLabel( QgsVectorLayer *samples, QgsFeatureId fid,
                                      int classId, const QString &classField,
                                      RsEditSession *session, QString *error )
{
    auto fail = [error]( const QString & msg )
    {
        if ( error )
            *error = msg;
        return false;
    };
    if ( !samples )
        return fail( QStringLiteral( "null sample layer" ) );
    if ( session )
    {
        const QString id = samples->id();
        if ( !session->isAttached( id ) )
            return fail( QStringLiteral( "sample layer is not attached to the edit session" ) );
        if ( session->isLocked( id ) )
            return fail( QStringLiteral( "sample layer is locked" ) );
    }
    if ( !samples->isEditable() )
        return fail( QStringLiteral( "sample layer is not editable" ) );

    const int fieldIdx = samples->fields().lookupField( classField );
    if ( fieldIdx < 0 )
        return fail( QStringLiteral( "class field not found: %1" ).arg( classField ) );

    RsEditCommandGuard guard( session, samples, QStringLiteral( "set ROI class label" ) );
    if ( session && !guard.isValid() )
        return fail( QStringLiteral( "session refused the label command" ) );
    if ( !samples->changeAttributeValue( fid, fieldIdx, classId ) )
    {
        guard.cancel();
        return fail( QStringLiteral( "attribute change rejected for fid %1" ).arg( fid ) );
    }
    return true;
}
