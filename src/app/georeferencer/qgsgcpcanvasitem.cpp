/***************************************************************************
     qgsgcpcanvasitem.cpp
     --------------------------------------
    Date                 : 14-Feb-2010
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info

    SICNU port (2026-06-03, Task 11.5.2) — refactored to render purely
    from owned visual state; no QgsGeorefDataPoint dependency.
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsgcpcanvasitem.h"

#include "qgsmapcanvas.h"
#include "qgsrasterlayer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QString>

namespace
{
  // Design tokens from design.html ArtboardGeoref
  const QColor kSrcBadge( 0x1f, 0x6f, 0xeb );  // blue
  const QColor kRefBadge( 0x2d, 0xa4, 0x4e );  // green
  const QColor kSelectedFill( 0xbf, 0x87, 0x00 ); // yellow
  constexpr double kRadius = 7.0;
  constexpr int kLabelPt = 9;
}

QgsGCPCanvasItem::QgsGCPCanvasItem( QgsMapCanvas *mapCanvas,
                                    int id,
                                    const QgsPointXY &worldPos,
                                    bool isSource )
  : QgsMapCanvasItem( mapCanvas )
  , mId( id )
  , mWorldPos( worldPos )
  , mIsSource( isSource )
  , mBadgeBrush( isSource ? kSrcBadge : kRefBadge )
  , mSelectedBrush( kSelectedFill )
{
  mResidualPen.setColor( QColor( 255, 0, 0 ) );
  mResidualPen.setWidthF( 2.0 );
  // Sit above map layers / other overlays so GCP badges stay visible.
  setZValue( 2000 );
  setVisible( true );

  updatePosition();
}

void QgsGCPCanvasItem::paint( QPainter *p )
{
  if ( !p )
    return;

  p->setRenderHint( QPainter::Antialiasing );
  p->setOpacity( mEnabled ? 1.0 : 0.4 );

  // Filled circle: badge color, or yellow when selected. Hover uses thicker cyan outline.
  QPen outline( mHovered ? QColor( QStringLiteral( "#39d0d8" ) ) : Qt::black );
  outline.setWidthF( mHovered ? 2.5 : ( mSelected ? 2.0 : 1.0 ) );
  p->setPen( outline );
  p->setBrush( mSelected ? mSelectedBrush : mBadgeBrush );
  p->drawEllipse( QPointF( 0, 0 ), kRadius, kRadius );

  // Numeric label inside the circle (only for non-temporary points)
  if ( mId >= 0 )
  {
    QFont f( QStringLiteral( "IBM Plex Mono" ) );
    f.setPointSize( kLabelPt );
    f.setBold( true );
    p->setFont( f );
    p->setPen( Qt::white );
    const QString s = QString::number( mId );
    const QRectF box( -kRadius, -kRadius, 2 * kRadius, 2 * kRadius );
    p->drawText( box, Qt::AlignCenter, s );
  }

  if ( mIsSource )
  {
    drawResidualArrow( p );
  }
}

QRectF QgsGCPCanvasItem::boundingRect() const
{
  // Include both the marker disc and any residual arrow extent.
  double residualLeft = -kRadius - 2;
  double residualRight = kRadius + 2;
  double residualTop = -kRadius - 2;
  double residualBottom = kRadius + 2;

  if ( mIsSource )
  {
    const double rf = residualToScreenFactor();
    const double rx = mResidual.x() * rf;
    const double ry = mResidual.y() * rf;
    residualLeft = std::min( residualLeft, rx - mResidualPen.widthF() );
    residualRight = std::max( residualRight, rx + mResidualPen.widthF() );
    residualTop = std::min( residualTop, ry - mResidualPen.widthF() );
    residualBottom = std::max( residualBottom, ry + mResidualPen.widthF() );
  }

  return QRectF( QPointF( residualLeft, residualTop ),
                 QPointF( residualRight, residualBottom ) );
}

QPainterPath QgsGCPCanvasItem::shape() const
{
  QPainterPath p;
  p.addEllipse( QPointF( 0, 0 ), kRadius, kRadius );
  return p;
}

void QgsGCPCanvasItem::updatePosition()
{
  if ( !mMapCanvas )
    return;
  // Source canvas: pixel coords; Reference canvas: world coords. In both cases we
  // treat mWorldPos as the position in the canvas' destination CRS — callers pass
  // pixel coordinates for SRC (the canvas extent is in pixels) and map coords for REF.
  setPos( toCanvasCoordinates( mWorldPos ) );
}

void QgsGCPCanvasItem::setId( int id )
{
  if ( mId == id )
    return;
  mId = id;
  update();
}

void QgsGCPCanvasItem::setWorldPos( const QgsPointXY &p )
{
  mWorldPos = p;
  updatePosition();
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::setEnabled( bool e )
{
  if ( mEnabled == e )
    return;
  mEnabled = e;
  update();
}

void QgsGCPCanvasItem::setSelected( bool s )
{
  if ( mSelected == s )
    return;
  mSelected = s;
  update();
}

void QgsGCPCanvasItem::setHovered( bool h )
{
  if ( mHovered == h )
    return;
  mHovered = h;
  update();
}

void QgsGCPCanvasItem::setResidual( QPointF r )
{
  mResidual = r;
  prepareGeometryChange();
  update();
}

void QgsGCPCanvasItem::checkBoundingRectChange()
{
  prepareGeometryChange();
}

void QgsGCPCanvasItem::drawResidualArrow( QPainter *p )
{
  if ( !mMapCanvas )
    return;
  if ( mResidual.isNull() )
    return;

  const double rf = residualToScreenFactor();
  p->setPen( mResidualPen );
  p->drawLine( QPointF( 0, 0 ), QPointF( mResidual.x() * rf, mResidual.y() * rf ) );
}

double QgsGCPCanvasItem::residualToScreenFactor() const
{
  if ( !mMapCanvas )
    return 1.0;

  const double mapUnitsPerScreenPixel = mMapCanvas->mapUnitsPerPixel();
  if ( mapUnitsPerScreenPixel <= 0 )
    return 1.0;
  double mapUnitsPerRasterPixel = 1.0;

  const QList<QgsMapLayer *> canvasLayers = mMapCanvas->mapSettings().layers();
  if ( !canvasLayers.isEmpty() )
  {
    if ( QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>( canvasLayers.first() ) )
      mapUnitsPerRasterPixel = rl->rasterUnitsPerPixelX();
  }

  return mapUnitsPerRasterPixel / mapUnitsPerScreenPixel;
}
