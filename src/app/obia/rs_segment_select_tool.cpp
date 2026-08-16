// rs_segment_select_tool.cpp — Phase 10B Task 10B.5
#include "rs_segment_select_tool.h"

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgspointxy.h>

#include <QMouseEvent>
#include <cstring>

RsSegmentSelectTool::RsSegmentSelectTool( QgsMapCanvas *canvas )
    : QgsMapTool( canvas )
{
    setCursor( Qt::CrossCursor );
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
        mRubberBand.reset();
    }
    emit selectionCleared();
}

void RsSegmentSelectTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    if ( mSegMap.isEmpty() )
        return;

    // Map coordinate → pixel coordinate
    QgsPointXY mapPoint = e->mapPoint();
    double x = mapPoint.x();
    double y = mapPoint.y();

    // Inverse geo-transform: pixel = (map - origin) / pixel_size
    int col = static_cast<int>( std::floor( ( x - mGeoTransform[0] ) / mGeoTransform[1] ) );
    int row = static_cast<int>( std::floor( ( y - mGeoTransform[3] ) / mGeoTransform[5] ) );

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
        mRubberBand = std::make_unique<QgsRubberBand>( canvas(), Qgis::GeometryType::Point );
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
        double mapX = mGeoTransform[0] + ( pt.x() + 0.5 ) * mGeoTransform[1];
        double mapY = mGeoTransform[3] + ( pt.y() + 0.5 ) * mGeoTransform[5];
        const bool isLast = ( i + step >= total );
        mRubberBand->addPoint( QgsPointXY( mapX, mapY ), isLast );
    }
}
