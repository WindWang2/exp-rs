// rs_sample_erase_tool.cpp — see rs_sample_erase_tool.h.
#include "rs_sample_erase_tool.h"

#include "rs_edit_command_guard.h"
#include "rs_edit_session.h"

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsrubberband.h>
#include <qgsvectorlayer.h>

#include <QColor>

RsSampleEraseTool::RsSampleEraseTool( QgsMapCanvas *canvas )
  : QgsMapTool( canvas )
{
    mRubber = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
    mRubber->setStrokeColor( QColor( 255, 80, 80 ) );
    mRubber->setFillColor( QColor( 255, 80, 80, 50 ) );
    mRubber->setWidth( 1 );
    mRubber->hide();
}

void RsSampleEraseTool::setTargetLayer( QgsVectorLayer *layer )
{
    mLayer = layer;
}

void RsSampleEraseTool::deactivate()
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

QgsGeometry RsSampleEraseTool::discAt( const QgsPointXY &layerPoint ) const
{
    QgsGeometry point = QgsGeometry::fromPointXY( layerPoint );
    return point.buffer( mRadius, kDiscSegments );
}

bool RsSampleEraseTool::strokeStartAllowed( QString *reason ) const
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
            *reason = QStringLiteral( "erase radius must be positive" );
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

void RsSampleEraseTool::collectHits( const QgsGeometry &stamp )
{
    if ( stamp.isNull() )
        return;
    const QgsRectangle bbox = stamp.boundingBox();
    QgsFeatureIterator it = mLayer->getFeatures(
      QgsFeatureRequest( bbox ).setFlags( Qgis::FeatureRequestFlag::ExactIntersect ) );
    QgsFeature f;
    while ( it.nextFeature( f ) )
    {
        if ( f.hasGeometry() && f.geometry().intersects( stamp ) )
            mHitIds.insert( f.id() );
    }
}

void RsSampleEraseTool::canvasPressEvent( QgsMapMouseEvent *e )
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
    mHitIds.clear();
    mStampCount = 0;
    mRubber->reset( Qgis::GeometryType::Polygon );
    mRubber->show();

    const QgsPointXY layerPoint = toLayerCoordinates( mLayer.data(), toMapCoordinates( e->pos() ) );
    const QgsGeometry disc = discAt( layerPoint );
    if ( !disc.isNull() )
    {
        mStamps.append( disc );
        mRubber->addGeometry( disc, mLayer.data() );
        mStampCount = 1;
        collectHits( disc );
    }
}

void RsSampleEraseTool::canvasMoveEvent( QgsMapMouseEvent *e )
{
    if ( !e || !mStroking || !mLayer )
    {
        if ( mStroking && !mLayer )
            cancelStroke();
        return;
    }
    const QgsPointXY layerPoint = toLayerCoordinates( mLayer.data(), toMapCoordinates( e->pos() ) );
    const QgsGeometry disc = discAt( layerPoint );
    if ( disc.isNull() )
        return;

    if ( mStamps.size() >= kMaxStampsPerStroke )
    {
        // Cap accounting: further stamps still count, but only the most
        // recent window is retained; hits collected so far stay valid
        // (erase semantics are per-stamp, not union-dependent).
        mStamps.clear();
    }
    mStamps.append( disc );
    ++mStampCount;
    mRubber->addGeometry( disc, mLayer.data() );
    collectHits( disc );
}

void RsSampleEraseTool::cancelStroke()
{
    mStroking = false;
    mStamps.clear();
    mHitIds.clear();
    mStampCount = 0;
    if ( mRubber )
    {
        mRubber->reset( Qgis::GeometryType::Polygon );
        mRubber->hide();
    }
}

void RsSampleEraseTool::finishStroke()
{
    mStroking = false;
    mStamps.clear();
    if ( mRubber )
    {
        mRubber->reset( Qgis::GeometryType::Polygon );
        mRubber->hide();
    }
    if ( !mLayer )
    {
        mHitIds.clear();
        return;
    }

    if ( mHitIds.isEmpty() )
    {
        mHitIds.clear();
        emit strokeCommitted( 0 ); // nothing under the stroke: not an error
        return;
    }

    const QgsFeatureIds ids = mHitIds;
    mHitIds.clear();

    RsEditCommandGuard guard( mSession.data(), mLayer.data(), QStringLiteral( "erase sample stroke" ) );
    if ( mSession && !guard.isValid() )
    {
        emit strokeRefused( QStringLiteral( "session refused the erase command" ) );
        return;
    }
    if ( !mLayer->deleteFeatures( ids ) )
    {
        guard.cancel();
        emit strokeRefused( QStringLiteral( "layer rejected feature deletion" ) );
        return;
    }
    emit strokeCommitted( ids.size() );
}

void RsSampleEraseTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    if ( !e || e->button() != Qt::LeftButton )
        return;
    if ( !mStroking )
        return;
    finishStroke();
}
