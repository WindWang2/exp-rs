// rs_segment_select_tool.cpp — Phase 10B Task 10B.5
#include "rs_segment_select_tool.h"

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgspointxy.h>

#include <gdal.h>

#include <QMouseEvent>
#include <cmath>
#include <cstring>

RsSegmentSelectTool::RsSegmentSelectTool( QgsMapCanvas *canvas )
    : QgsMapTool( canvas )
{
    setCursor( Qt::CrossCursor );
}

RsSegmentSelectTool::~RsSegmentSelectTool()
{
    // A null canvas means QgsMapCanvas::~QgsMapCanvas already ran: it deletes
    // every scene item (rubber bands included) before ~QObject destroys its
    // tool children, so the item is gone and deleting it here would double
    // free.
    if ( canvas() )
    {
        delete mRubberBand;
    }
    mRubberBand = nullptr;
}

void RsSegmentSelectTool::setSegmentMap( const RsSegmentMap &segMap )
{
    mSegMap = segMap;
    clearSelection();
}

void RsSegmentSelectTool::setGeoTransform( const double gt[6] )
{
    std::memcpy( mGeoTransform, gt, 6 * sizeof( double ) );
}

void RsSegmentSelectTool::clearSelection()
{
    mSelectedSegId = 0;
    if ( mRubberBand )
    {
        mRubberBand->reset( Qgis::GeometryType::Polygon );
        delete mRubberBand;
        mRubberBand = nullptr;
    }
    emit selectionCleared();
}

void RsSegmentSelectTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    if ( mSegMap.isEmpty() )
        return;

    // Map coordinate → pixel coordinate.
    // swarm-review APP-R-5: the previous inverse dropped the rotation terms
    // (gt[2]/gt[4]), so on a rotated raster every click landed on the wrong
    // pixel. Use the full 6-parameter inverse (same as
    // rs_roi_tool_magicwand.cpp); a degenerate geotransform cannot be mapped.
    QgsPointXY mapPoint = e->mapPoint();
    double x = mapPoint.x();
    double y = mapPoint.y();

    double inv[6];
    if ( !GDALInvGeoTransform( mGeoTransform, inv ) )
        return;
    const int col = static_cast<int>( std::floor( inv[0] + inv[1] * x + inv[2] * y ) );
    const int row = static_cast<int>( std::floor( inv[3] + inv[4] * x + inv[5] * y ) );

    quint32 segId = mSegMap.labelAt( row, col );
    if ( segId == 0 )
    {
        clearSelection();
        return;
    }

    mSelectedSegId = segId;
    highlightSegment( segId );
    emit segmentSelected( segId );
}

void RsSegmentSelectTool::highlightSegment( quint32 segmentId )
{
    if ( !mRubberBand )
    {
        // MEDIUM #11 fix: use Point mode for correct per-pixel highlighting
        mRubberBand = new QgsRubberBand( canvas(), Qgis::GeometryType::Point );
        mRubberBand->setIcon( QgsRubberBand::ICON_CIRCLE );
        mRubberBand->setIconSize( 6 );
        mRubberBand->setColor( QColor( 255, 255, 0, 180 ) );
        mRubberBand->setFillColor( QColor( 255, 255, 0, 120 ) );
    }
    mRubberBand->reset( Qgis::GeometryType::Point );

    // Add each pixel center as a point marker with doUpdate = false except last
    auto coords = mSegMap.pixelCoords( segmentId );
    if ( coords.isEmpty() )
        return;

    constexpr int kMaxPoints = 5000;
    const int total = coords.size();
    const int step = std::max( 1, total / kMaxPoints );

    for ( int i = 0; i < total; i += step )
    {
        const QPoint &pt = coords[i];
        // swarm-review APP-R-5: forward affine must include the rotation terms
        // (gt[2]/gt[4]) so highlighted markers land on the segment on rotated
        // rasters.
        const double mapX = mGeoTransform[0] + ( pt.x() + 0.5 ) * mGeoTransform[1]
                            + ( pt.y() + 0.5 ) * mGeoTransform[2];
        const double mapY = mGeoTransform[3] + ( pt.x() + 0.5 ) * mGeoTransform[4]
                            + ( pt.y() + 0.5 ) * mGeoTransform[5];
        const bool isLast = ( i + step >= total );
        mRubberBand->addPoint( QgsPointXY( mapX, mapY ), isLast );
    }
}
