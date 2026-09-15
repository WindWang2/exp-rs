// rs_sample_brush_tool.cpp — see rs_sample_brush_tool.h.
#include "rs_sample_brush_tool.h"

#include "rs_edit_command_guard.h"
#include "rs_edit_session.h"

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsrubberband.h>
#include <qgsvectorlayer.h>

#include <QColor>

namespace
{

/// Merge a batch of stamp geometries. QgsGeometry::combine is an instance
/// method here (no static multi-geometry union), so fold sequentially; the
/// per-stroke stamp cap keeps the fold bounded.
QgsGeometry mergeStamps( const QgsGeometry &head, const QVector<QgsGeometry> &stamps )
{
    if ( head.isNull() && stamps.isEmpty() )
        return QgsGeometry();
    QgsGeometry result = head;
    for ( const QgsGeometry &g : stamps )
    {
        if ( g.isNull() )
            continue;
        result = result.isNull() ? g : result.combine( g );
    }
    return result;
}

} // namespace

RsSampleBrushTool::RsSampleBrushTool( QgsMapCanvas *canvas )
  : QgsMapTool( canvas )
{
    mRubber = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
    mRubber->setStrokeColor( QColor( 60, 180, 255 ) );
    mRubber->setFillColor( QColor( 60, 180, 255, 70 ) );
    mRubber->setWidth( 1 );
    mRubber->hide();
}

void RsSampleBrushTool::setTargetLayer( QgsVectorLayer *layer )
{
    mLayer = layer;
}

void RsSampleBrushTool::deactivate()
{
    if ( mStroking )
        cancelStroke();
    if ( mRubber )
    {
        mRubber->reset( Qgis::GeometryType::Polygon );
        mRubber->hide();
    }
    QgsMapTool::deactivate();
}

QgsGeometry RsSampleBrushTool::discAt( const QgsPointXY &layerPoint ) const
{
    QgsGeometry point = QgsGeometry::fromPointXY( layerPoint );
    return point.buffer( mRadius, kDiscSegments );
}

bool RsSampleBrushTool::strokeStartAllowed( QString *reason ) const
{
    if ( !mLayer )
    {
        if ( reason )
            *reason = QStringLiteral( "no target layer" );
        return false;
    }
    if ( !mLayer->isEditable() )
    {
        if ( reason )
            *reason = QStringLiteral( "target layer is not editable" );
        return false;
    }
    if ( mRadius <= 0 )
    {
        if ( reason )
            *reason = QStringLiteral( "brush radius must be positive" );
        return false;
    }
    if ( mSession )
    {
        const QString id = mLayer->id();
        if ( !mSession->isAttached( id ) )
        {
            if ( reason )
                *reason = QStringLiteral( "target layer is not attached to the edit session" );
            return false;
        }
        if ( mSession->isLocked( id ) )
        {
            if ( reason )
                *reason = QStringLiteral( "target layer is locked" );
            return false;
        }
    }
    return true;
}

void RsSampleBrushTool::canvasPressEvent( QgsMapMouseEvent *e )
{
    if ( !e || e->button() != Qt::LeftButton )
        return;
    QString reason;
    if ( !strokeStartAllowed( &reason ) )
    {
        emit strokeRefused( reason );
        return;
    }
    mStroking = true;
    mStamps.clear();
    mCombined = QgsGeometry();
    mCoalescedCount = 0;
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->show();

    const QgsPointXY layerPoint = toLayerCoordinates( mLayer.data(), toMapCoordinates( e->pos() ) );
    const QgsGeometry disc = discAt( layerPoint );
    if ( !disc.isNull() )
    {
        mStamps.append( disc );
        mRubber->addGeometry( disc, mLayer.data() );
    }
}

void RsSampleBrushTool::canvasMoveEvent( QgsMapMouseEvent *e )
{
    if ( !e || !mStroking )
        return;
    const QgsPointXY layerPoint = toLayerCoordinates( mLayer.data(), toMapCoordinates( e->pos() ) );
    const QgsGeometry disc = discAt( layerPoint );
    if ( disc.isNull() )
        return;
    mStamps.append( disc );
    mRubber->addGeometry( disc, mLayer.data() );

    if ( mStamps.size() >= kMaxStampsPerStroke )
    {
        // Coalesce: bounded memory per stroke (PERFORMANCE). The collected
        // stamps fold into the merged head; the pending vector resets.
        mCombined = mergeStamps( mCombined, mStamps );
        mCoalescedCount += mStamps.size();
        mStamps.clear();
        if ( mCombined.isNull() )
        {
            cancelStroke();
            emit strokeRefused( QStringLiteral( "stamp coalescing failed" ) );
        }
    }
}

void RsSampleBrushTool::cancelStroke()
{
    mStroking = false;
    mStamps.clear();
    mCombined = QgsGeometry();
    mCoalescedCount = 0;
    if ( mRubber )
    {
        mRubber->reset( Qgis::GeometryType::Polygon );
        mRubber->hide();
    }
}

void RsSampleBrushTool::finishStroke()
{
    const QgsGeometry stroke = mergeStamps( mCombined, mStamps );
    mStamps.clear();
    mCombined = QgsGeometry();
    mCoalescedCount = 0;
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->hide();

    if ( stroke.isNull() || stroke.isEmpty() )
    {
        mStroking = false;
        emit strokeRefused( QStringLiteral( "empty stroke" ) );
        return;
    }
    mStroking = false;

    RsEditCommandGuard guard( mSession.data(), mLayer.data(), QStringLiteral( "brush sample stroke" ) );
    if ( mSession && !guard.isValid() )
    {
        emit strokeRefused( QStringLiteral( "session refused the stroke command" ) );
        return;
    }
    // A stroke is a multipart geometry; a single-part target layer would
    // only fail later at commit. Refuse explicitly instead (never silent).
    if ( stroke.isMultipart()
         && QgsWkbTypes::isSingleType( mLayer->wkbType() ) )
    {
        guard.cancel();
        emit strokeRefused( QStringLiteral(
          "target layer is single-part; use a MultiPolygon sample layer" ) );
        return;
    }
    QgsFeature f( mLayer->fields() );
    f.setGeometry( stroke );
    if ( !mLayer->addFeature( f ) )
    {
        guard.cancel();
        emit strokeRefused( QStringLiteral( "layer rejected the stroke feature" ) );
        return;
    }
    emit strokeCommitted( 1 );
}

void RsSampleBrushTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    if ( !e || e->button() != Qt::LeftButton )
        return;
    if ( !mStroking )
        return;
    finishStroke();
}
